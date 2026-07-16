#pragma once

// Peek<Base> layers the "peeking" reduction on top of any representation Base
// that provides triage / stem / fork (every eager evaluator here does). It is
// the peeking counterpart of ReduceRecursive (reduce-recursive.hpp): where that
// mixin restates the five rules straight, Peek expands rule 2 (S) by peeking
// into x so that a provably dead apply(y, b) is never built (S + K elimination).
//
// Like ReduceRecursive it is written purely against triage -- no direct node
// access, so no backend needs extra accessors.
//
// PEEK_INLINE forces each triage callable to inline. triage itself is
// always_inline, but the lambdas handed to it are not, and this dispatch nests
// triage far deeper than the plain reduction's three levels: without the hint
// the largest inner lambdas exceed the inliner's size threshold and are left
// out of line. An out-of-line callable defeats the scheme -- triage can no
// longer collapse into apply, so a closure is materialised on the stack and the
// reduction state is spilled on every step (measured ~50% slower, ~14 spills per
// call). Forcing the lambdas inline collapses apply() back into one spill-free
// self-recursive function whose machine code matches a hand-written switch.
#define PEEK_INLINE __attribute__((always_inline))
//
// Peeking expansion of rule 2, apply(fork(stem x, y), b):
//   x = leaf              -> fork(b, apply(y, b))
//   x = stem(leaf)        -> b                       // apply(y, b) never built
//   x = stem(stem x2)     -> R; apply(apply(x2, R), apply(b, R))
//   x = stem(fork(w, x2)) -> R; triage R: leaf->w | stem d->apply(x2,d)
//                                                  | fork d e->apply(apply(b,d),e)
//   x = fork(leaf, x2)    -> R; peek x2 (x2 = K's body, applied to R):
//                             x2 = leaf     -> stem(R)      // apply(leaf, R) = △R
//                             x2 = stem x3  -> fork(x3, R)  // apply(△x3, R) = △x3 R
//                             x2 = fork ..  -> apply(x2, R)
//   x = fork(_, _)        -> apply(apply(x, b), apply(y, b))   // generic fallback
// where R := apply(y, b), built lazily.
//
// The fork(leaf, x2) case is K x2 (a constant function) and is by far the most
// common rule-2 shape in compiled programs; peeking x2 turns the trivial
// apply(x2, R) into a direct node build ~2/3 of the time (measured), ~18% fewer
// total applies and ~10% faster wall-clock on fib and merge-sort.
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
