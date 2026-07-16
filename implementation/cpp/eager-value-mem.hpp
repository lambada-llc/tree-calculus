#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include "reduce-recursive.hpp"

class EagerValueMem : public ReduceRecursive<EagerValueMem> {
private:
  std::vector<int> _type; // 0 = leaf, 1 = stem, 2 = fork
  std::vector<int> _u;    // (left) child for stems or forks
  std::vector<int> _v;    // right child for forks

public:
  using Tree = int;

  EagerValueMem() {
    // put the one and only leaf node at index 0
    _type.push_back(0);
    _u.push_back(0);
    _v.push_back(0);
  }

  std::string stats() {
    return std::to_string(_type.size()) + " nodes allocated";
  }

  Tree leaf() {
    return 0;
  }

  Tree stem(Tree u) {
    _type.push_back(1);
    _u.push_back(u);
    _v.push_back(0);
    return _type.size() - 1;
  }

  Tree fork(Tree u, Tree v) {
    _type.push_back(2);
    _u.push_back(u);
    _v.push_back(v);
    return _type.size() - 1;
  }

  // The invariant check is kept but pushed into a cold, out-of-line helper: when
  // triage is inlined three deep into the shared ReduceRecursive::apply, an
  // inline throw (with its string building and exception edges) would pin the
  // hot path's registers to the stack. Out-of-lining it keeps the dispatch tiny.
  [[noreturn, gnu::noinline, gnu::cold]] void invariant_violation(Tree x) {
    throw std::runtime_error("invariant violation: type " + std::to_string(_type[x]) + " at index " + std::to_string(x) + " not 0, 1 or 2");
  }

  // Callables are template parameters (not std::function) so the shared
  // ReduceRecursive::apply inlines its lambdas straight through this dispatch.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case()) {
    switch (_type[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(_u[x]);
      case 2: return fork_case(_u[x], _v[x]);
      default: invariant_violation(x);
    }
  }

  // apply() is inherited from ReduceRecursive<EagerValueMem>.
};
