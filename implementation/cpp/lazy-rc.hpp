#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>
#include <cstdint>

// Lazy evaluator with reference-counting garbage collection.
//
// Like EagerRC, nodes live in a flat pool with a free list. But apply()
// creates unevaluated "thunk" nodes (tag=3) instead of immediately reducing.
// force() evaluates a thunk to WHNF (Weak Head Normal Form), overwrites
// the thunk in place with its result (memoization), and returns the index.
//
// Node layout: [tag_rc, c0, c1, _pad]
//   tag 0 = leaf, 1 = stem, 2 = fork, 3 = thunk (c0=fn, c1=arg)
//
// Ownership: same as EagerRC. Thunks own references to their fn and arg.
// force() uses an explicit stack to handle arbitrarily deep thunk chains
// without risk of stack overflow.

class LazyRC {
private:
  static constexpr uint32_t TAG_MASK = 0x7;
  static constexpr uint32_t RC_INC   = 0x8;
  static constexpr uint32_t RC_SAT   = 0xFFFFFFF8;

  struct Node {
    uint32_t tag_rc;
    uint32_t c0;
    uint32_t c1;
    uint32_t next_free;
  };

  std::vector<Node> pool_;
  std::vector<uint32_t> dec_work_;
  std::vector<uint32_t> force_spine_;
  uint32_t free_head_;
  uint64_t alloc_count_;
  uint64_t force_count_;

  uint32_t alloc_node(uint32_t tag, uint32_t c0, uint32_t c1) {
    uint32_t idx;
    if (free_head_ != UINT32_MAX) {
      idx = free_head_;
      free_head_ = pool_[idx].next_free;
    } else {
      idx = pool_.size();
      pool_.push_back({});
    }
    pool_[idx].tag_rc = tag | RC_INC;
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
    dec_work_.push_back(idx);
    while (!dec_work_.empty()) {
      uint32_t i = dec_work_.back(); dec_work_.pop_back();
      auto &n = pool_[i];
      if ((n.tag_rc & RC_SAT) == RC_SAT) continue;
      n.tag_rc -= RC_INC;
      if ((n.tag_rc >> 3) == 0) {
        uint32_t tag = n.tag_rc & TAG_MASK;
        if (tag >= 1) dec_work_.push_back(n.c0);
        if (tag >= 2) dec_work_.push_back(n.c1);
        n.next_free = free_head_;
        free_head_ = i;
      }
    }
  }

  uint32_t tag_of(uint32_t idx) {
    return pool_[idx].tag_rc & TAG_MASK;
  }

  void overwrite_with(uint32_t target, uint32_t source) {
    uint32_t old_c0 = pool_[target].c0;
    uint32_t old_c1 = pool_[target].c1;
    uint32_t src_tag = tag_of(source);

    uint32_t saved_rc = pool_[target].tag_rc & ~TAG_MASK;
    pool_[target].tag_rc = saved_rc | src_tag;
    pool_[target].c0 = pool_[source].c0;
    pool_[target].c1 = pool_[source].c1;

    if (src_tag >= 1) inc(pool_[source].c0);
    if (src_tag >= 2) inc(pool_[source].c1);

    // Dec old children (was a thunk: tag >= 2, so both c0 and c1)
    dec(old_c0);
    dec(old_c1);
  }

