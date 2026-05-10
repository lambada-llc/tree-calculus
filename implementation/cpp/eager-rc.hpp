#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>
#include <cstdint>

// Eager evaluator with reference-counting garbage collection.
//
// Nodes are stored in a flat pool with a free list for O(1) reuse.
// Each node is 4 uint32_t: [tag_rc, child0, child1, _pad].
//   tag_rc: bits 0-1 = tag (0=leaf, 1=stem, 2=fork), bits 2-31 = refcount.
// Leaf is always at index 0 with a pinned (saturated) refcount.
//
// Ownership convention:
//   - leaf(), stem(u), fork(u, v) return an owned reference (rc already bumped).
//   - apply(a, b) borrows its arguments and returns an owned reference.
//   - Callers must dec() values they no longer need.

class EagerRC {
private:
  static constexpr uint32_t TAG_MASK = 0x3;
  static constexpr uint32_t RC_INC   = 0x4;
  static constexpr uint32_t RC_SAT   = 0xFFFFFFFC; // saturated — never freed

  struct Node {
    uint32_t tag_rc;
    uint32_t c0;
    uint32_t c1;
    uint32_t next_free; // used only when on free list
  };

  std::vector<Node> pool_;
  uint32_t free_head_;
  uint64_t alloc_count_;
  uint64_t free_count_;

  uint32_t alloc_node(uint32_t tag, uint32_t c0, uint32_t c1) {
    uint32_t idx;
    if (free_head_ != UINT32_MAX) {
      idx = free_head_;
      free_head_ = pool_[idx].next_free;
    } else {
      idx = pool_.size();
      pool_.push_back({});
    }
    pool_[idx].tag_rc = tag | RC_INC; // refcount = 1
    pool_[idx].c0 = c0;
    pool_[idx].c1 = c1;
    alloc_count_++;
    return idx;
  }

  void inc(uint32_t idx) {
    if ((pool_[idx].tag_rc & RC_SAT) == RC_SAT) return;
    pool_[idx].tag_rc += RC_INC;
  }

  void dec(uint32_t idx) {
    auto &n = pool_[idx];
    if ((n.tag_rc & RC_SAT) == RC_SAT) return;
    n.tag_rc -= RC_INC;
    if ((n.tag_rc >> 2) != 0) return;
    uint32_t tag = n.tag_rc & TAG_MASK;
    uint32_t c0 = n.c0, c1 = n.c1;
    n.next_free = free_head_;
    free_head_ = idx;
    free_count_++;
    if (tag >= 1) dec(c0);
    if (tag == 2) dec(c1);
  }

public:
  using Tree = uint32_t;

  EagerRC() : free_head_(UINT32_MAX), alloc_count_(0), free_count_(0) {
    pool_.push_back({RC_SAT, 0, 0, 0}); // index 0 = leaf, pinned
  }

  std::string stats() {
    return std::to_string(alloc_count_) + " allocs, " +
           std::to_string(free_count_) + " frees, " +
           std::to_string(pool_.size()) + " pool size";
  }

  Tree leaf() {
    return 0;
  }

  Tree stem(Tree u) {
    inc(u);
    return alloc_node(1, u, 0);
  }

  Tree fork(Tree u, Tree v) {
    inc(u);
    inc(v);
    return alloc_node(2, u, v);
  }

  template <typename T>
  T triage(std::function<T()> leaf_case,
           std::function<T(Tree)> stem_case,
           std::function<T(Tree, Tree)> fork_case,
           Tree x) {
    uint32_t tag = pool_[x].tag_rc & TAG_MASK;
    switch (tag) {
      case 0: return leaf_case();
      case 1: return stem_case(pool_[x].c0);
      case 2: return fork_case(pool_[x].c0, pool_[x].c1);
      default: __builtin_unreachable();
    }
  }

  Tree apply(Tree a, Tree b) {
    uint32_t tag_a = pool_[a].tag_rc & TAG_MASK;

    switch (tag_a) {
      case 0: // apply(leaf, b) = stem(b)
        return stem(b);

      case 1: { // apply(stem(u), b) = fork(u, b)
        Tree u = pool_[a].c0;
        return fork(u, b);
      }

      case 2: {
        Tree u = pool_[a].c0;
        Tree y = pool_[a].c1;
        uint32_t tag_u = pool_[u].tag_rc & TAG_MASK;

        switch (tag_u) {
          case 0: { // apply(fork(leaf, y), b) = y
            inc(y);
            return y;
          }

          case 1: { // apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
            Tree u_inner = pool_[u].c0;
            Tree left = apply(u_inner, b);
            Tree right = apply(y, b);
            Tree result = apply(left, right);
            dec(left);
            dec(right);
            return result;
          }

          case 2: { // apply(fork(fork(w, x), y), b) — triage on b
            Tree w = pool_[u].c0;
            Tree x = pool_[u].c1;
            uint32_t tag_b = pool_[b].tag_rc & TAG_MASK;

            switch (tag_b) {
              case 0: { // b = leaf: return w
                inc(w);
                return w;
              }
              case 1: { // b = stem(d): apply(x, d)
                Tree d = pool_[b].c0;
                return apply(x, d);
              }
              case 2: { // b = fork(d, e): apply(apply(y, d), e)
                Tree d = pool_[b].c0;
                Tree e = pool_[b].c1;
                Tree inner = apply(y, d);
                Tree result = apply(inner, e);
                dec(inner);
                return result;
              }
              default: __builtin_unreachable();
            }
          }
          default: __builtin_unreachable();
        }
      }
      default: __builtin_unreachable();
    }
  }
};
