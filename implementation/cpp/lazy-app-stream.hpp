#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

// A tree is represented as a boolean vector (bitstream) as per
// "minimalist binary encoding" (see ../../conventions/#minimalist-binary):
// '1' = node (leaf), '0' = application.
// E.g. leaf = {1}, apply(A,B) = {0} + A + B.
// Ternary "0" (leaf) maps to {1}.
// Ternary "1x" (stem x) maps to {0,1} + bits(x).
// Ternary "2xy" (fork x y) maps to {0,0,1} + bits(x) + bits(y).
//
// Reduction is done by "streaming": we read the input bitstream left-to-right
// and append to an output bitstream (push_back only, no splicing). When a
// reducible redex is encountered, the replacement is emitted directly.
// The reducer recurses into emitted subtrees, so multiple reductions can
// happen in a single pass. We repeat passes until a fixpoint is reached.

class LazyAppStream {
public:
  using Tree = std::vector<bool>;

private:
  // Find the end of a subtree starting at position pos.
  static size_t subtree_end(const Tree &t, size_t pos) {
    size_t depth = 1;
    while (depth > 0) {
      if (pos >= t.size()) throw std::runtime_error("subtree_end: out of bounds");
      if (t[pos]) depth--;
      else depth++;
      pos++;
    }
    return pos;
  }

  // Reduce a tree to normal form by repeated streaming passes.
  static void reduce(Tree &t) {
    bool changed = true;
    while (changed) {
      changed = false;
      Tree out;
      out.reserve(t.size());
      stream_reduce(t, 0, out, changed);
      t = std::move(out);
    }
  }

  // Consume one subtree from src[pos..], emit its reduced form into dst.
  // Returns position in src after the consumed subtree.
  static size_t stream_reduce(const Tree &src, size_t pos, Tree &dst, bool &changed) {
    if (pos >= src.size()) throw std::runtime_error("stream_reduce: out of bounds");
    if (src[pos]) { dst.push_back(true); return pos + 1; }

    // apply(A, B): reduce A, check for redex
    Tree a;
    size_t b_pos = stream_reduce(src, pos + 1, a, changed);

    // A starts with 001 = apply(apply(leaf, Y), G) = fork(Y, G) → redex
    if (a.size() >= 3 && !a[0] && !a[1] && a[2]) {
      size_t y_end = subtree_end(a, 3);
      return reduce_redex(src, b_pos,
        Tree(a.begin() + 3, a.begin() + y_end),
        Tree(a.begin() + y_end, a.end()),
        dst, changed);
    }

    // No redex: emit apply(A_reduced, B_reduced)
    dst.push_back(false);
    dst.append_range(a);
    return stream_reduce(src, b_pos, dst, changed);
  }

  // Reduce the redex apply(fork(Y, G), W) where W starts at src[w_pos..].
  // Emits result into dst. Returns position in src after W.
  static size_t reduce_redex(const Tree &src, size_t w_pos,
                              Tree y, Tree g, Tree &dst, bool &changed) {
    reduce(y);
    changed = true;

    // Y=leaf: fork(leaf, G) W → G (drop W)
    if (y[0]) {
      dst.append_range(g);
      return subtree_end(src, w_pos);
    }

    // Y=stem(Y'): fork(stem(Y'), G) W → apply(apply(Y', W), apply(G, W))
    if (y[1]) {
      Tree w;
      size_t after_w = stream_reduce(src, w_pos, w, changed);
      dst.push_back(false);
      dst.push_back(false);
      dst.append_range(Tree(y.begin() + 2, y.end()));
      dst.append_range(w);
      dst.push_back(false);
      dst.append_range(g);
      dst.append_range(w);
      return after_w;
    }

    // Y=fork(P, Q): triage on W
    size_t p_end = subtree_end(y, 3);
    Tree p(y.begin() + 3, y.begin() + p_end);
    Tree q(y.begin() + p_end, y.end());

    Tree w;
    size_t after_w = stream_reduce(src, w_pos, w, changed);
    reduce(w);

    // W=leaf: → P
    if (w[0]) {
      dst.append_range(p);
      return after_w;
    }

    // W=stem(D): → apply(Q, D)
    if (w[1]) {
      dst.push_back(false);
      dst.append_range(q);
      dst.append_range(Tree(w.begin() + 2, w.end()));
      return after_w;
    }

    // W=fork(D, E): → apply(apply(G, D), E)
    size_t d_end = subtree_end(w, 3);
    dst.push_back(false);
    dst.push_back(false);
    dst.append_range(g);
    dst.append_range(Tree(w.begin() + 3, w.begin() + d_end));
    dst.append_range(Tree(w.begin() + d_end, w.end()));
    return after_w;
  }

public:
  LazyAppStream() {}

  std::string stats() {
    return "no internal state";
  }

  Tree leaf() {
    return Tree{true};
  }

  Tree stem(Tree u) {
    Tree result = {false, true};
    result.append_range(u);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    Tree result = {false, false, true};
    result.append_range(u);
    result.append_range(v);
    return result;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case, std::function<T(Tree)> stem_case, std::function<T(Tree, Tree)> fork_case, Tree x) {
    reduce(x);
    if (x.size() == 1 && x[0]) {
      return leaf_case();
    }
    if (x.size() >= 2 && !x[0] && x[1]) {
      Tree u(x.begin() + 2, x.end());
      return stem_case(u);
    }
    if (x.size() >= 3 && !x[0] && !x[1] && x[2]) {
      size_t u_end = subtree_end(x, 3);
      Tree u(x.begin() + 3, x.begin() + u_end);
      Tree v(x.begin() + u_end, x.end());
      return fork_case(u, v);
    }
    throw std::runtime_error("triage: tree is not fully reduced");
  }

  Tree apply(Tree a, Tree b) {
    Tree result = {false};
    result.append_range(a);
    result.append_range(b);
    return result;
  }

};
