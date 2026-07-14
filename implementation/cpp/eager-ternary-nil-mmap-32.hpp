#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>
#include <sys/mman.h>

// Eager evaluator like EagerTernaryNilMmap (constant-size tagless nodes in an
// mmap'd arena, arity discriminated by null children), but nodes are
// addressed by 32-bit indices into the arena instead of raw pointers: a whole
// node packs into 64 bits.
//
//   {0, 0}      — leaf
//   {child, 0}  — stem
//   {a, b}      — fork  (both non-null)
//
// Since 0 serves as the null sentinel, no node can live at index 0; slot 0 is
// reserved padding and the shared leaf lives at index 1 (mirroring the
// vector-backed EagerTernaryNil32).
//
// Differences to the pointer-based mmap variant:
//   — Nodes shrink from 16 to 8 bytes: half the memory traffic per node and
//     twice as many nodes per cache line. Measured on recursive fib:
//     ~1.2-1.3x, fairly uniform from L3-resident up to multi-GB arenas.
//   — Every access re-bases off the arena pointer (`_arena[x]`), but on
//     x86-64 and AArch64 the base+index*8 addressing is a single load, so
//     the indirection costs nothing over a raw pointer dereference.
//   — 32-bit indices address 2^32 nodes = 32 GiB of arena; the reservation
//     below is exactly that. Exhausting it is not detected and will wrap.

class EagerTernaryNilMmap32 {
public:
  struct Node {
    uint32_t u;
    uint32_t v;
  };
  using Tree = uint32_t;

private:
  static constexpr size_t ARENA_BYTES = size_t(1) << 35; // 2^32 nodes of 8 bytes

  Node *_arena;
  uint32_t _head;

public:
  EagerTernaryNilMmap32() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void *mem = mmap(nullptr, ARENA_BYTES, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mem == MAP_FAILED)
      throw std::runtime_error("mmap failed to reserve arena");
    _arena = static_cast<Node *>(mem);
    _arena[0] = {0, 0}; // index 0 reserved: 0 is the null child sentinel
    _arena[1] = {0, 0}; // pre-populate the shared leaf at index 1
    _head = 2;
  }

  ~EagerTernaryNilMmap32() {
    munmap(_arena, ARENA_BYTES);
  }

  EagerTernaryNilMmap32(const EagerTernaryNilMmap32 &) = delete;
  EagerTernaryNilMmap32 &operator=(const EagerTernaryNilMmap32 &) = delete;

  std::string stats() {
    return std::to_string(_head) + " nodes in arena";
  }

  Tree leaf() {
    return 1;
  }

  Tree stem(Tree u) {
    Tree result = _head++;
    _arena[result] = {u, 0};
    return result;
  }

  Tree fork(Tree u, Tree v) {
    Tree result = _head++;
    _arena[result] = {u, v};
    return result;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case,
           std::function<T(Tree)> stem_case,
           std::function<T(Tree, Tree)> fork_case,
           Tree x)
  {
    Node n = _arena[x];
    if (!n.u) return leaf_case();
    if (!n.v) return stem_case(n.u);
    return fork_case(n.u, n.v);
  }

  Tree apply(Tree a, Tree b) {
    Tree u = _arena[a].u;
    Tree y = _arena[a].v;

    // apply(leaf, b) = stem(b)
    if (!u) return stem(b);

    // apply(stem(u), b) = fork(u, b)
    if (!y) return fork(u, b);

    // a = fork(u, y)
    Tree w = _arena[u].u;
    Tree x = _arena[u].v;

    // apply(fork(leaf, y), b) = y
    if (!w) return y;

    // apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
    if (!x) return apply(apply(w, b), apply(y, b));

    // apply(fork(fork(w, x), y), b) — triage on b
    Tree d = _arena[b].u;
    Tree e = _arena[b].v;
    if (!d) return w;                         // b = leaf
    if (!e) return apply(x, d);               // b = stem(d)
    return apply(apply(y, d), e);             // b = fork(d, e)
  }
};
