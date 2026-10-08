# Lean 4

A [Lean 4](https://lean-lang.org/) formalization of triage calculus (the
[reduction rules](../../reduction-rules) used throughout this repo):
the basics, an eager evaluator, and a machine-checked proof of

> **Theorem** (`Eval.sn`): if eager evaluation terminates on an expression,
> then that expression is *strongly normalizing* — **every** reduction
> sequence, under **any** strategy, is finite.

This gives the eager evaluators in this repo (C++, asm, wasm, ...) a strong
guarantee for free: whenever they return a result, no other reduction order
could have diverged on that input (and by orthogonality/confluence, no other
order can produce a different value either).

## Contents

| File | What's in it |
| ---- | ------------ |
| [`TreeCalculus/Basic.lean`](TreeCalculus/Basic.lean) | Terms (`△`, application), values, the five reduction rules (1), (2), (3a), (3b), (3c) as `Root`, one-step reduction `Step` (closure under contexts), multi-step `Steps`, strong normalization `SN` (defined via accessibility, with the "no infinite reduction sequence" reading proven) |
| [`TreeCalculus/Eval.lean`](TreeCalculus/Eval.lean) | Eager evaluation, twice: as big-step derivations (`Apply`, `Eval`) and as an executable fuel-based evaluator (`applyWithFuel`, `evalWithFuel`), with soundness, monotonicity and completeness proofs connecting the two. Also `Term.applyOrStuck`, a faster executable applier that reports running out of fuel in-band as `stuck` instead of through `Option`, proven sound (`Term.applyOrStuck_sound`). Also: eager results are values, values are exactly the normal forms, and `Eval t v → t ⟶* v` |
| [`TreeCalculus/ValueOrStuck.lean`](TreeCalculus/ValueOrStuck.lean) | `ValueOrStuck`, values (leaf, stem, fork) one cell per node, plus `stuck` for running out of fuel, and `ValueOrStuck.applyOrStuck`, `Term.applyOrStuck` over it: proven to compute exactly what `Term.applyOrStuck` does (`ValueOrStuck.applyOrStuck_toTerm`), hence sound (`ValueOrStuck.applyOrStuck_sound`). `Term` needs no such variant: `Term.stuck` is one of its redexes |
| [`TreeCalculus/Value.lean`](TreeCalculus/Value.lean) | `Value`, the same without `stuck`, and `Value.applyWithFuel`, `Term.applyWithFuel` over it: proven to compute exactly what `Term.applyWithFuel` does (`Value.applyWithFuel_toTerm`), hence sound (`Value.applyWithFuel_sound`) |
| [`TreeCalculus/StrongNormalization.lean`](TreeCalculus/StrongNormalization.lean) | The main theorem `Eval.sn : Eval t v → SN t` and its corollary `sn_of_evalWithFuel : evalWithFuel n t = some v → SN t` |
| [`TreeCalculus/Examples.lean`](TreeCalculus/Examples.lean) | `#guard` tests exercising every rule, a diverging term, and example SN certificates obtained by running the evaluator inside `decide` |
| [`TreeCalculus/Machine.lean`](TreeCalculus/Machine.lean) | `step`: `Apply` as a loop over a stack of frames; `run_iff_apply` |
| [`TreeCalculus/Runtime.lean`](TreeCalculus/Runtime.lean) | `RStep J`: `step` with a memo (any policy) and jets `J`; `runtime_sound` |
| [`TreeCalculus/Symbolic.lean`](TreeCalculus/Symbolic.lean) | `sstep`: `step` on values with variables; `sstep_next` |
| [`TreeCalculus/Check.lean`](TreeCalculus/Check.lean) | `checkRun`, `checkDropThrough`: kernel-decided, sound for every argument; `DropJet.reaches` |
| [`TreeCalculus/RuleJets.lean`](TreeCalculus/RuleJets.lean) | Bigger-step rules over patterns with holes: `Rule` (a pair of symbolic states), `checkRule ρ ℓ` (the symbolic run from the left side reaches the right side in exactly `ℓ` steps, kernel-decided), `checkRule_sound`, `checkRule_inst`, `RuleJet` and `rules_runtime_sound` (`runtime_sound` for a runtime that fires checked rules, `RStep` unchanged), `RuleTree` (shape-split rules), memo entries as ground rules, `rules_length_bound` |
| [`TreeCalculus/Progress.lean`](TreeCalculus/Progress.lean) | Termination: with jets that take at least one step (`Plus`) and a policy that idles finitely, the runtime returns `r` iff the machine does (`runtime_returns_iff`, `memoPolicy_returns_iff`); `checkRule_plus`; `RStep` alone is not well-founded; peek.hpp's `S K y b ⟶ b` is term-sound but returns where the machine diverges |
| [`Rules.lean`](Rules.lean), [`Rules/`](Rules/) | Rules derived from library trees by `Rules/gen-rules.mjs` and checked by `decide +kernel`: `fix f x ⟶ f (fix f) x` and 52 more, 7 shape-split trees (`Rules/Core.lean`); `fix_plus` |

