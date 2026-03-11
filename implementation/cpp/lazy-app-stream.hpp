#pragma once

#include <ranges>
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
      if (t[pos]) depth--;  // leaf: closes one subtree
      else depth++;         // apply: consumes one, opens two = net +1
      pos++;
    }
    return pos;
  }

  // Copy a subtree from src[pos..] into dst, returning the position after it.
  static size_t copy_subtree(const Tree &src, size_t pos, Tree &dst) {
    size_t end = subtree_end(src, pos);
    dst.append_range(std::ranges::subrange(src.begin() + pos, src.begin() + end));
    return end;
  }

  // Streaming reducer: read src starting at pos, emit one reduced subtree
  // into dst. Returns the position in src after the consumed subtree.
  // 'changed' is set to true if any reduction was performed.
  static size_t stream_reduce(const Tree &src, size_t pos, Tree &dst, bool &changed) {
    if (pos >= src.size()) throw std::runtime_error("stream_reduce: out of bounds");

    // Leaf
    if (src[pos]) {
      dst.push_back(true);
      return pos + 1;
    }

    // Application: 0 A B
    // Recursively reduce A first, then see if we have a redex.
    size_t a_start = pos + 1;

    // Reduce A into a temporary buffer so we can inspect its head form.
    Tree a_buf;
    size_t after_a = stream_reduce(src, a_start, a_buf, changed);

    // Check if reduced A is itself an application (starts with 0) —
    // i.e. A = apply(F, G), making the whole thing apply(apply(F, G), B).
    // If F is also apply(leaf, Y), we have a full redex: apply(apply(apply(leaf, Y), Z), W)
    // where G=Z, B=W.

    if (a_buf.size() >= 1 && !a_buf[0]) {
      // A = 0 F G — parse F from a_buf
      size_t f_end = subtree_end(a_buf, 1);
      // Check if F = 0 leaf Y, i.e. F starts with '0' and then '1' (leaf)
      if (!a_buf[1] && a_buf.size() >= 3 && a_buf[2]) {
        // F = apply(leaf, Y) = 0 1 Y
        // So A = apply(apply(leaf, Y), G) where G starts at f_end
        // Full expression: apply(A, B) = apply(apply(apply(leaf, Y), G), B)
        // This is a redex! Y starts at a_buf[3], G starts at f_end.
        size_t y_start = 3;
        size_t y_end = f_end;
        size_t g_start = f_end;
        // g goes to end of a_buf

        // Now we need B from src
        // But first, extract Y and G from a_buf
        // Then reduce B from src

        // The reduction depends on the head form of Y:
        // Y = leaf:         result = G                        (rule: leaf case of triage)
        // Y = stem(Y'):     result = apply(apply(Y', W), apply(G, W))  where W=B
        // Y = fork(P, Q):   triage on W=B:
        //   W = leaf:       result = P
        //   W = stem(D):    result = apply(Q, D)
        //   W = fork(D,E):  result = apply(apply(G, D), E)

        // First, reduce Y to head form
        Tree y_reduced(a_buf.begin() + y_start, a_buf.begin() + y_end);
        {
          // Recursively fully reduce Y by streaming it
          bool y_changed = true;
          while (y_changed) {
            y_changed = false;
            Tree y_out;
            stream_reduce(y_reduced, 0, y_out, y_changed);
            y_reduced = std::move(y_out);
          }
        }

        Tree g(a_buf.begin() + g_start, a_buf.end());

        if (y_reduced.size() >= 1 && y_reduced[0]) {
          // Y = leaf: fork(leaf, G) W -> G
          // Skip consuming B (W) from src — we still need to consume it
          size_t after_b = subtree_end(src, after_a);
          // Emit G, but also recurse-reduce it
          changed = true;
          // G might create new redexes when combined with tail, so
          // just emit it and let the outer loop catch it
          dst.append_range(g);
          // We consumed B but didn't use it — still need to advance past it
          // Actually wait: the rule is fork(leaf, Z) W -> Z, so we DROP W.
          return after_b;
        }

        if (y_reduced.size() >= 2 && !y_reduced[0] && y_reduced[1]) {
          // Y = stem(Y'): fork(stem(Y'), G) W -> apply(apply(Y', W), apply(G, W))
          Tree yp(y_reduced.begin() + 2, y_reduced.end());

          // We need W = B from src
          Tree w;
          size_t after_b = stream_reduce(src, after_a, w, changed);

          changed = true;
          // Emit: 0 0 Y' W 0 G W
          // = apply(apply(Y', W), apply(G, W))
          dst.push_back(false);
          dst.push_back(false);
          dst.append_range(yp);
          dst.append_range(w);
          dst.push_back(false);
          dst.append_range(g);
          dst.append_range(w);
          return after_b;
        }

        if (y_reduced.size() >= 3 && !y_reduced[0] && !y_reduced[1] && y_reduced[2]) {
          // Y = fork(P, Q): triage on W
          size_t p_end = subtree_end(y_reduced, 3);
          Tree p(y_reduced.begin() + 3, y_reduced.begin() + p_end);
          Tree q(y_reduced.begin() + p_end, y_reduced.end());

          // We need W = B from src, but we need to reduce it to inspect its head
          Tree w;
          size_t after_b = stream_reduce(src, after_a, w, changed);

          // Fully reduce W to head form for triage
          Tree w_reduced = w;
          {
            bool w_changed = true;
            while (w_changed) {
              w_changed = false;
              Tree w_out;
              stream_reduce(w_reduced, 0, w_out, w_changed);
              w_reduced = std::move(w_out);
            }
          }

          changed = true;

          if (w_reduced.size() >= 1 && w_reduced[0]) {
            // W = leaf: -> P
            dst.append_range(p);
            return after_b;
          }

          if (w_reduced.size() >= 2 && !w_reduced[0] && w_reduced[1]) {
            // W = stem(D): -> apply(Q, D)
            Tree d(w_reduced.begin() + 2, w_reduced.end());
            dst.push_back(false);
            dst.append_range(q);
            dst.append_range(d);
            return after_b;
          }

          if (w_reduced.size() >= 3 && !w_reduced[0] && !w_reduced[1] && w_reduced[2]) {
            // W = fork(D, E): -> apply(apply(G, D), E)
            size_t d_end = subtree_end(w_reduced, 3);
            Tree d(w_reduced.begin() + 3, w_reduced.begin() + d_end);
            Tree e(w_reduced.begin() + d_end, w_reduced.end());
            dst.push_back(false);
            dst.push_back(false);
            dst.append_range(g);
            dst.append_range(d);
            dst.append_range(e);
            return after_b;
          }

          throw std::runtime_error("triage: W not in head form after reduction");
        }

        // Y is not in head form (starts with 000) — shouldn't happen after
        // full reduction, but fall through to non-redex path.
      }
    }

    // Not a redex (or not a matching pattern): emit apply(A_reduced, B_reduced)
    dst.push_back(false);
    dst.append_range(a_buf);
    return stream_reduce(src, after_a, dst, changed);
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
