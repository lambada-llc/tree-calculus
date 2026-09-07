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
| [`TreeCalculus/Eval.lean`](TreeCalculus/Eval.lean) | Eager evaluation, twice: as big-step derivations (`Apply`, `Eval`) and as an executable fuel-based evaluator (`applyF`, `evalF`), with soundness, monotonicity and completeness proofs connecting the two. Also: eager results are values, values are exactly the normal forms, and `Eval t v → t ⟶* v` |
| [`TreeCalculus/StrongNormalization.lean`](TreeCalculus/StrongNormalization.lean) | The main theorem `Eval.sn : Eval t v → SN t` and its corollary `sn_of_evalF : evalF n t = some v → SN t` |
| [`TreeCalculus/Examples.lean`](TreeCalculus/Examples.lean) | `#guard` tests exercising every rule, a diverging term, and example SN certificates obtained by running the evaluator inside `decide` |

`Eval.sn` is proven without any axioms (`#print axioms Eval.sn` reports none —
not even `propext` or choice).

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

## Building

Install [elan](https://github.com/leanprover/elan) (the Lean toolchain
manager), then:

```sh
lake build
```

The pinned toolchain (`lean-toolchain`) is downloaded automatically on first
use. There are no external dependencies (no mathlib); the `#guard` tests in
`Examples.lean` run as part of the build.
