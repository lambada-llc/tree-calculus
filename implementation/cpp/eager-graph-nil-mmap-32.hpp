#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>
#include <sys/mman.h>

// Eager *graph* reduction over the nil-packed 32-bit mmap representation: the
// evaluator an eager module build needs, which none of the eager evaluators
// beside it is.
//
//   {0, 0}  — leaf   △
//   {u, 0}  — stem   △u        (u != 0)
//   {u, v}  — fork   △uv       (u, v != 0)
//
// The representation is EagerTernaryNilMmap32's, unchanged: 8-byte nodes in an
// mmap'd arena, arity read off the null children. apply(a, b) returns a normal
// form, so there is no fourth "not yet reduced" node shape the way
// LazyGraphNilMmap32 has one, and no node is ever rewritten after it is built.
//
// Everything around that is different, because the other eager evaluators here
// are benchmark implementations: they run one program to completion in a process
// that then exits. A module is thousands of bindings reduced back to back in one
// process, and that breaks them three separate ways.
//
//   — Depth. ReduceRecursive and Peek recurse in C once per nested application,
//     so reduction depth is stack depth and a deep enough term is a SIGSEGV —
//     with "deep enough" set by a thread stack size rather than by anything
//     about the program. Here the continuations are Frames in a heap vector
//     (the EagerTernaryNilMmapVM32 loop, which the rules below are otherwise a
//     copy of), so depth costs what any other allocation costs.
//
//   — Garbage. Eager reduction allocates its intermediates and walks away from
//     them: normalizing one binding routinely allocates orders of magnitude
//     more than the normal form it arrives at. An arena that never frees turns
//     that into the process's peak RSS. So: mark-and-sweep, driven from inside
//     the reduction loop rather than between requests, exactly as
//     LazyGraphNilMmap32 does it and for the same reason — a single request can
//     allocate a thousand times what it keeps, so waiting until it has answered
//     is far too late.
//
//   — Sharing, which is the one that actually decides whether this is usable.
//     A DAG bundle is hash-consed: it says what it says in a few hundred KB
//     because a subterm that occurs a thousand times is one node. Lazy graph
//     reduction preserves that for free — whnf() rewrites a node in place, so
//     every reference to it sees the result, and a shared redex is reduced
//     once. Plain eager reduction destroys it: apply() builds fresh nodes, so
//     the shared subterm is re-derived per occurrence and the normal form is
//     materialized as a *tree*. That tree is exponentially larger than the DAG
//     it prints as — compiling a definition that binds n variables costs
//     ~2.4x per variable, so the LambAda compiler runs out of memory on its own
//     source. It is not a constant factor and no collector fixes it.
//
// So sharing has to be put back by hand, and it takes both halves to work:
//
//   — Hash-consing. stem()/fork() return the existing node for a shape that has
//     been built before (`_interned`), so the arena holds the DAG rather than
//     the tree, and structurally equal terms are the same index.
//   — Memoizing the reduction. Because equal terms are now equal indices, a
//     redex that recurs can be recognized: `_memo` maps the operands of a step
//     to the normal form it reached, so a repeated one is a lookup. Only the
//     steps that recurse are memoized — the rules that answer outright are
//     cheaper to redo than to remember.
//
// Neither alone does anything: hash-consing without the memo still re-derives
// every occurrence (it just writes the answers on top of each other), and the
// memo without hash-consing never sees the same pair of indices twice. Together
// they take the same measurement from exponential to flat.
//
// Marks live in the top bit of the left field, so an index is 31 bits and the
// arena is sized to match: 2^31 nodes, 16 GiB. Nothing moves, so a Tree stays
// the index it was across a collection — which is what lets every Tree a caller
// is holding survive one without being registered anywhere.

class EagerGraphNilMmap32 {
public:
  using Tree = uint32_t;

private:
  // The top bit of the left field is the collector's mark, cleared before a
  // collection returns. An index is therefore 31 bits.
  static constexpr uint32_t MARK = 0x80000000u;
  static constexpr uint32_t IDX = 0x7fffffffu;
  static constexpr size_t ARENA_NODES = size_t(1) << 31;
  static constexpr size_t ARENA_BYTES = ARENA_NODES * 8;

  // Smallest table either side of the memory management ever shrinks to, and
  // the largest the memo is allowed to reach whatever the budget says — the
  // smaller cap applying when no budget was set (see size_memo).
  static constexpr size_t MIN_TABLE = size_t(1) << 12;
  static constexpr size_t MAX_MEMO = size_t(1) << 24;
  static constexpr size_t MIN_MEMO_CAP = size_t(1) << 21;

