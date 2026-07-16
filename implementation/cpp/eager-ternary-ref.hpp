#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include "reduce-recursive.hpp"

// Eager evaluator using a flat buffer with pointer sharing.
//
// Every tree is a position in the buffer where a tag (0, 1, or 2) lives.
// The buffer uses a simple, regular encoding:
//
//   0            — leaf  (1 slot)
//   1  <child>   — stem  (2 slots: tag + position of child tree)
//   2  <a> <b>   — fork  (3 slots: tag + positions of two child trees)
//
// Each <child>/<a>/<b> is a buffer index pointing directly to a tag (0/1/2).
// This invariant is maintained by construction: leaf/stem/fork/apply always
// return positions of tags, and children are always such positions.
//
// Consequences:
//   — No pointer-to-pointer chains, no resolve loop needed.
//   — No variable-length inline trees, no skip function needed.
//   — Tag vs child position is always determined by structural context.

class EagerTernaryRef : public ReduceRecursive<EagerTernaryRef> {
private:
  std::vector<size_t> _buf;

public:
  using Tree = size_t;

  EagerTernaryRef() {
    _buf.push_back(0);  // pre-populate leaf at index 0
  }

  std::string stats() {
    return std::to_string(_buf.size()) + " entries in buffer";
  }

  Tree leaf() {
    return 0;
  }

  Tree stem(Tree u) {
    size_t result = _buf.size();
    _buf.push_back(1);
    _buf.push_back(u);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    size_t result = _buf.size();
    _buf.push_back(2);
    _buf.push_back(u);
    _buf.push_back(v);
    return result;
  }

  // The invariant check is kept but pushed into a cold, out-of-line helper: when
  // triage is inlined three deep into the shared ReduceRecursive::apply, an
  // inline throw (with its string building and exception edges) would pin the
  // hot path's registers to the stack. Out-of-lining it keeps the dispatch tiny.
  [[noreturn, gnu::noinline, gnu::cold]] void invariant_violation(Tree x) {
    throw std::runtime_error(
      "invariant violation: unexpected value " + std::to_string(_buf[x]) +
      " at index " + std::to_string(x));
  }

  // Callables are template parameters (not std::function) so the shared
  // ReduceRecursive::apply inlines its lambdas straight through this dispatch.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    switch (_buf[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(_buf[x + 1]);
      case 2: return fork_case(_buf[x + 1], _buf[x + 2]);
      default: invariant_violation(x);
    }
  }

  // apply() is inherited from ReduceRecursive<EagerTernaryRef>.
};
