#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>
#include <sys/mman.h>

// Eager evaluator like EagerTernaryNilMmap (constant-size tagless nodes in an
// mmap'd arena, addressed by raw pointers, arity discriminated by null
// children), but with an explicit VM-style evaluation loop — apply() uses no
// recursion, managing its entire state via an explicit continuation stack.
//
// Tree representation is the same as EagerTernaryNilMmap:
//   {nullptr, nullptr}  — leaf
//   {child,   nullptr}  — stem
//   {a,       b}        — fork  (both non-null)

class EagerTernaryNilMmapVM {
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
    Tree arg1;
    Tree arg2 = nullptr; // only used for COMPUTE_AND_APPLY
  };

public:
  EagerTernaryNilMmapVM() {
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

  ~EagerTernaryNilMmapVM() {
    munmap(_arena, ARENA_BYTES);
  }

  EagerTernaryNilMmapVM(const EagerTernaryNilMmapVM &) = delete;
  EagerTernaryNilMmapVM &operator=(const EagerTernaryNilMmapVM &) = delete;

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
    std::vector<Frame> stack;
    Tree result;

  reduce: // ---- evaluate apply(a, b) ----
    {
      Tree u = a->u;
      Tree y = a->v;

      if (!u) {                                          // apply(△, b) = △b
        result = stem(b);
        goto dispatch;
      }

      if (!y) {                                          // apply(△u, b) = △ub
        result = fork(u, b);
        goto dispatch;
      }

      Tree w = u->u;
      Tree x = u->v;

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
      Tree d = b->u;
      Tree e = b->v;

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
