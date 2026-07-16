#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include "reduce-recursive.hpp"

// Eager evaluator storing trees as contiguous ternary-encoded sequences in a
// single flat buffer.  A Tree is an index (pointer) into that buffer where its
// ternary encoding begins, e.g. buf[ptr..] = {2,0,0} for fork(leaf,leaf).
//
// The buffer is append-only and immutable: once written, entries are never
// modified.  apply() either returns a pointer to an existing subtree (for
// 2 of the 5 reduction rules) or appends a new encoding to the end.

class EagerTernary : public ReduceRecursive<EagerTernary> {
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

  // The invariant check is kept but pushed into a cold, out-of-line helper: when
  // triage is inlined three deep into the shared ReduceRecursive::apply, an
  // inline throw (with its string building and exception edges) would pin the
  // hot path's registers to the stack. Out-of-lining it keeps the dispatch tiny.
  [[noreturn, gnu::noinline, gnu::cold]] void invariant_violation(Tree x) {
    throw std::runtime_error("invariant violation: tag " + std::to_string(_buf[x]) + " at index " + std::to_string(x));
  }

  // Callables are template parameters (not std::function) so the shared
  // ReduceRecursive::apply inlines its lambdas straight through this dispatch.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case()) {
    switch (_buf[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(x + 1);
      case 2: {
        Tree u = x + 1;
        Tree v = skip(u);
        return fork_case(u, v);
      }
      default: invariant_violation(x);
    }
  }

  // apply() is inherited from ReduceRecursive<EagerTernary>.
};
