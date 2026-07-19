#pragma once

// Peek<Base>: apply() for the triage-calculus reduction, over any triage/stem/fork
// backend. @ = application (left-associative); △, △ u, △ u v = leaf, stem, fork.
//
//   △ @ b                     = △ b                                    5.9%
//   △ u @ b                   = △ u b                                 15.4%
//   △ △ y @ b                 = y                                     13.4%
//   △ (△ △) y @ b             = △ b (y @ b)                            0.0%
//   △ (△ (△ △)) y @ b         = b                                      2.6%
//   △ (△ (△ (△ x))) y @ b     = (x @ (y @ b)) @ (b @ (y @ b))          0.0%
//   △ (△ (△ (△ w x))) y @ b   = ((△ (△ w x)) @ b) @ (y @ b)            0.0%
//   △ (△ (△ △ △)) y @ b       = △ (y @ b)                              6.3%
//   △ (△ (△ △ (△ x))) y @ b   = △ x (y @ b)                           17.8%
//   △ (△ (△ △ x)) y @ b       = x @ (y @ b)               (x a fork)  17.8%
//   △ (△ (△ u v)) y @ b       = ((△ u v) @ b) @ (y @ b)   (u ≠ △)     17.4%
//   △ (△ w x) y @ △           = w                                      0.2%
//   △ (△ w x) y @ △ d         = x @ d                                  0.2%
//   △ (△ w x) y @ △ d e       = (y @ d) @ e                            3.0%
//
// The trailing % on each rule is how often it fires, as a share of all reductions
// over a large benchmark (~1.6M reductions from a broad program corpus). Rule 2
// (the S regime, rows 4-11) is ~62% of them, and two thirds of that is S on a
// K-absorber -- the △ △ x / 2a rows (17.8 + 17.8 + 6.3%) -- which is why peeking
// pays off in the first place.
//
// The triages tagged below are optional -- they peek past what the rules require,
// since rule 2's reduct is (x @ b) @ (y @ b) whatever x turns out to be. Each tag
// is what dropping that peek costs, wall-clock leave-one-out, as fib+ms / bench:
// the first is the original tuning on fib + merge-sort; the second is how much the
// large benchmark above slows down without that peek (its own leave-one-out).
// They nest, so the numbers don't sum -- dropping an outer peek subsumes inner.
//
// The two disagree sharply: on the broad corpus the 2a peek (xw) and its x2 fold
// dominate, well above the fib+ms estimate. Per-branch wall time is workload- and
// representation-dependent (fib + nil-mmap-32 -> xw; value-mem + list code -> 2c),
// so read any single number as a regime, not a constant. Peek buys 7-37% overall.
//
// PEEK_INLINE forces a triage lambda to inline; without it the larger ones go out
// of line, spilling reduction state to a stack closure per step.
#define PEEK_INLINE __attribute__((always_inline))

template <typename Base>
class Peek : public Base {
public:
  using Tree = typename Base::Tree;

  Tree apply(Tree a, Tree b) {
    return this->triage(
      [&]() PEEK_INLINE { return this->stem(b); },
      [&](Tree u) PEEK_INLINE { return this->fork(u, b); },
      [&](Tree u, Tree y) PEEK_INLINE {
        return this->triage(
          [&]() PEEK_INLINE { return y; },
          [&](Tree x) PEEK_INLINE {
            return this->triage(                                              // peek x: 31% / +51%   (whole rule-2 peek; == plain when off)
              [&]() PEEK_INLINE { return this->fork(b, this->apply(y, b)); },
              [&](Tree x1) PEEK_INLINE {
                return this->triage(                                          // peek x1: 7% / +14%   (2c: x = stem(leaf) -> b)
                  [&]() PEEK_INLINE { return b; },
                  [&](Tree x2) PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->apply(this->apply(x2, R), this->apply(b, R));
                  },
                  [&](Tree, Tree) PEEK_INLINE { return this->apply(this->apply(x, b), this->apply(y, b)); },
                  x1);
              },
              [&](Tree xw, Tree x2) PEEK_INLINE {
                return this->triage(                                          // peek xw: 16% / +51%  (2a K-absorber -- the production win)
                  [&]() PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->triage(                                      // peek x2: 0% / +34%   (folds the follow-up @ under 2a)
                      [&]() PEEK_INLINE { return this->stem(R); },
                      [&](Tree x3) PEEK_INLINE { return this->fork(x3, R); },
                      [&](Tree, Tree) PEEK_INLINE { return this->apply(x2, R); },
                      x2);
                  },
                  [&](Tree) PEEK_INLINE { return this->apply(this->apply(x, b), this->apply(y, b)); },
                  [&](Tree, Tree) PEEK_INLINE { return this->apply(this->apply(x, b), this->apply(y, b)); },
                  xw);
              },
              x);
          },
          [&](Tree w, Tree x) PEEK_INLINE {                                  // rule 3
            return this->triage(
              [&]() PEEK_INLINE { return w; },
              [&](Tree d) PEEK_INLINE { return this->apply(x, d); },
              [&](Tree d, Tree e) PEEK_INLINE { return this->apply(this->apply(y, d), e); },
              b);
          },
          u);
      },
      a);
  }
};

#undef PEEK_INLINE
