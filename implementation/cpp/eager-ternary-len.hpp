#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include "reduce-recursive.hpp"

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

class EagerTernaryLen : public ReduceRecursive<EagerTernaryLen> {
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

  // Callables are template parameters (not std::function) so the shared
  // ReduceRecursive::apply inlines its lambdas straight through this dispatch.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
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

  // apply() is inherited from ReduceRecursive<EagerTernaryLen>.
};
