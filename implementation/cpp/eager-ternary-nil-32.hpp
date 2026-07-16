#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include "reduce-recursive.hpp"

// Eager evaluator like EagerTernaryNil (flat buffer, pointer sharing,
// constant-size tagless nodes, arity discriminated by null children), but with
// 32-bit slots instead of size_t: a whole node packs into 64 bits.
//
//   <0> <0>      — leaf
//   <child> <0>  — stem
//   <a> <b>      — fork  (both non-null)
//
// Position 0 is reserved (0 is the null child sentinel); the shared leaf
// lives at position 1.
//
// Consequences (relative to EagerTernaryNil):
//   — Nodes shrink from 16 to 8 bytes: half the memory traffic per node and
//     twice as many nodes per cache line, which is where the speedup comes
//     from once the working set outgrows the caches. Measured on recursive
//     fib: ~1.45-1.5x once the buffer is hundreds of MB, ~1.1x while it
//     still fits in L3, ~parity on tiny workloads.
//   — Positions are uint32_t, so the buffer is capped at 2^32 slots
//     (16 GiB). Growing past that silently wraps positions; like arena
//     exhaustion in the mmap variants, this is not detected. Second-slot
//     reads widen to size_t before the +1, so a node at the last
//     representable position still decodes correctly instead of wrapping
//     into slot 0.

class EagerTernaryNil32 : public ReduceRecursive<EagerTernaryNil32> {
private:
  std::vector<uint32_t> _buf;

public:
  using Tree = uint32_t;

  EagerTernaryNil32() {
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
    Tree result = static_cast<Tree>(_buf.size());
    _buf.push_back(u);
    _buf.push_back(0);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    Tree result = static_cast<Tree>(_buf.size());
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
    Tree c2 = _buf[size_t(x) + 1];
    if (c1 == 0) return leaf_case();
    if (c2 == 0) return stem_case(c1);
    return fork_case(c1, c2);
  }

  // apply() is inherited from ReduceRecursive<EagerTernaryNil32>.
};
