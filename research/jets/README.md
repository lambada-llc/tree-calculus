# Bigger-step reduction: jets over patterns with holes

Research notes, October 2026. These notes are not meant to merge as they are. The Lean modules
they rely on are real modules of the formalization (`implementation/lean`, below) and build with
`lake build`. The C++ work is prototype patches against `implementation/cpp` at `127992a`.

## The question

The C++ runtime's only jet, `skip_line`, is keyed on one whole tree: it answers `apply(F, x)` for a
fixed `F`. Programs mostly embed *unknown* trees in *known* tree patterns. A person reduces
`fix f x` to `f (fix f) x` in one step whatever `f` is, because `fix f` is a pattern with a hole.
The question was what such rules are, how to find them, how to prove them, how to match them
cheaply, and how much faster a reducer gets with them.

## Answer in brief

* **Theory is settled and formalized.**
  * A bigger-step rule is a pair of symbolic machine states, and it can be derived by running the
    machine's own `step` over a pattern with holes (`Symbolic.lean`'s `sstep`).
  * The kernel checks a rule by running it: `checkRule ρ ℓ`.
  * `runtime_sound` already admits such rules, with no change to `RStep`.
  * A rule checked at a length of at least 1 is a `Plus`-jet. Under any policy that idles
    finitely, a runtime firing such rules returns `r` exactly when the machine does
    (`Progress.lean`).
* **Rules can be derived and checked automatically, at scale.**
  * `Rules/gen-rules.mjs` derives the rules of arboretum's core and lambada's prelude, including
    `fix f x ⟶ f (fix f) x` (21 transitions, no axioms).
  * These 53 rules and 7 shape-split trees are kernel-checked in about 0.35 s.
  * All 1,669 rules of the whole library check in about 97 s with `kernel_rfl`.
* **Steps are not time.**
  * Rule-shaped speedups cut machine steps by 45–80%.
  * Wall time falls by much less, because about 80% of the time goes to hash-consing and memo
    probes, not to the five rules.
* **The general mechanism works.** A per-head derived-rule cache in the C++ eager machine gives:

  | Workload | Wall time | Steps (real reduce) |
  |---|---|---|
  | benchmark programs | 1.3–4.9× faster | |
  | arboretum test mixes | −25–33% | |
  | whole bundle | −14% | −55% |

  Every symbol and every benchmark output is identical, and 4.8M randomized differential cases
  give 0 mismatches. The cache finds the hand-written combinator and `fix` jets by itself.
* **The biggest remaining levers are outside rule dispatch:**
  * cheaper interning;
  * not hash-consing transient closures, while keeping memo keys;
  * a semantic jet for the TC-in-TC normalizer (`Quoted._whnf`/`_nf`), which is about two thirds
    of the arboretum build's test time.

## Theory

### Rules, and why they are safe

`TreeCalculus/RuleJets.lean`:

* A `Rule` has `lhs` and `rhs`, both of type `State SValue`. It holds if every instance of `lhs`
  runs, by `step`, to the same instance of `rhs`.
* `checkRule ρ ℓ := ssteps ℓ ρ.lhs = some ρ.rhs` means the symbolic run reaches the right side in
  exactly `ℓ` steps without a case split. It is decided by the kernel.
* `checkRule_steps` gives the exact length on every instance (by `sstep_next`).
* `checkRule_inst` shows that a rule checked on a general pattern holds on every refinement.

A runtime that fires checked rules under any stack is an `RStep (RuleJet rules)`, and
`rules_runtime_sound` is `runtime_sound` applied to it. Two more facts:

* Memo entries are exactly the ground rules (`memoRule_holds_iff`).
* A `RuleTree` (case splits on a hole's shape) is a list of refined rules. The tree is an index
  for matching, not part of soundness.

At the term level, adding rules `S ⊆ →⁺` leaves the following unchanged: `→*`, normal forms,
confluence, and SN/WN per term (Terese). The system stops being orthogonal but stays a confluent
overlay system. Gramlich 1995 (also formalized in the Isabelle AFP) gives innermost termination
⇔ termination per term.

### Termination

`TreeCalculus/Progress.lean`:

* `RStep` on its own is not well-founded, even with no jets: `forget` can stutter and `miss` can
  push frames forever.
* Glue the memo frames back onto the stack. Then every transition either moves forward at least
  one machine step or is bookkeeping (`RStep.progress`).
* `runtime_returns_iff` and `runtime_terminates_iff`: under a policy that never gets stuck and
  idles finitely, the runtime returns `r` exactly when the machine does.
* `memoPolicy` is such a policy *provided the jets are `Plus`-jets*. A stuttering jet
  (`J = Eq`) loops (`memoPolicy_eq_jet_loops`).
* `checkRule_plus`: every rule checked at a length of `ℓ + 1` is a `Plus`-jet. Only length `0`
  (lhs = rhs) is not, so a rule checker should refuse it.
* `DropJet.plus`, `Cpp.Jets.skipLine_plus` and `Rules.fix_plus` show the existing jet and the fix
  rules make progress.
* Peek: `implementation/cpp/peek.hpp`'s row `△(△(△△)) y b = b` is term-sound but not
  eager-exact. On `△(△(△△)) ω ω` the `-peek` evaluators return while the eager machine loops
  (`Peek.not_sound`). The Lean README's "a returned result certifies SN" does not hold for them.
  They are not used in production.
  * A guarded row (skip `y b` only when `y` is a leaf, a stem or `△△z`) is eager-exact and keeps
    nearly all of the speedup.

### A hierarchy of jets

| Order | What is known | Example | How it is checked |
|---|---|---|---|
| 0 ground | the whole `(a, b)` | memo entries | `checkRun` / `run_iff_apply` |
| 1 parametric | the head, with holes never inspected | `fix f x ⟶ f (fix f) x`, `:c f g ⟶ △(△f)(△△g)`, `B f g x ⟶ f (g x)` | `checkRule` (straight-line `sstep`) |
| 2 shape-dependent | a finite case split on a hole's shape | a triage dispatch, one loop iteration | `RuleTree` of refined rules |
| 3 inductive | a rule that folds back onto its own left side | `skip_line` (`DropJet`) | induction (`checkDropThrough`) |
| 4 semantic | a whole algorithm on a data encoding | `equal` by node index under exact hash-consing, Nat via machine integers, the quoted normalizer | a functional-correctness proof or a differential test |

Two further distinctions cut across the table:

* **Soundness class.** *Eager-exact* rules follow the machine's own path (`Reaches`). *Term-sound*
  rules follow some path of the term system (`Steps`, as peek does).
* **Frame patterns.** Some rules' left sides include continuation frames. The B/C shape jets need
  a new frame kind (`APPLY_FN`).

Limits:

* With rules of at most `L` transitions and no memo, a run takes at least `N / L` transitions
  where the machine takes `N` (`rules_length_bound`). Bounded rule sets give constant factors.
* Asymptotic wins need the memo or orders 3–4. For example, `equal` on two equal 10,000-element
  lists takes 3.27M steps, against a single index compare.
* "Has a finite rule set for this head" is Σ₁-complete. Fuel-bounded derivation is decidable.

### Prior art

* **Urbit/Nock jets** are already first-order on the data side: the battery is matched and the
  sample is a hole (Vere `jets.c:_cj_fine`). They are matched when a core is built and cached per
  call site. They are bound to label paths rather than content, which led to real mismatches
  (vere#871). Correctness rests on differential testing.
* **Simplicity** jets are explicit nodes, so matching is free. They are mostly tested; only two
  are verified with VST.
* **Agda** checks a builtin's defining equations symbolically. This is the same idea as
  `checkRule`.
* **Chordata** (Kirisame et al., arXiv 2603.19560, 2026) learns "shortcut" rules over machine
  states by unification, indexes them in a discrimination tree, and reports 13× at 20× memory.
  It is the closest learned-rule system.
* **Supercompilation / partial evaluation** ("driving" with case splits) is what `sstep`/`explore`
  already are.
* **Superinstructions and tracing JITs** are the profile-driven, online form of the same idea.
* **Matching:**
  * Hoffmann–O'Donnell bottom-up tree automata, and Ertl's DAG labelling, could store one state
    per hash-consed node at intern time.
  * Anti-unification (Plotkin) over-generalizes: two `fix` instances generalize to 14 variables.
    Holes should come from what reduction never inspects.

## Measurements

The workloads are:

* the arboretum bundle (all definitions plus 1,295 tests, 2.69G steps);
* replays of the build's test runner processes;
* the `benchmark/` programs.

Measurements were taken on a shared 4-vCPU VM, and wall times are best of N, interleaved.

### Where steps go

The bundle's rule mix:

| Rule | Share of steps |
|---|---|
| K | 29.4% |
| S | 30.2% |
| leaf/stem builds | 28% |
| triage | 5.7% |
| memo hits | 6.2% |

Within that:

* 74.8% of S steps are the B shape `△(△(△△f))g`.
* `:c f` closure builds alone are about 20% of steps: 16 steps each, and the memo almost never
  helps.
* `fix f x` costs 13 steps per unfold (core.lamb's comment is right) but is only 1–3% of time.
  Hot compiler loops recurse through `self_apply` instead.
* In compiled code, combinator plumbing is about 90% of steps. Statically, `:b` heads 63% of code
  nodes and `:c` 19%.

### Where time goes

Sampled CPU, on a replayed build test process of 1.02G steps:

| Activity | Share of time |
|---|---|
| hash-consing (intern search 29.8%, insert 28.1%, alloc 4.7%) | 62.6% |
| memo lookup | 18.5% |
| `rebuild_interned` | 8.1% |
| dispatch | 5.2% |
| frames | 2.8% |
| GC | 1.5% |

Costs per event:

* An intern search costs about 133 ns and a memo probe about 75 ns, against about 5 ns per K/S
  step. The marginal cost is 0.4–0.7× of that, because misses overlap.
* ns/step tracks the memory footprint: 32 ns at a 32 MB budget, 81 ns at 2 GB. A smaller budget
  loses memo entries, though, and steps grow 6×.

Node lifetimes:

* 99.94% of built nodes are dead by the end of the command.
* 62.6% are kept alive only by the memo, and 37.4% are dead even counting the memo.
* By shape they are closures, not data: S-closures 39%, stems 37%, K 15%, triage 9%.

By named function:

* The TC-in-TC lazy normalizer (`Quoted._whnf`/`_nf`, via `Certify.Size._lazily`) is about 66% of
  the heaviest test process (80% of `Snat.Size`, 74% of `Certify.Size`). It costs about
  1,400–3,000 meta-steps per object step.
* Binary Nat arithmetic is 58–66% of the Melody/Wav tests.
* `equal` is 0.3% and fix is ≤2.3%.

### Prototypes

**Hand-written jets** (`cpp/hand-jets.patch`): B shape, C closures, fix.

* Bundle steps fall 46% with an identical hash over every symbol.
* Wall time falls 8–18%.
* The fix jet saves 12 steps per firing without the memo but only 0.5–1 per firing on the
  bundle, because it skips memo checkpoints.

**Derived-rule cache** (`cpp/derived-rules.patch`, which applies to `127992a` and brings its
tools in `implementation/cpp/rules-prototype/`):

* It runs only for a head that keeps missing the memo, counted per candidate head (Misra–Gries
  per bucket).
* It derives the head's rule by running `apply()`'s transitions symbolically from
  `reduce(a, HOLE)`. Derivation stops when a transition needs `HOLE`'s shape or applies it, or at
  a cap.
* It stores a template: the hole-dependent nodes the residual reaches, plus the residual state.
* Later applications intern only those nodes and continue from the residual.
* Rule state shares the `_cold` cache lines, so a check that does not fire costs +7–9%
  instructions with no extra cache misses.

Results (the hand jets column is the B/C/fix patch above):

| Workload | Rules | Hand jets |
|---|---|---|
| recursive-fib | −80% (4.9×) | −1% |
| silly-exp | −63% | −4% |
| exercise-rules | −52% | +5% |
| merge-sort | −20% | −12% |
| parallel-equal | −57% | −1% |
| arboretum test mixes | −25% to −33% | −10% to −17% |
| `Snat.Size.47` | −45% | −44% |
| whole bundle | −14% | −18% |

Correctness evidence:

* an identical hash over all 2,849 bundle symbols across 8 configurations;
* all benchmark outputs identical;
* 4.8M randomized differential cases against an independent evaluator, with tiny caps, mid-run
  GCs, shape splits and pattern rules: 0 mismatches.

Design findings:

* **Drop the memo checkpoints inside a rule.** Keeping them forces their key nodes to be built:
  fib then builds 346K nodes instead of 640 and runs 2.7× slower.
* **Count misses per candidate head, not per bucket.** Rules derived on the bundle drop from 2.9M
  to 806K, and derivation time from 6.3 s to 1.1 s.
* **Order-2 splits and order-1b pattern rules help only some workloads.** Splits help fixed-code
  loops. Provenance-keyed pattern rules find `fix f x` automatically and give −29% on one test,
  but cost 7% on the bundle. Both should be tiered in only for rules that keep firing.
* **Interning is now the floor**, at 50–58% of time with rules. 75% of firings on library code
  build closures that the next application consumes at once.

## What to do next, by expected gain

1. **A semantic jet for the quoted normalizer** gives about 2.5–3× on the build's test phase.
   It has to reproduce `normalize_lazy`'s exact fuel and stuck semantics. It is order 4: it needs
   a specification in Lean or a differential test harness.
2. **Make interning cheaper, and stop hash-consing transient closures** while keeping memo keys.
   The estimated bound is 2–3.4×. Options:
   * tag bits in the intern table, so a probe mismatch never loads the arena;
   * a nursery hash-cons table that promotes a node when it becomes a memo key;
   * fusing closure construction with its immediate application across rule boundaries.
3. **Productionize the derived-rule cache** behind a flag, with compiled hot templates. Emit
   templates as `Rule` certificates so a sample can be checked offline by `checkRule`; the C++
   derivation is the trusted part.
4. **Upstream the Lean modules** (`RuleJets`, `Progress`, `Rules`). Fix the peek row with the guard,
   or qualify the Lean README's SN claim for `-peek`.

Combined, items 1 and 2 are estimated at 5–10× on the build's tests.

## Files

* `implementation/lean/TreeCalculus/{RuleJets,Progress,ProgressCpp}.lean`,
  `implementation/lean/Rules.lean`, `implementation/lean/Rules/` (`gen-rules.mjs` writes
  `Core.lean` from `trees/core.dag`). Build with `lake build`. There is no `sorry` or
  `native_decide`, and `#print axioms Rules.Core.fix_2_check` reports none.
* `cpp/derived-rules.patch`: the derived-rule cache, with every variant behind `-D` flags, plus
  its tools.
* `cpp/hand-jets.patch`: the B/C/fix jets, against the header as of 6 Oct (it predates `5679507`).
* `cpp/drv-jets.cpp`: the bundle driver and "hashenv" differential check.
* `cpp/profile/`: the sampling profiler (`sampler.hpp`), counter and owner instrumentation
  (`cnt.patch`, `own.patch`), and analysis scripts (`pie.py`, `own.py`, ...).
* `js/theory/`: the JS model of `step`/`sstep`, per-head rule simulations, pattern mining and the
  `equal` cost.
* `js/runtime-map/`: a Lean-faithful symbolic deriver (`sym.mjs`, `derive.mjs`) and the derived
  costs of every core/prelude pattern (`derive_all.txt`).

Scripts refer to inputs by the paths of the environment they ran in; adjust before rerunning.