`Eval.sn` is proven without any axioms (`#print axioms Eval.sn` reports none —
not even `propext` or choice). So are `run_iff_apply` and `runtime_sound`; the
checkers' soundness uses `propext`, `Classical.choice` and `Quot.sound`.

## Why the theorem is interesting

"The eager evaluator halted" is a statement about *one* strategy; strong
normalization quantifies over *all* of them. The implication is genuinely
false in the λ-calculus — `λx.Ω` is a call-by-value normal form but contains
a diverging subterm — and it fails in rewrite systems with overlapping rules.
It holds here because the five triage rules form an *orthogonal* rewrite
system (left-linear, no critical pairs) and eager evaluation is an innermost
strategy: results in the spirit of O'Donnell and Gramlich say that for such
systems innermost termination implies termination. This formalization proves
the implication directly for triage calculus, without developing that general
theory:

1. **Preservation** (`Eval.step_preserved`): a single reduction step
   anywhere — any rule, any position, not just eager ones — preserves the
   result of eager evaluation. Orthogonality shows up concretely: a step is
   either inside a subterm that eager evaluation evaluates anyway, or it is
   a root contraction that eager evaluation also performs, just in a
   different order.
2. **Closure** (`sn_app_closure`): by a double well-founded induction on
   `SN s` and `SN t`, the application `s ⬝ t` is strongly normalizing as soon
   as every *root* contraction of every reduct `s' ⬝ t'` is. Preservation
   keeps the eager values of `s'`, `t'` pinned while `s` and `t` reduce.
3. **Heart** (`apply_sn`): structural induction on the big-step `Apply`
   derivation of the two eager values. When a root rule fires on a reduct
   `s' ⬝ t'`, the fired pattern is necessarily mirrored in the eager values
   (a reduct of something that evaluates to a fork with a stem child is
   itself such a fork, etc.), so the contractum is covered by the induction
   hypotheses for the sub-derivations — each one strictly smaller because it
   performs the "rest" of the evaluation after that same contraction.

The converse (`SN` implies eager termination) is comparatively boring: on a
strongly normalizing term every strategy terminates, the eager one included.

## The machine, a runtime, and jets

```lean
theorem run_iff_apply : Run a b r ↔ Apply a.toTerm b.toTerm r.toTerm
theorem runtime_sound (hj : ∀ s t, J s t → Reaches s t) (hm : m.Sound)
    (h : Star (RStep J) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    Apply a.toTerm b.toTerm r.toTerm ∧ m'.Sound
theorem DropJet.reaches (h : checkDropThrough F ks n = true) : DropJet F ks s t → Reaches s t
```

With `Eval.sn_steps_value`, `a ⬝ b` is strongly normalizing and reduces to `r`
(`Runtime.lean`'s closing example). The C++ runtime: [`Cpp/`](Cpp/README.md).

## Building

Install [elan](https://github.com/leanprover/elan) (the Lean toolchain
manager), then:

```sh
lake build
```

The pinned toolchain (`lean-toolchain`) is downloaded automatically on first
use. There are no external dependencies (no mathlib); the `#guard` tests in
`Examples.lean` run as part of the build, which also builds every module
under `TreeCalculus/` and [`Cpp`](Cpp/README.md).

## Benchmark executable

`Main.lean` wraps the verified evaluators (`ValueOrStuck.applyOrStuck`,
`Term.applyOrStuck`, `Value.applyWithFuel`, `Term.applyWithFuel`) in a
command-line program that
[`benchmark/`](../../benchmark) times alongside the other implementations:
ternary-encoded trees on stdin, one per line, applied as a left fold, result
printed in ternary. It is not a default target:

```sh
lake build tree-calculus
.lake/build/bin/tree-calculus --list                   # the evaluators, fastest first
.lake/build/bin/tree-calculus --evaluator NAME         # default: ValueOrStuck.applyOrStuck
```