  // A step that resolved in fewer rule applications than this is cheaper to
  // redo than to let its entry evict a slower one from the memo (see the
  // MEMOIZE pop in apply).
  static constexpr uint32_t MEMO_MIN_STEPS = 16;

  struct Node {
    uint32_t u;
    uint32_t v;
  };

  // Continuation frames for the reduction loop. APPLY_TO and COMPUTE_AND_APPLY
  // are EagerTernaryNilMmapVM32's:
  //
  //   APPLY_TO(arg):
  //     when the current reduction lands its result r, begin apply(r, arg).
  //   COMPUTE_AND_APPLY(fn, arg):
  //     when it lands r, push APPLY_TO(r) and begin apply(fn, arg) — the
  //     apply(apply(fn, arg), r) shape the fork-stem rule reduces to.
  //   MEMOIZE(a, b):
  //     when it lands r, record that apply(a, b) is r. Pushed on entry to a
  //     step that is about to recurse, so it sits underneath whatever that step
  //     pushes and is reached exactly when the step is finished.
  //
  // Every argument is a Tree the reduction still needs, which is what makes
  // this stack the collector's root set as well as the VM's.
  enum FrameTag : uint32_t { APPLY_TO, COMPUTE_AND_APPLY, MEMOIZE };

  struct Frame {
    FrameTag tag;
    uint32_t arg1;
    uint32_t arg2; // 0 on an APPLY_TO frame, which mark() ignores
    uint32_t meta; // MEMOIZE: low bits of the step counter at push
  };

  /** One memo entry: apply(a, b) normalizes to r. a == 0 marks an empty slot. */
  struct Memo {
    uint32_t a;
    uint32_t b;
    uint32_t r;
  };

  Node *_arena;
  uint32_t _head;      // one past the highest node ever allocated
  Tree _free = 0;      // head of the free list, chained through v
  size_t _live = 0;    // what the last collection found, for sizing the next one
  size_t _budget;      // collect once _head reaches this

public:
  // Counters for RUNNER_STATS; incrementing them is noise next to the memory
  // traffic they count, so they are unconditional.
  struct Stats {
    uint64_t steps = 0, memo_hits = 0, memo_puts = 0, gcs = 0, gc_marked = 0;
  };
  Stats stats_counters;

private:
  // The continuations being unwound, reused across calls. Nested apply() calls
  // take the region above the size they found, so an outer reduction's frames
  // stay where they are — and stay roots — while an inner one runs.
  std::vector<Frame> _stack;
  std::vector<Tree> _roots;
  std::vector<Tree> _grey; // mark stack, reused across collections

  // Hash-consing: open addressing over node indices, keyed by the (u, v) of the
  // node a slot names, so a slot costs 4 bytes and the keys are the arena. 0 is
  // the empty slot, which no node can be. Exact — sharing is what the reduction
  // below is counting on, not a hint.
  std::vector<Tree> _interned;
  size_t _interned_count = 0;
  size_t _interned_mask = 0;

  // Memoized reductions: direct-mapped, one slot per hash, a colliding write
  // replacing what was there. Bounded on purpose — this is the one table whose
  // natural size is the number of distinct redexes rather than the number of
  // live nodes, and losing an entry costs time, not correctness.
  std::vector<Memo> _memo;
  size_t _memo_mask = 0;

  static size_t round_up_pow2(size_t n) {
    size_t p = MIN_TABLE;
    while (p < n) p <<= 1;
    return p;
  }

  /** 64-bit finalizer: the pair of indices is the key, and it needs mixing. */
  static uint64_t hash(uint32_t u, uint32_t v) {
    uint64_t x = (uint64_t(u) << 32) | v;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^ (x >> 33);
  }

  /** A node, from the free list or the high-water mark. Not interned. */
  Tree alloc(uint32_t u, uint32_t v) {
    Tree result;
    if (_free) {
      result = _free;
      _free = _arena[_free].v;
    } else {
      result = _head++;
    }
    _arena[result] = {u, v};
    return result;
  }

  /** Put a live node in the hash-consing table, which must have room for it. */
  void insert_interned(Tree at) {
    const Node n = _arena[at];
    size_t i = hash(n.u, n.v) & _interned_mask;
    while (_interned[i]) i = (i + 1) & _interned_mask;
    _interned[i] = at;
    ++_interned_count;
  }

