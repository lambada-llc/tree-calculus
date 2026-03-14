#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

// Eager evaluator storing trees as contiguous ternary-encoded sequences in a
// single flat buffer.  A Tree is an index (pointer) into that buffer where its
// ternary encoding begins, e.g. buf[ptr..] = {2,0,0} for fork(leaf,leaf).
//
// The buffer is append-only and immutable: once written, entries are never
// modified.  apply() either returns a pointer to an existing subtree (for
// 2 of the 5 reduction rules) or appends a new encoding to the end.

class EagerTernary {
private:
  std::vector<int> _buf;

  // Skip one complete tree starting at pos, return position after it.
  size_t skip(size_t pos) const {
    size_t count = 1;
    while (count > 0) {
      int tag = _buf[pos++];
      if (tag == 0) count--;       // leaf: consumed one tree
      else if (tag == 2) count++;  // fork: two children, consumed tag = net +1
      // tag == 1 (stem): one child follows, count unchanged
    }
    return pos;
  }

  // Copy one complete tree from _buf[pos..] to end of _buf.
  // Returns source position after the copied tree.
  size_t copy(size_t pos) {
    size_t count = 1;
    while (count > 0) {
      int tag = _buf[pos++];
      _buf.push_back(tag);
      if (tag == 0) count--;
      else if (tag == 2) count++;
    }
    return pos;
  }

public:
  using Tree = size_t;

  EagerTernary() {
    _buf.push_back(0); // pre-populate leaf at index 0
  }

  std::string stats() {
    return std::to_string(_buf.size()) + " ints in buffer";
  }

  Tree leaf() {
    return 0; // reuse the pre-populated leaf
  }

  Tree stem(Tree u) {
    size_t result = _buf.size();
    _buf.push_back(1);
    copy(u);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    size_t result = _buf.size();
    _buf.push_back(2);
    copy(u);
    copy(v);
    return result;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case, std::function<T(Tree)> stem_case, std::function<T(Tree, Tree)> fork_case, Tree x) {
    switch (_buf[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(x + 1);
      case 2: {
        Tree u = x + 1;
        Tree v = skip(u);
        return fork_case(u, v);
      }
      default: throw std::runtime_error("invariant violation: tag " + std::to_string(_buf[x]) + " at index " + std::to_string(x));
    }
  }

  Tree apply(Tree a, Tree b) {
    switch (_buf[a]) {
      case 0: {
        // apply(leaf, b) = stem(b) — new tree
        size_t result = _buf.size();
        _buf.push_back(1);
        copy(b);
        return result;
      }
      case 1: {
        // apply(stem(u), b) = fork(u, b) — new tree
        size_t result = _buf.size();
        _buf.push_back(2);
        copy(a + 1);
        copy(b);
        return result;
      }
      case 2: {
        Tree u = a + 1;
        switch (_buf[u]) {
          case 0: {
            // apply(fork(leaf, y), b) = y — just return pointer to y
            return u + 1;
          }
          case 1: {
            // apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
            Tree u_inner = u + 1;
            Tree y = skip(u);
            return apply(apply(u_inner, b), apply(y, b));
          }
          case 2: {
            // apply(fork(fork(w, x), y), b) — triage on b
            Tree w = u + 1;
            Tree x = skip(w);
            Tree y = skip(x);
            switch (_buf[b]) {
              case 0: return w;                       // b = leaf:      return w — just return pointer
              case 1: return apply(x, b + 1);         // b = stem(d):   apply(x, d)
              case 2: {                               // b = fork(d,e): apply(apply(y, d), e)
                Tree d = b + 1;
                Tree e = skip(d);
                return apply(apply(y, d), e);
              }
              default: throw std::runtime_error("invariant violation: tag " + std::to_string(_buf[b]) + " at index " + std::to_string(b));
            }
          }
          default: throw std::runtime_error("invariant violation: tag " + std::to_string(_buf[u]) + " at index " + std::to_string(u));
        }
      }
      default: throw std::runtime_error("invariant violation: tag " + std::to_string(_buf[a]) + " at index " + std::to_string(a));
    }
  }

};
