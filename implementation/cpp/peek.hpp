#pragma once

// Peek<Base>: the peeking reduction over any triage/stem/fork backend (cf.
// ReduceRecursive, which runs the five rules straight). It expands rule 2 (S) by
// peeking into x, so a dead apply(y,b) is never built (S+K elimination) and
// trivial applies fold into direct node builds. Effective rules, R := apply(y,b):
//
//   apply(leaf, b)               = stem(b)
//   apply(stem u, b)             = fork(u, b)
//   apply(fork(leaf, y), b)      = y                                          // rule 1
//   apply(fork(fork(w,x), y), b) = w | apply(x,d) | apply(apply(y,d),e)       // rule 3, b = leaf | stem d | fork d e
//   apply(fork(stem x, y), b), peeking x =                                    // rule 2 (S)
//     leaf                -> fork(b, R)
//     stem(leaf)          -> b                                                // R dead
//     stem(stem x2)       -> apply(apply(x2,R), apply(b,R))
//     stem(fork w x2)     -> w | apply(x2,d) | apply(apply(b,d),e)            // R = leaf | stem d | fork d e
//     fork(leaf, leaf)    -> stem(R)
//     fork(leaf, stem x3) -> fork(x3, R)
//     fork(leaf, fork ..) -> apply(x2, R)
//     fork(_, _)          -> apply(apply(x,b), R)                             // generic
//
// PEEK_INLINE forces the triage lambdas to inline; at this depth the biggest are
// otherwise left out of line, spilling the reduction state to a stack closure on
// every step (~50% slower). Inlined, apply() is one spill-free self-recursive
// function matching a hand-written switch.
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