  /** Re-lay the hash-consing table at `capacity`, from the arena's live nodes. */
  void rebuild_interned(size_t capacity) {
    _interned.assign(capacity, 0);
    _interned_mask = capacity - 1;
    _interned_count = 0;
    // A swept node is {0, next-free}: the leaf is the only live node whose left
    // field is 0, and it is at index 1, below where interning starts.
    for (Tree at = 2; at < _head; ++at)
      if (_arena[at].u) insert_interned(at);
  }

  /** The node for this shape: the one that already exists, or a new one. */
  Tree intern(uint32_t u, uint32_t v) {
    size_t i = hash(u, v) & _interned_mask;
    for (;; i = (i + 1) & _interned_mask) {
      const Tree at = _interned[i];
      if (!at) {
        const Tree fresh = alloc(u, v);
        _interned[i] = fresh;
        // Grow at a 0.7 load factor, so probe runs stay short.
        if (++_interned_count * 10 > (_interned_mask + 1) * 7)
          rebuild_interned((_interned_mask + 1) * 2);
        return fresh;
      }
      const Node n = _arena[at];
      if (n.u == u && n.v == v) return at;
    }
  }

  Tree memo_get(uint32_t a, uint32_t b) {
    const Memo &m = _memo[hash(a, b) & _memo_mask];
    if (m.a == a && m.b == b) { ++stats_counters.memo_hits; return m.r; }
    return 0; // no tree is index 0, so 0 is "miss"
  }

  void memo_put(uint32_t a, uint32_t b, uint32_t r) {
    ++stats_counters.memo_puts;
    _memo[hash(a, b) & _memo_mask] = {a, b, r};
  }

  /** Whether a collection's mark phase found `at` reachable. Indices 0 and 1
   * are permanent (padding and the shared leaf), so they count as live. */
  bool marked(Tree at) const {
    return at < 2 || (_arena[at].u & MARK);
  }

  /** Set MARK on everything reachable from x. Iterative: the live set is deep. */
  void mark(Tree x) {
    _grey.push_back(x);
    while (!_grey.empty()) {
      const Tree at = _grey.back();
      _grey.pop_back();
      // Index 0 is padding and index 1 the shared leaf: neither is ever swept,
      // so neither may be marked — a mark left on one would outlive the
      // collection that set it, and be read back as part of an index. It is
      // also what lets an unused frame argument be marked as a plain 0.
      if (at < 2) continue;
      Node &n = _arena[at];
      if (n.u & MARK) continue;
      n.u |= MARK;
      _grey.push_back(n.u & IDX);
      if (n.v) _grey.push_back(n.v);
    }
  }

  /**
   * Clear every mark, and chain what was not marked onto the free list.
   *
   * The list is rebuilt from scratch, so nodes freed by an earlier sweep simply
   * turn up again as unmarked — no separate "already free" state to track.
   */
  void sweep() {
    _free = 0;
    _live = 0;
    // Downwards, so the list comes out in ascending order and allocation walks
    // the arena forwards rather than backwards.
    for (Tree at = _head; at-- > 2;) {
      Node &n = _arena[at];
      if (n.u & MARK) {
        n.u &= ~MARK;
        ++_live;
      } else {
        n = {0, _free};
        _free = at;
      }
    }
  }

  void map_arena() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void *mem = mmap(nullptr, ARENA_BYTES, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mem == MAP_FAILED)
      throw std::runtime_error("mmap failed to reserve arena");
    _arena = static_cast<Node *>(mem);
    _arena[0] = {0, 0}; // index 0 reserved: 0 is the null child sentinel
    _arena[1] = {0, 0}; // the shared leaf
    _head = 2;
    _free = 0;
    _live = 0;
  }

  /**
   * Size the memo to the budget, since it is the rest of what a collection
   * bounds: a slot per eight nodes of arena, which at 12 bytes a slot is
   * comparable to what the nodes cost — a long-lived module build is exactly
   * the workload where remembering more reductions beats touching less memory.
   * Capped, because a cache stops paying for itself well before it is the size
   * of the thing it is caching, and direct-mapped, so the capacity is exactly
   * what it occupies. An unbudgeted evaluator (a one-shot benchmark run that
   * never calls set_budget) keeps the small cap: it exits before a large memo
   * pays for its footprint.
   */
  void size_memo() {
    const size_t cap = _budget < ARENA_NODES ? MAX_MEMO : MIN_MEMO_CAP;
    const size_t capacity = std::min(round_up_pow2(_budget / 8), cap);
    _memo.assign(capacity, Memo{0, 0, 0});
    _memo_mask = capacity - 1;
  }

  /**
   * Collect if the arena has grown past its budget, and raise the budget if that
   * did not leave much room.
   *
   * Called once per reduction step, which allocates at most one node, so the
   * arena stays within a node of the budget rather than only being tidied up
   * once a request is over. `a` and `b` are the step's own operands: everything
   * else it can still reach is a Frame or a caller's root, but those two are
   * held in registers and have to be named.
   */
  void collect_if_over_budget(Tree a, Tree b) {
    if (_free || _head < _budget) return; // room left, either reused or untouched
    _roots.push_back(a);
    _roots.push_back(b);
    collect();
    _roots.pop_back();
    _roots.pop_back();
    // Growing the budget when the live set turns out to be a large share of it
    // is what keeps a genuinely big term from turning into a collection per
    // allocation. There is nothing left to grow into at the ceiling.
    while (_live * 2 > _budget) {
      if (_budget >= ARENA_NODES)
        throw std::runtime_error("arena exhausted: the live set does not fit in 2^31 nodes");
      _budget = std::min(_budget * 2, ARENA_NODES);
      size_memo();
    }
  }

