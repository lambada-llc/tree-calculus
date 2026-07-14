#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>
#include <sys/mman.h>

// Eager evaluator like EagerTernaryNilMmapVM (constant-size tagless nodes in
// an mmap'd arena, explicit VM-style evaluation loop), but nodes are
// addressed by 32-bit indices into the arena instead of raw pointers: a whole
// node packs into 64 bits.
//
// Tree representation is the same as EagerTernaryNilMmap32:
//   {0, 0}      — leaf
//   {child, 0}  — stem
//   {a, b}      — fork  (both non-null)
// Index 0 is reserved (0 is the null child sentinel); the shared leaf lives
// at index 1. 32-bit indices address 2^32 nodes = 32 GiB of arena;
// exhausting it is not detected and will wrap.
//
// The VM's continuation frames shrink along with the nodes (two uint32_t
// arguments instead of two pointers), halving stack traffic as well.
// Measured speedups match EagerTernaryNilMmap32's (~1.2-1.3x).

class EagerTernaryNilMmapVM32 {
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

  // Stack frame types for the VM.
  //
  //   APPLY_TO(arg):
  //     When the current computation produces result r,
  //     begin computing apply(r, arg).
  //
  //   COMPUTE_AND_APPLY(fn, arg):
  //     When the current computation produces result r,
  //     push APPLY_TO(r) and begin computing apply(fn, arg).
  //     This supports the pattern  apply(apply(fn, arg), r)
  //     used in the fork-stem reduction rule.
  enum FrameTag { APPLY_TO, COMPUTE_AND_APPLY };

  struct Frame {
    FrameTag tag;
    uint32_t arg1;
    uint32_t arg2 = 0; // only used for COMPUTE_AND_APPLY
  };

public:
  EagerTernaryNilMmapVM32() {
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

  ~EagerTernaryNilMmapVM32() {
    munmap(_arena, ARENA_BYTES);
  }

  EagerTernaryNilMmapVM32(const EagerTernaryNilMmapVM32 &) = delete;
  EagerTernaryNilMmapVM32 &operator=(const EagerTernaryNilMmapVM32 &) = delete;

  std::string stats() {
    return std::to_string(_head - 1) + " nodes in arena"; // index 0 is padding, not a node
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
    std::vector<Frame> stack;
    Tree result;

  reduce: // ---- evaluate apply(a, b) ----
    {
      Tree u = _arena[a].u;
      Tree y = _arena[a].v;

      if (!u) {                                          // apply(△, b) = △b
        result = stem(b);
        goto dispatch;
      }

      if (!y) {                                          // apply(△u, b) = △ub
        result = fork(u, b);
        goto dispatch;
      }

      Tree w = _arena[u].u;
      Tree x = _arena[u].v;

      if (!w) {                                          // apply(△△y, b) = y
        result = y;
        goto dispatch;
      }

      if (!x) {                                          // apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
        stack.push_back({COMPUTE_AND_APPLY, w, b});
        a = y;
        goto reduce;
      }

      // apply(△(△wx)y, b) — triage on b
      Tree d = _arena[b].u;
      Tree e = _arena[b].v;

      if (!d) {                                          //   b = △:     w
        result = w;
        goto dispatch;
      }
      if (!e) {                                          //   b = △d:    apply(x, d)
        a = x;
        b = d;
        goto reduce;
      }
      stack.push_back({APPLY_TO, e});                    //   b = △de:   apply(apply(y, d), e)
      a = y;
      b = d;
      goto reduce;
    }

  dispatch: // ---- dispatch result through stack ----
    if (stack.empty()) return result;
    Frame f = stack.back(); stack.pop_back();
    if (f.tag == APPLY_TO) {
      a = result;
      b = f.arg1;
    } else { // COMPUTE_AND_APPLY: apply(apply(fn, arg), result)
      stack.push_back({APPLY_TO, result});
      a = f.arg1;
      b = f.arg2;
    }
    goto reduce;
  }
};
