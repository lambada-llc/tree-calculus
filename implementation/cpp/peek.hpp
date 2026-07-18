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
// PEEK_INLINE forces the triage lambdas to inline; at this depth the biggest are
// otherwise left out of line, spilling reduction state to a stack closure every
// step (~50% slower). Inlined, apply() is one spill-free self-recursive function.
#define PEEK_INLINE __attribute__((always_inline))

template <typename Base>
class Peek : public Base {
public:
  using Tree = typename Base::Tree;

  Tree apply(Tree a, Tree b) {
    return this->triage(
      // a = leaf:    apply(△, b) = △b
      [&]() PEEK_INLINE { return this->stem(b); },
      // a = stem(u): apply(△u, b) = △ u b
      [&](Tree u) PEEK_INLINE { return this->fork(u, b); },
      // a = fork(u, y)
      [&](Tree u, Tree y) PEEK_INLINE {
        return this->triage(
          // u = leaf (rule 1): apply(△△y, b) = y
          [&]() PEEK_INLINE { return y; },
          // u = stem(x) (rule 2, "S"): peek into x
          [&](Tree x) PEEK_INLINE {
            return this->triage(
              // x = leaf: fork(b, apply(y, b))
              [&]() PEEK_INLINE { return this->fork(b, this->apply(y, b)); },
              // x = stem(x1)
              [&](Tree x1) PEEK_INLINE {
                return this->triage(
                  // x1 = leaf, i.e. x = stem(leaf): apply(y, b) is dead
                  [&]() PEEK_INLINE { return b; },
                  // x1 = stem(x2): apply(apply(x2, R), apply(b, R))
                  [&](Tree x2) PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->apply(this->apply(x2, R), this->apply(b, R));
                  },
                  // x1 = fork(w, x2): triage R
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
              // x = fork(xw, x2): peek xw (x itself is still in scope)
              [&](Tree xw, Tree x2) PEEK_INLINE {
                return this->triage(
                  // xw = leaf, i.e. x = fork(leaf, x2) = K x2: apply(x2, apply(y, b)).
                  // Peek x2 so a trivial apply(x2, R) builds its node directly.
                  [&]() PEEK_INLINE {
                    Tree R = this->apply(y, b);
                    return this->triage(
                      [&]() PEEK_INLINE { return this->stem(R); },       // x2=leaf: apply(leaf,R)=△R
                      [&](Tree x3) PEEK_INLINE { return this->fork(x3, R); }, // x2=stem: apply(△x3,R)=△x3 R
                      [&](Tree, Tree) PEEK_INLINE { return this->apply(x2, R); }, // x2=fork: recurse
                      x2);
                  },
                  // xw = stem: generic fallback apply(apply(x, b), apply(y, b))
                  [&](Tree) PEEK_INLINE { return this->apply(this->apply(x, b), this->apply(y, b)); },
                  // xw = fork: generic fallback
                  [&](Tree, Tree) PEEK_INLINE { return this->apply(this->apply(x, b), this->apply(y, b)); },
                  xw);
              },
              x);
          },
          // u = fork(w, x) (rule 3): dispatch on b
          [&](Tree w, Tree x) PEEK_INLINE {
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