public:
  // No collection until set_budget() says so: an unregistered root set makes
  // everything look garbage, so opting in has to be the caller's decision.
  EagerGraphNilMmap32() : _budget(ARENA_NODES) {
    map_arena();
    rebuild_interned(MIN_TABLE);
    size_memo();
  }

  ~EagerGraphNilMmap32() { munmap(_arena, ARENA_BYTES); }

  EagerGraphNilMmap32(const EagerGraphNilMmap32 &) = delete;
  EagerGraphNilMmap32 &operator=(const EagerGraphNilMmap32 &) = delete;

  /** Drop everything allocated so far. Every Tree handed out becomes invalid. */
  void clear() {
    Node *old = _arena;
    map_arena(); // first, so a failed mapping leaves the old arena intact
    munmap(old, ARENA_BYTES);
    _stack.clear();
    _stack.shrink_to_fit();
    _roots.clear();
    _grey.clear();
    _grey.shrink_to_fit();
    rebuild_interned(MIN_TABLE);
    size_memo();
  }

  /**
   * What a collection treats as live, on top of what those reach.
   *
   * A module evaluator puts every binding here once and never touches it again:
   * nothing moves, so a root stays the index it was. Anything else a caller
   * holds that is *not* reachable from a binding — a freshly marshalled
   * argument, the result of the apply it was passed to — belongs here too, for
   * as long as it is held.
   */
  std::vector<Tree> &roots() { return _roots; }

  /**
   * Reclaim every node no root can reach. Indices survive: nothing moves.
   *
   * Both tables name nodes, so both are the collection's business. The
   * hash-consing table is re-laid over what survived — an entry left pointing
   * at a freed slot would hand out a node that has since become something else.
   * The memo is dropped outright: its keys are node indices too, and it is a
   * cache, so re-earning its entries is the cheaper correctness.
   */
  void collect() {
    for (Tree root : _roots) mark(root);
    for (const Frame &f : _stack) { // a reduction in progress is live
      mark(f.arg1);
      mark(f.arg2);
    }
    // Between mark and sweep is the one moment liveness is written on the
    // nodes themselves, which is what lets the memo be filtered rather than
    // dropped: nothing moves, so an entry whose operands and result all
    // survived still says exactly what it said, and an entry any of whose
    // nodes is about to be swept must go — the index may be reused. What the
    // filter keeps is reduction work; re-earning it was the old cost of every
    // collection.
    for (Memo &m : _memo) {
      if (!m.a) continue;
      if (marked(m.a) && marked(m.b) && marked(m.r)) continue;
      m = {0, 0, 0};
    }
    sweep();
    ++stats_counters.gcs;
    stats_counters.gc_marked += _live;
    // Re-laid at the size it already had rather than at the size of what
    // survived: the arena keeps its high-water mark, so the free list will fill
    // back up to about here before the next collection, and shrinking now only
    // buys a run of rehashes on the way back.
    rebuild_interned(_interned_mask + 1);
  }

  /** The arena's high-water mark in nodes, which is what it costs in memory. */
  size_t allocated() const { return _head - 1; } // index 0 is padding, not a node

  /** How many nodes the last collection found reachable. */
  size_t live() const { return _live; }

  /** Collect at most every `nodes` allocations. 0 never collects. */
  void set_budget(size_t nodes) {
    _budget = nodes ? nodes : ARENA_NODES;
    size_memo();
  }

  std::string stats() {
    return std::to_string(allocated()) + " nodes in arena, " +
           std::to_string(_interned_count) + " shared, " +
           std::to_string(_memo.size()) + " memo slots";
  }

  Tree leaf() { return 1; }
  Tree stem(Tree u) { return intern(u, 0); }
  Tree fork(Tree u, Tree v) { return intern(u, v); }

  // Callables are template parameters (not std::function) so the walks over a
  // result — marshalling, Evaluator's utilities — inline straight through the
  // dispatch, as they do for the other backends. Nothing is reduced here:
  // apply() already returned a normal form, so this is the read it looks like.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    const Node n = _arena[x];
    if (!n.u) return leaf_case();
    if (!n.v) return stem_case(n.u);
    return fork_case(n.u, n.v);
  }

  /**
   * Reduce apply(a, b) to normal form.
   *
   * The rules are EagerTernaryNilMmapVM32's. What is around them is the budget
   * check at the top — the one point where a collection can happen, and hence
   * the one point where the live set has to be exactly the roots, the frames
   * and these two operands — and the memo, consulted on the way into the three
   * shapes that go on to reduce something. Everything else only reads nodes and
   * interns, neither of which collects.
   */
  Tree apply(Tree a, Tree b) {
    const size_t base = _stack.size();
    Tree result;

    try {
    reduce: // ---- evaluate apply(a, b) ----
      {
        ++stats_counters.steps;
        collect_if_over_budget(a, b);

        const Node an = _arena[a];

        if (!an.u) {                                     // apply(△, b) = △b
          result = stem(b);
          goto dispatch;
        }
        if (!an.v) {                                     // apply(△u, b) = △ub
          result = fork(an.u, b);
          goto dispatch;
        }

        // a = fork(u, y)
        const Tree y = an.v;
        const Node un = _arena[an.u];

        if (!un.u) {                                     // apply(△△y, b) = y
          result = y;
          goto dispatch;
        }
        if (!un.v) { // apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
          const Tree hit = memo_get(a, b);
          if (hit) { result = hit; goto dispatch; }
          _stack.push_back({MEMOIZE, a, b});
          _stack.push_back({COMPUTE_AND_APPLY, un.u, b});
          a = y;
          goto reduce;
        }

        // apply(△(△wx)y, b) — triage on b
        const Node bn = _arena[b];

        if (!bn.u) {                                     //   b = △:  w
          result = un.u;
          goto dispatch;
        }
        if (!bn.v) {                                     //   b = △d: apply(x, d)
          const Tree hit = memo_get(a, b);
          if (hit) { result = hit; goto dispatch; }
          _stack.push_back({MEMOIZE, a, b});
          a = un.v;
          b = bn.u;
          goto reduce;
        }
        {                                                //   b = △de: apply(apply(y, d), e)
          const Tree hit = memo_get(a, b);
          if (hit) { result = hit; goto dispatch; }
          _stack.push_back({MEMOIZE, a, b});
          _stack.push_back({APPLY_TO, bn.v, 0});
          a = y;
          b = bn.u;
          goto reduce;
        }
      }

    dispatch: // ---- feed the result to the pending continuation ----
      // The frames below `base` belong to an apply() further out; this one is
      // done when it has given back everything it pushed.
      while (_stack.size() != base) {
        const Frame f = _stack.back();
        _stack.pop_back();
        if (f.tag == MEMOIZE) {
          // A tail step (`b = △d`) shares its result with the step it became,
          // so consecutive MEMOIZE frames all record the same normal form.
          // Steps that resolved in a handful of rules are cheaper to redo than
          // to let their entries evict a slower one from the memo.
          if ((uint32_t)stats_counters.steps - f.meta >= MEMO_MIN_STEPS)
            memo_put(f.arg1, f.arg2, result);
          continue;
        }
        if (f.tag == APPLY_TO) {
          // `result` is unreachable from any root for exactly as long as it
          // takes to become `a`, which allocates nothing.
          a = result;
          b = f.arg1;
        } else { // COMPUTE_AND_APPLY: apply(apply(fn, arg), result)
          _stack.push_back({APPLY_TO, result, 0});
          a = f.arg1;
          b = f.arg2;
        }
        goto reduce;
      }
      return result;
    } catch (...) {
      // An exhausted arena leaves half a reduction on the stack; drop it, so the
      // frames of a request that failed do not stay roots for every later one.
      _stack.resize(base);
      throw;
    }
  }
};
