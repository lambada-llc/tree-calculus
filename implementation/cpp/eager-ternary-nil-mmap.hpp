#pragma once

#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include "reduce-recursive.hpp"

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

class EagerTernaryNilMmap : public ReduceRecursive<EagerTernaryNilMmap> {
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

  // Callables are template parameters (not std::function) so the shared
  // ReduceRecursive::apply inlines its lambdas straight through this dispatch.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    if (!x->u) return leaf_case();
    if (!x->v) return stem_case(x->u);
    return fork_case(x->u, x->v);
  }

  // apply() is inherited from ReduceRecursive<EagerTernaryNilMmap>.
};
