#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <string>

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
//     exhaustion in the mmap variants, this is not detected.

class EagerTernaryNil32 {
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

  template <typename T>
  T triage(std::function<T()> leaf_case,
           std::function<T(Tree)> stem_case,
           std::function<T(Tree, Tree)> fork_case,
           Tree x)
  {
    Tree c1 = _buf[x];
    Tree c2 = _buf[x + 1];
    if (c1 == 0) return leaf_case();
    if (c2 == 0) return stem_case(c1);
    return fork_case(c1, c2);
  }

  Tree apply(Tree a, Tree b) {
    Tree u = _buf[a];
    Tree y = _buf[a + 1];

    // apply(leaf, b) = stem(b)
    if (u == 0) return stem(b);

    // apply(stem(u), b) = fork(u, b)
    if (y == 0) return fork(u, b);

    // a = fork(u, y)
    Tree w = _buf[u];
    Tree x = _buf[u + 1];

    // apply(fork(leaf, y), b) = y
    if (w == 0) return y;

    // apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
    if (x == 0) return apply(apply(w, b), apply(y, b));

    // apply(fork(fork(w, x), y), b) — triage on b
    Tree d = _buf[b];
    Tree e = _buf[b + 1];
    if (d == 0) return w;                     // b = leaf
    if (e == 0) return apply(x, d);           // b = stem(d)
    return apply(apply(y, d), e);             // b = fork(d, e)
  }
};
