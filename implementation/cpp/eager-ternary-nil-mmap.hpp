#pragma once

#include <functional>
#include <string>
#include <stdexcept>
#include <sys/mman.h>

// Eager evaluator like EagerTernaryNil (constant-size tagless nodes, arity
// discriminated by null children), but nodes live in an mmap'd arena and are
// addressed by raw pointers instead of indices into a std::vector.
//
//   {nullptr, nullptr}  — leaf
//   {child,   nullptr}  — stem
//   {a,       b}        — fork  (both non-null)
//
// Differences to the vector-backed variant:
//   — Allocation is a pure pointer bump: no capacity check, no reallocation,
//     no data-pointer indirection through the vector on every access.
//   — nullptr is the natural null sentinel, so no arena slot needs to be
//     reserved and node addresses never move.
//   — The arena is a large virtual reservation (pages are committed lazily
//     by the OS on first touch), so untouched capacity costs no memory.
//     Exhausting the reservation is not detected and will crash.

class EagerTernaryNilMmap {
public:
  struct Node;
  using Tree = Node *;
  struct Node {
    Tree u;
    Tree v;
  };

private:
  static constexpr size_t ARENA_BYTES = size_t(1) << 36; // 64 GiB of virtual address space

  Node *_arena;
  Node *_head;
  Node *_leaf;

public:
  EagerTernaryNilMmap() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void *mem = mmap(nullptr, ARENA_BYTES, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mem == MAP_FAILED)
      throw std::runtime_error("mmap failed to reserve arena");
    _arena = static_cast<Node *>(mem);
    _head = _arena;
    _leaf = _head++;
    *_leaf = {nullptr, nullptr}; // pre-populate the shared leaf
  }

  ~EagerTernaryNilMmap() {
    munmap(_arena, ARENA_BYTES);
  }

  EagerTernaryNilMmap(const EagerTernaryNilMmap &) = delete;
  EagerTernaryNilMmap &operator=(const EagerTernaryNilMmap &) = delete;

  std::string stats() {
    return std::to_string(_head - _arena) + " nodes in arena";
  }

  Tree leaf() {
    return _leaf;
  }

  Tree stem(Tree u) {
    Tree result = _head++;
    *result = {u, nullptr};
    return result;
  }

  Tree fork(Tree u, Tree v) {
    Tree result = _head++;
    *result = {u, v};
    return result;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case,
           std::function<T(Tree)> stem_case,
           std::function<T(Tree, Tree)> fork_case,
           Tree x)
  {
    if (!x->u) return leaf_case();
    if (!x->v) return stem_case(x->u);
    return fork_case(x->u, x->v);
  }

  Tree apply(Tree a, Tree b) {
    Tree u = a->u;
    Tree y = a->v;

    // apply(leaf, b) = stem(b)
    if (!u) return stem(b);

    // apply(stem(u), b) = fork(u, b)
    if (!y) return fork(u, b);

    // a = fork(u, y)
    Tree w = u->u;
    Tree x = u->v;

    // apply(fork(leaf, y), b) = y
    if (!w) return y;

    // apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
    if (!x) return apply(apply(w, b), apply(y, b));

    // apply(fork(fork(w, x), y), b) — triage on b
    Tree d = b->u;
    Tree e = b->v;
    if (!d) return w;                         // b = leaf
    if (!e) return apply(x, d);               // b = stem(d)
    return apply(apply(y, d), e);             // b = fork(d, e)
  }
};
