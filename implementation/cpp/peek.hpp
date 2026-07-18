#pragma once

// Peek<Base>: apply() for the triage-calculus reduction, over any triage/stem/fork
// backend. @ = application (left-associative); △, △ u, △ u v = leaf, stem, fork.
//
//   △ @ b                     = △ b
//   △ u @ b                   = △ u b
//   △ △ y @ b                 = y
//   △ (△ △) y @ b             = △ b (y @ b)
//   △ (△ (△ △)) y @ b         = b
//   △ (△ (△ (△ x))) y @ b     = (x @ (y @ b)) @ (b @ (y @ b))
//   △ (△ (△ (△ w x))) y @ b   = w | x @ d | (b @ d) @ e   when y @ b = △ | △ d | △ d e
//   △ (△ (△ △ △)) y @ b       = △ (y @ b)
//   △ (△ (△ △ (△ x))) y @ b   = △ x (y @ b)
//   △ (△ (△ △ x)) y @ b       = x @ (y @ b)               (x a fork)
//   △ (△ (△ u v)) y @ b       = ((△ u v) @ b) @ (y @ b)   (u ≠ △)
//   △ (△ w x) y @ △           = w
//   △ (△ w x) y @ △ d         = x @ d
//   △ (△ w x) y @ △ d e       = (y @ d) @ e
//
// What each rule-2 peek buys: wall-clock cost of dropping it and letting that x
// fall back to the plain (x @ b) @ (y @ b) (leave-one-out, fib + merge-sort).
//
//   x = △              0%   never built by either program
//   x = △ △            6%
//   x = △ (△ x)        0%   never built by either program
//   x = △ (△ w x)      0%   never built by either program
//   x = △ △ x         16%   a third of all applies land here
//     refine by x tail  0%  the @ it saves (△ △ △, △ △ (△ x)) is already one step
//   all five off       30%  super-additive: each fallback feeds the next
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
            return this->triage(
              [&]() PEEK_INLINE { return this->fork(b, this->apply(y, b)); },
              [&](Tree x1) PEEK_INLINE {
                return this->triage(
                  [&]() PEEK_INLINE { return b; },
                  [&](Tree x2) PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->apply(this->apply(x2, R), this->apply(b, R));
                  },
                  [&](Tree w, Tree x2) PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->triage(
                      [&]() PEEK_INLINE { return w; },
                      [&](Tree d) PEEK_INLINE { return this->apply(x2, d); },
                      [&](Tree d, Tree e) PEEK_INLINE { return this->apply(this->apply(b, d), e); },
                      R);
                  },
                  x1);
              },
              [&](Tree xw, Tree x2) PEEK_INLINE {
                return this->triage(
                  [&]() PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->triage(
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
          [&](Tree w, Tree x) {                                              // rule 3
            return this->triage(
              [&]() { return w; },
              [&](Tree d) { return this->apply(x, d); },
              [&](Tree d, Tree e) { return this->apply(this->apply(y, d), e); },
              b);
          },
          u);
      },
      a);
  }
};

#undef PEEK_INLINE
