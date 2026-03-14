#pragma once

#include <vector>
#include <functional>
#include <string>
#include <cstring>
#include <stdexcept>

// Eager evaluator storing trees as contiguous length-prefixed sequences in a
// single flat buffer, with full copying (no pointer sharing).
//
// Like EagerTernaryBuf, but replaces the 0/1/2 tag with a length header:
//   _buf[pos] = total number of slots this subtree occupies (including header)
//     1           → leaf  (no children)
//     n where child at pos+1 has size n-1  → stem (one child fills the rest)
//     n otherwise  → fork (two children packed after header)
//
// This makes skip() O(1):  skip(pos) = pos + _buf[pos]

class EagerTernaryLen {
private:
  std::vector<int> _buf;

  // Copy one complete tree from _buf[pos..] to end of _buf.
  // Returns source position after the copied tree.
  size_t copy(size_t pos) {
    int len = _buf[pos];
    size_t dst = _buf.size();
    _buf.resize(dst + len);
    std::memcpy(&_buf[dst], &_buf[pos], len * sizeof(int));
    return pos + len;
  }

public:
  using Tree = size_t;

  EagerTernaryLen() {
    _buf.push_back(1); // pre-populate leaf at index 0
  }

  std::string stats() {
    return std::to_string(_buf.size()) + " ints in buffer";
  }

  Tree leaf() {
    return 0; // reuse the pre-populated leaf
  }

  Tree stem(Tree u) {
    size_t result = _buf.size();
    _buf.push_back(0);   // placeholder
    copy(u);
    _buf[result] = (int)(_buf.size() - result);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    size_t result = _buf.size();
    _buf.push_back(0);   // placeholder
    copy(u);
    copy(v);
    _buf[result] = (int)(_buf.size() - result);
    return result;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case,
           std::function<T(Tree)> stem_case,
           std::function<T(Tree, Tree)> fork_case,
           Tree x)
  {
    int len = _buf[x];
    if (len == 1) return leaf_case();
    size_t c1 = x + 1;
    int c1sz = _buf[c1];
    if (c1sz == len - 1) {
      return stem_case(c1);           // child fills the rest → stem
    } else {
      return fork_case(c1, c1 + c1sz); // two children → fork
    }
  }

  Tree apply(Tree a, Tree b) {
    int len_a = _buf[a];

    if (len_a == 1) {
      // apply(leaf, b) = stem(b)
      size_t result = _buf.size();
      _buf.push_back(0);
      copy(b);
      _buf[result] = (int)(_buf.size() - result);
      return result;
    }

    size_t u = a + 1;
    int usz = _buf[u];

    if (usz == len_a - 1) {
      // a = stem(u):  apply(stem(u), b) = fork(u, b)
      size_t result = _buf.size();
      _buf.push_back(0);
      copy(u);
      copy(b);
      _buf[result] = (int)(_buf.size() - result);
      return result;
    }

    // a = fork(u, v)
    size_t v = u + usz;
    int len_u = _buf[u];

    if (len_u == 1) {
      // apply(fork(leaf, y), b) = y — just return pointer to y
      return v;
    }

    size_t uc1 = u + 1;
    int uc1sz = _buf[uc1];

    if (uc1sz == len_u - 1) {
      // u = stem(u'):  apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
      return apply(apply(uc1, b), apply(v, b));
    }

    // u = fork(w, x):  apply(fork(fork(w, x), y), b) — triage on b
    size_t w = uc1;
    size_t x = uc1 + uc1sz;

    int len_b = _buf[b];

    if (len_b == 1) return w;           // b = leaf: return w

    size_t bc1 = b + 1;
    int bc1sz = _buf[bc1];

    if (bc1sz == len_b - 1) {
      // b = stem(d):  apply(x, d)
      return apply(x, bc1);
    }

    // b = fork(d, e):  apply(apply(y, d), e)
    size_t e = bc1 + bc1sz;
    return apply(apply(v, bc1), e);
  }
};