  // Force a node to WHNF using an explicit stack.
  // Returns the (possibly same) index, now guaranteed to be a value (tag 0/1/2).
  uint32_t force(uint32_t idx) {
    if (tag_of(idx) != 3) return idx;
    force_count_++;

    // Stack of thunks that depend on the thunk below them being forced first.
    // When we encounter a thunk whose fn is also a thunk, we push the outer
    // thunk and descend to force the inner fn first.
    size_t spine_base = force_spine_.size();
    uint32_t cur = idx;

    while (true) {
      if (tag_of(cur) != 3) {
        // cur is now a value. Unwind the spine.
        if (force_spine_.size() == spine_base) return idx;
        cur = force_spine_.back(); force_spine_.pop_back();
        continue;
      }

      uint32_t fn = pool_[cur].c0;

      // If fn is itself a thunk, force it first
      if (tag_of(fn) == 3) {
        force_spine_.push_back(cur);
        cur = fn;
        continue;
      }

      uint32_t arg = pool_[cur].c1;

      // For fork(u, y) as fn, we may need u forced
      if (tag_of(fn) == 2) {
        uint32_t u = pool_[fn].c0;
        if (tag_of(u) == 3) {
          force_spine_.push_back(cur);
          cur = u;
          continue;
        }
        // For fork(fork(w,x), y) we need arg forced to determine the sub-rule
        if (tag_of(u) == 2) {
          if (tag_of(arg) == 3) {
            force_spine_.push_back(cur);
            cur = arg;
            continue;
          }
        }
      }

      // All prerequisites are forced. Reduce.
      uint32_t result = reduce_step(fn, arg);

      // Overwrite thunk in place with result (memoization)
      overwrite_with(cur, result);
      dec(result);

      // If result was another thunk (fork-stem rule), loop to keep forcing cur
      if (tag_of(cur) != 3) {
        if (force_spine_.size() == spine_base) return idx;
        cur = force_spine_.back(); force_spine_.pop_back();
      }
    }
  }

  // Single reduction step. fn must be in WHNF (tag 0/1/2).
  // For fork(fork(w,x),y) case, arg must also be in WHNF.
  // Returns an owned reference (may be a thunk for the fork-stem rule).
  uint32_t reduce_step(uint32_t fn, uint32_t arg) {
    uint32_t tag_fn = tag_of(fn);

    switch (tag_fn) {
      case 0: { // apply(leaf, arg) = stem(arg)
        inc(arg);
        return alloc_node(1, arg, 0);
      }

      case 1: { // apply(stem(u), arg) = fork(u, arg)
        uint32_t u = pool_[fn].c0;
        inc(u);
        inc(arg);
        return alloc_node(2, u, arg);
      }

      case 2: { // apply(fork(u, y), arg)
        uint32_t u = pool_[fn].c0;
        uint32_t y = pool_[fn].c1;
        uint32_t tag_u = tag_of(u);

        switch (tag_u) {
          case 0: { // apply(fork(leaf, y), arg) = y
            inc(y);
            return y;
          }

          case 1: { // apply(fork(stem(u'), y), arg) = apply(apply(u', arg), apply(y, arg))
            uint32_t u_inner = pool_[u].c0;
            inc(u_inner); inc(arg);
            uint32_t left_thunk = alloc_node(3, u_inner, arg);
            inc(y); inc(arg);
            uint32_t right_thunk = alloc_node(3, y, arg);
            uint32_t result = alloc_node(3, left_thunk, right_thunk);
            return result;
          }

          case 2: { // apply(fork(fork(w, x), y), arg) — arg is already forced
            uint32_t w = pool_[u].c0;
            uint32_t x = pool_[u].c1;
            uint32_t tag_arg = tag_of(arg);

            switch (tag_arg) {
              case 0: { // arg = leaf: return w
                inc(w);
                return w;
              }
              case 1: { // arg = stem(d): apply(x, d)
                uint32_t d = pool_[arg].c0;
                inc(x); inc(d);
                return alloc_node(3, x, d);
              }
              case 2: { // arg = fork(d, e): apply(apply(y, d), e)
                uint32_t d = pool_[arg].c0;
                uint32_t e = pool_[arg].c1;
                inc(y); inc(d);
                uint32_t inner_thunk = alloc_node(3, y, d);
                inc(e);
                return alloc_node(3, inner_thunk, e);
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

public:
  using Tree = uint32_t;

  LazyRC() : free_head_(UINT32_MAX), alloc_count_(0), force_count_(0) {
    pool_.push_back({RC_SAT, 0, 0, 0}); // index 0 = leaf, pinned
  }

  std::string stats() {
    return std::to_string(alloc_count_) + " allocs, " +
           std::to_string(force_count_) + " forces, " +
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
    x = force(x);
    uint32_t tag = tag_of(x);
    switch (tag) {
      case 0: return leaf_case();
      case 1: return stem_case(pool_[x].c0);
      case 2: return fork_case(pool_[x].c0, pool_[x].c1);
      default: __builtin_unreachable();
    }
  }

  Tree apply(Tree a, Tree b) {
    inc(a);
    inc(b);
    return alloc_node(3, a, b);
  }
};
