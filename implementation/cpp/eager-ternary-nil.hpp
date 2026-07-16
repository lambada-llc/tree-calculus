#pragma once

#include <vector>
#include <string>
#include "reduce-recursive.hpp"

// Eager evaluator using a flat buffer with pointer sharing and constant-size,
// tagless nodes.
//
// Every node occupies exactly two slots holding child positions; the arity is
// discriminated by null (0) child pointers instead of a tag:
//
//   <0> <0>      — leaf
//   <child> <0>  — stem
//   <a> <b>      — fork  (both non-null)
//
// Since 0 serves as the null sentinel, no node can live at position 0; slot 0
// is reserved padding and the shared leaf lives at position 1.
//
// Consequences (relative to EagerTernaryRef):
//   — No tag slot: forks shrink from 3 slots to 2, stems stay at 2 slots,
//     but there is only one leaf ever, so per-node size is constant 2.
//   — No tag validity check: every two-slot pattern decodes to some arity.

class EagerTernaryNil : public ReduceRecursive<EagerTernaryNil> {
private:
  std::vector<size_t> _buf;

public:
  using Tree = size_t;

  EagerTernaryNil() {
    _buf.push_back(0);  // position 0 reserved: 0 is the null child sentinel
    _buf.push_back(0);  // pre-populate leaf at position 1
    _buf.push_back(0);
  }

  std::string stats() {
    return std::to_string(_buf.size()) + " entries in buffer";
  }

  Tree leaf() {
    return 1;
  }

  Tree stem(Tree u) {
    size_t result = _buf.size();
    _buf.push_back(u);
    _buf.push_back(0);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    size_t result = _buf.size();
    _buf.push_back(u);
    _buf.push_back(v);
    return result;
  }

  // Callables are template parameters (not std::function) so the shared
  // ReduceRecursive::apply inlines its lambdas straight through this dispatch.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    Tree c1 = _buf[x];
    Tree c2 = _buf[x + 1];
    if (c1 == 0) return leaf_case();
    if (c2 == 0) return stem_case(c1);
    return fork_case(c1, c2);
  }

  // apply() is inherited from ReduceRecursive<EagerTernaryNil>.
};
