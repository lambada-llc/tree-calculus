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
  // Copy one subtree from src[pos..] into dst. Returns position after it.
  static size_t copy_tree(const Tree &src, size_t pos, Tree &dst) {
    size_t depth = 1;
    while (depth > 0) {
      bool bit = src[pos++];
      dst.push_back(bit);
      if (bit) depth--; else depth++;
    }
    return pos;
  }

  // Skip one subtree in src starting at pos. Returns position after it.
  static size_t skip_tree(const Tree &src, size_t pos) {
    size_t depth = 1;
    while (depth > 0) {
      if (src[pos++]) depth--; else depth++;
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
    if (src[pos]) { dst.push_back(true); return pos + 1; }

    // Check for redex: apply(fork(Y, G), W) = 0,0,0,1,Y...,G...,W...
    if (!src[pos+1] && !src[pos+2] && src[pos+3]) {
      Tree y;
      size_t g_pos = copy_tree(src, pos + 4, y);
      size_t w_pos = skip_tree(src, g_pos);
      reduce(y);
      changed = true;
      return reduce_redex(src, g_pos, w_pos, y, dst, changed);
    }

    // No redex: stream apply(A_reduced, B_reduced) directly into dst
    dst.push_back(false);
    pos = stream_reduce(src, pos + 1, dst, changed);
    return stream_reduce(src, pos, dst, changed);
  }

  // Handle redex: apply(fork(Y, G), W). Y is reduced. G and W are in src.
  static size_t reduce_redex(const Tree &src, size_t g_pos, size_t w_pos,
                              const Tree &y, Tree &dst, bool &changed) {
    // Y=leaf: → G
    if (y[0]) {
      copy_tree(src, g_pos, dst);
      return skip_tree(src, w_pos);
    }

    // Y=stem(Y'): → apply(apply(Y', W), apply(G, W))
    if (y[1]) {
      Tree w;
      size_t after_w = stream_reduce(src, w_pos, w, changed);
      dst.push_back(false);
      dst.push_back(false);
      copy_tree(y, 2, dst);
      dst.append_range(w);
      dst.push_back(false);
      copy_tree(src, g_pos, dst);
      dst.append_range(w);
      return after_w;
    }

    // Y=fork(P, Q): triage on W
    size_t q_pos = skip_tree(y, 3);
    Tree w;
    size_t after_w = stream_reduce(src, w_pos, w, changed);
    reduce(w);

    // W=leaf: → P
    if (w[0]) {
      copy_tree(y, 3, dst);
      return after_w;
    }

    // W=stem(D): → apply(Q, D)
    if (w[1]) {
      dst.push_back(false);
      copy_tree(y, q_pos, dst);
      copy_tree(w, 2, dst);
      return after_w;
    }

    // W=fork(D, E): → apply(apply(G, D), E)
    dst.push_back(false);
    dst.push_back(false);
    copy_tree(src, g_pos, dst);
    size_t e_pos = copy_tree(w, 3, dst);
    copy_tree(w, e_pos, dst);
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
    if (x[0]) return leaf_case();
    if (x[1]) {
      Tree u;
      copy_tree(x, 2, u);
      return stem_case(std::move(u));
    }
    Tree u, v;
    size_t v_pos = copy_tree(x, 3, u);
    copy_tree(x, v_pos, v);
    return fork_case(std::move(u), std::move(v));
  }

  Tree apply(Tree a, Tree b) {
    Tree result = {false};
    result.append_range(a);
    result.append_range(b);
    return result;
  }

};
