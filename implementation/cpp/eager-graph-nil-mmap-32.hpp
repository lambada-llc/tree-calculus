#pragma once

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>

#include "jets.hpp"

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
//     cheaper to redo than to remember — and of those, not the ones that took
//     fewer than MEMO_MIN_STEPS rules, nor those of a function whose lookups
//     keep missing (recall).
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
//
// EagerGraphNilMmap32Jets also answers lambada's skip_line and arboretum's
// Nat.add and Nat.mul natively, jets (implementation/lean/Cpp/README.md);
// EagerGraphNilMmap32 is the same code without them.

// Anonymous memory, reserved rather than committed: a page is only committed
// the first time it is touched, so a region can be mapped at the most it will
// ever need and cost what is used of it.
//
// Small pages, on purpose. Huge ones (MADV_HUGEPAGE) spare the TLB, and ran
// 8-20% faster right after a run that used them; but a VM that returns freed
// memory to its host returns whole 2 MiB blocks — the ones a huge-page fault
// needs — so a run that started after a pause touched its memory at a seventh
// of the speed, and took up to twice as long.
static void *map_pages(size_t bytes) {
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
  flags |= MAP_NORESERVE;
#endif
  void *mem = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (mem == MAP_FAILED) throw std::runtime_error("mmap failed to reserve memory");
  return mem;
}

template <bool JETS> class BasicEagerGraphNilMmap32 {
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

  // The most slots the hash-consing table can reach: under reserve_interned's
  // 0.7 load factor, 2^31 nodes take 2^32.
  static constexpr size_t MAX_INTERNED = ARENA_NODES * 2;

  // A step that resolved in fewer rule applications than this is cheaper to
  // redo than to let its entry evict a slower one from the memo (see the
  // MEMOIZE pop in apply). Low, because a step of a few rules can still be
  // the one asked for most: a compiler's character-class test is asked of
  // every character of its input, and at 16 compile_file of a 5 MB source
  // took twice the steps.
  static constexpr uint32_t MEMO_MIN_STEPS = 4;

  // How many lookups in a row may miss for one function before it stops being
  // looked up at all (see recall).
  static constexpr uint8_t MEMO_COLD = 64;

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

  // A frame is two 8-byte words, written whole and read back whole, because
  // it is usually popped within a few instructions of being pushed: a load
  // that the store buffer cannot answer from one earlier store (a 16-byte
  // Frame read back from its four 4-byte fields, say) waits for those stores
  // to retire instead — a stall on nearly every step.
  struct Frame {
    uint64_t lo, hi;
    Frame() = default;
    Frame(FrameTag tag, uint32_t arg1, uint32_t arg2, uint32_t meta = 0)
        : lo(tag | uint64_t(arg1) << 32), hi(arg2 | uint64_t(meta) << 32) {}
    FrameTag tag() const { return FrameTag(uint32_t(lo)); }
    uint32_t arg1() const { return lo >> 32; }
    uint32_t arg2() const { return uint32_t(hi); } // 0 on an APPLY_TO frame, which mark() ignores
    uint32_t meta() const { return hi >> 32; }     // MEMOIZE: low bits of the step counter at push
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
  Tree _newest = ~0u;  // the node alloc() made last, if no collection since
  size_t _live = 0;    // what the last collection found, for sizing the next one
  size_t _budget;      // collect once _head reaches this

public:
  // Counters for RUNNER_STATS; incrementing them is noise next to the memory
  // traffic they count, so they are unconditional.
  struct Stats {
    uint64_t steps = 0, memo_hits = 0, memo_puts = 0, gcs = 0, gc_marked = 0;
    uint64_t jets = 0, skipped = 0; // jets taken, and list elements they skipped
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
  // the empty slot, which no node can be.
  //
  // Exact, and everything else here counts on it: two structurally equal trees
  // are one node, hence one index, always — across collections (the table is
  // re-laid from what survived, never dropped), clear(), and everything a
  // caller builds through stem(), fork() or list(). The memo is keyed on it,
  // list() skips searches because of it, and deciding that two trees are equal
  // by comparing their indices is sound only because of it. So no node is ever
  // made by alloc() alone: it is made by a search that came up empty, or where
  // no search could have found one (list(), and intern() over the node made
  // last). A search is this table's, and for the cells of a run (below), where
  // the cell would be.
  //
  // Both tables are mapped once, at the most they can reach, and re-laid in
  // place: rebuild_interned() reads the arena rather than the old table, so
  // nothing needs the old copy. Memory already touched costs a memset to
  // re-lay; a fresh mapping costs a fault per page all over again — on a VM, a
  // host round trip per page — and holds both copies at once while it grows.
  Tree *const _interned = static_cast<Tree *>(map_pages(MAX_INTERNED * sizeof(Tree)));
  size_t _interned_count = 0;
  size_t _interned_mask = 0;

  // A run: cells of the last long list() made, (_run, _run_end], left out of
  // the table because where one is says what it is. list() lays a list out
  // from its end at consecutive indices, from _run up: each cell's tail is the
  // cell right below it, so for a v in [_run, _run_end) the one cell that can
  // be (u, v) is the one at v + 1 (in_run). The cell at _run, the list's first
  // new one, is not the run's: its tail is not below it, and it goes in the
  // table. A source file bound as a string is millions of cells, and inserting
  // each was four fifths of marshalling it; this is a comparison.
  //
  // Every cell reaches all the cells below it, so what survives a collection
  // of a run is a range from its start, and the run shrinks to it (collect):
  // the range holds the run's cells and nothing else — no slot the free list
  // hands out — so the table can be re-laid around it rather than through it.
  // A run ends when the next one starts (end_run), or when none of it survives.
  static constexpr size_t RUN_MIN = size_t(1) << 16;
  Tree _run = 0, _run_end = 0;

  // Memoized reductions: direct-mapped, one slot per hash, a colliding write
  // replacing what was there. Bounded on purpose — this is the one table whose
  // natural size is the number of distinct redexes rather than the number of
  // live nodes, and losing an entry costs time, not correctness.
  Memo *const _memo = static_cast<Memo *>(map_pages(MAX_MEMO * sizeof(Memo)));
  size_t _memo_mask = 0;
  size_t _memo_cap = 0;  // the bound (see size_memo)
  size_t _memo_puts = 0; // since it last grew (see memo_put)

  // Per function (the `a` of apply(a, b), by hash), how many of its lookups in
  // a row have missed, up to MEMO_COLD, and past it how many it has skipped
  // since one was last made (see recall). Small on purpose: it is read on every
  // lookup, so it has to stay in cache where the memo cannot.
  uint8_t _cold[1 << 16];

  // jets.hpp's trees, or the parts of them a partial is told apart by (mul's U;
  // add's C, K, N, L(△) and L(△△): intern_jets), interned on a fresh arena and
  // marked by every collection, so that each stays the index of its tree:
  // interning is exact, so comparing an index with them compares trees. 0,
  // which no tree is, while jets are off.
  Tree _skip_line = 0, _newline = 0, _mul_u = 0, _add_c = 0, _add_k = 0, _add_nil = 0;
  Tree _add_bit[2] = {};
  std::vector<uint64_t> _x, _y, _z; // add's and mul's operands and a product in limbs, reused
  bool _jets = JETS;

  static size_t round_up_pow2(size_t n) {
    size_t p = MIN_TABLE;
    while (p < n) p <<= 1;
    return p;
  }

  /**
   * The pair of indices is the key, and it needs mixing — but every step hashes
   * before it can load anything, so the mixing is on the critical path. One
   * multiply (Fibonacci hashing), with the high half, where a product mixes
   * best, folded onto the low bits the tables index by.
   *
   * Which leaves the bits of `u` at and above a table's index width out of the
   * index: bit 32 + j of the product depends on bits 0..j of u only, so in a
   * table of 2^k slots (u, v) and (u + 2^k, v) always share a slot. The
   * hash-consing table is sized to the live nodes, and indices outrun it only
   * by what collections have freed (probes measure 2.3-2.8 slots a search);
   * the memo is smaller than the arena by design, and there such pairs evict
   * each other. Folding a 128-bit product instead keeps every bit, and took
   * 0.15% fewer steps on the build's three heaviest tests — in 35.9 s against
   * this hash's 35.4 s.
   */
  static uint64_t hash(uint32_t u, uint32_t v) {
    const uint64_t x = ((uint64_t(u) << 32) | v) * 0x9e3779b97f4a7c15ULL;
    return x ^ (x >> 32);
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
    return _newest = result;
  }

  /** Put a live node in the hash-consing table, which must have room for it. */
  void insert_interned(Tree at) {
    const Node n = _arena[at];
    size_t i = hash(n.u, n.v) & _interned_mask;
    while (_interned[i]) i = (i + 1) & _interned_mask;
    _interned[i] = at;
    ++_interned_count;
  }

  /** Re-lay the hash-consing table at `capacity`, from the arena's live nodes
   * outside the run. Out of line, as is everything else apply() reaches only
   * now and then — end_run, grow_memo, collect: inlined, they made apply()
   * twice the size, and which of its helpers the compiler inlined then hung on
   * any edit to it, for as much as 13% more instructions a step. */
  [[gnu::noinline]] void rebuild_interned(size_t capacity) {
    std::fill_n(_interned, capacity, 0);
    _interned_mask = capacity - 1;
    _interned_count = 0;
    // A swept node is {0, next-free}: the leaf is the only live node whose left
    // field is 0, and it is at index 1, below where interning starts.
    const auto insert_live = [&](Tree from, Tree to) {
      for (Tree at = std::max(from, Tree(2)); at < to; ++at)
        if (_arena[at].u) insert_interned(at);
    };
    insert_live(2, _run + 1);
    insert_live(_run_end + 1, _head);
  }

  /** The cell of the run that is (u, v), or 0: the one above v, if v is a tail in the run. */
  Tree in_run(uint32_t u, uint32_t v) const {
    if (v >= _run_end) return 0; // every node made since the run, and with none, all of them
    return v >= _run && _arena[v + 1].u == u ? v + 1 : 0;
  }

  /** The node for this shape if there is one, else 0. The run first: a list
   * that is some of it — a string bound again, or a suffix of one — finds every
   * cell right where it looks, and never probes the table at all. */
  Tree find(uint32_t u, uint32_t v) const {
    if (const Tree cell = in_run(u, v)) return cell;
    return _interned[slot(u, v)];
  }

  /** Put the run's cells in the table, so that it no longer takes a run to find them. */
  [[gnu::noinline]] void end_run() {
    const size_t capacity = capacity_for(_interned_count + (_run_end - _run));
    if (capacity != _interned_mask + 1) rebuild_interned(capacity); // which leaves the run out
    for (Tree at = _run + 1; at <= _run_end; ++at) insert_interned(at);
    _run = _run_end = 0;
  }

  /** Grow the table, if need be, so that `more` further nodes fit under a 0.7
   * load factor, which keeps probe runs short. Grown, it has room for the run's
   * cells as well, as if they were in it: a reduction that outgrows the table
   * is typically one working through the run, and would otherwise take it
   * through every size its cells' insertion would have skipped, probing fuller
   * tables on the way (5% more instructions compiling a 5 MB source with a
   * compiler that walks every character of it). */
  void reserve_interned(size_t more) {
    if ((_interned_count + more) * 10 > (_interned_mask + 1) * 7)
      rebuild_interned(capacity_for(_interned_count + more + (_run_end - _run)));
  }

  /** The table's size, doubled as often as it takes to hold `nodes`. */
  size_t capacity_for(size_t nodes) const {
    size_t capacity = _interned_mask + 1;
    while (nodes * 10 > capacity * 7) capacity *= 2;
    return capacity;
  }

  /** The slot holding the node for this shape, or the empty one it would take. */
  size_t slot(uint32_t u, uint32_t v) const {
    for (size_t i = hash(u, v) & _interned_mask;; i = (i + 1) & _interned_mask) {
      const Tree at = _interned[i];
      if (!at) return i;
      const Node n = _arena[at];
      if (n.u == u && n.v == v) return i;
    }
  }

  /**
   * The node for this shape: the one that already exists, or a new one.
   *
   * No search when a child is the node made last: a node is made after its
   * children, so nothing can have that one as a child yet (list()'s argument,
   * a cell at a time). That is a quarter to a half of all the nodes a
   * reduction builds — whatever it builds on what it just built — and without
   * a search to wait on, the table write overlaps with the steps after it.
   */
  Tree intern(uint32_t u, uint32_t v) {
    if (u == _newest || v == _newest) {
      const Tree fresh = alloc(u, v);
      insert_interned(fresh);
      reserve_interned(0);
      return fresh;
    }
    const size_t i = slot(u, v);
    if (_interned[i]) return _interned[i];
    if (const Tree cell = in_run(u, v)) return cell;
    const Tree fresh = alloc(u, v);
    _interned[i] = fresh;
    ++_interned_count;
    reserve_interned(0);
    return fresh;
  }

  Tree memo_get(uint32_t a, uint32_t b) {
    const Memo &m = _memo[hash(a, b) & _memo_mask];
    if (m.a == a && m.b == b) { ++stats_counters.memo_hits; return m.r; }
    return 0; // no tree is index 0, so 0 is "miss"
  }

  /** Record an entry, and double the memo once it has taken as many entries
   * as it has slots, until it reaches its bound. */
  void memo_put(uint32_t a, uint32_t b, uint32_t r) {
    ++stats_counters.memo_puts;
    _memo[hash(a, b) & _memo_mask] = {a, b, r};
    if (++_memo_puts > _memo_mask && _memo_mask + 1 < _memo_cap) grow_memo();
  }

  /**
   * Double the memo in place, keeping its entries: at twice the size, an entry
   * hashes to the slot it is in or to the one `half` above it, in the half being
   * added — so no entry moves onto another.
   */
  [[gnu::noinline]] void grow_memo() {
    const size_t half = _memo_mask + 1;
    std::fill_n(_memo + half, half, Memo{0, 0, 0});
    _memo_mask = 2 * half - 1;
    for (Memo *m = _memo; m < _memo + half; ++m)
      if (m->a && (hash(m->a, m->b) & half)) {
        m[half] = *m;
        *m = {0, 0, 0};
      }
    _memo_puts = 0;
  }

  /**
   * What the memo says apply(a, b) is, or 0 — in which case the step is about
   * to be reduced, and a MEMOIZE frame is pushed to record it.
   *
   * Unless `a` has gone cold. Most lookups are for functions that never hit:
   * in compile_file of a 5 MB source, three in four are, each of them a cache
   * miss into a table sized for a build, plus a write when the step is done
   * that evicts something that would have. A function MEMO_COLD lookups in a
   * row missed is neither looked up nor recorded, except every sixteenth of its
   * own lookups, and a hit warms it again. Its own, counted in its own byte:
   * with one count for all cold lookups, a function looked up between the same
   * even number of others each time keeps landing on the same residue, and can
   * stay unsampled for good. What is left is the lookups a cold function skips
   * once it needs the memo after all — linear, not exponential: a recursion
   * that calls itself twice on one argument takes about twice the steps it
   * does with every lookup made.
   */
  Tree recall(Tree a, Tree b) {
    uint8_t &cold = _cold[uint32_t(hash(a, 0) >> 48)];
    if (cold >= MEMO_COLD && ++cold < MEMO_COLD + 16) return 0;
    if (const Tree hit = memo_get(a, b)) {
      cold = 0;
      return hit;
    }
    cold = std::min(cold + 1, int(MEMO_COLD)); // a sampled miss counts afresh
    _stack.emplace_back(MEMOIZE, a, b, (uint32_t)stats_counters.steps);
    return 0;
  }

  /** The tree a jets.hpp DAG denotes, read as dag2lean.mjs reads it: a line of
   * three words builds a stem or a fork, and the line of one names the tree. */
  Tree intern_dag(const char *dag) {
    std::unordered_map<std::string, Tree> env{{"\xe2\x96\xb3", leaf()}}; // △
    std::istringstream lines(dag);
    Tree value = 0;
    for (std::string line; std::getline(lines, line);) {
      std::istringstream words(line);
      std::string w[3];
      words >> w[0] >> w[1] >> w[2];
      if (!w[2].empty()) { // △ x is △x, △u x is △ux
        const Tree u = _arena[env.at(w[1])].u, x = env.at(w[2]);
        env[w[0]] = u ? fork(u, x) : stem(x);
      } else if (!w[0].empty()) value = env.at(w[0]);
    }
    return value;
  }

  void intern_jets() {
    if constexpr (JETS) {
      _skip_line = _newline = _mul_u = _add_c = _add_k = _add_nil = _add_bit[0] = _add_bit[1] = 0;
      if (!_jets) return;
      _skip_line = intern_dag(jets::skipLine), _newline = intern_dag(jets::newline);
      // mul is S (K (△U)) K, so `mul a` is △U (△△a) by S and K alone.
      _mul_u = _arena[_arena[_arena[_arena[intern_dag(jets::mul)].u].u].v].u;
      // `add a` is △ (△ (△C Z(a))) K, and Z(a) a closure per cell of a, which
      // the rules build as they take a apart: Z([]) is N, and Z(h:t) is
      // △ L(h) (△ (△ (h:t) △) (△△ Z(t))). The pieces are read off
      // addPartial, add [△, △△].
      const Node p = _arena[intern_dag(jets::addPartial)], q = _arena[_arena[p.u].u];
      _add_k = p.v, _add_c = q.u, _add_nil = q.v;
      for (Tree &l : _add_bit) // L(△), then L(△△), and below them N
        l = _arena[_add_nil].u, _add_nil = _arena[_arena[_arena[_add_nil].v].v].v;
    }
  }

  /** r, which apply(a, b) is, recorded as RStep's put for the MEMOIZE frame
   * recall() pushed, if it did: a jet takes no steps, so that frame would not. */
  Tree answered(Tree a, Tree b, Tree r) {
    if (!_stack.empty() && _stack.back().tag() == MEMOIZE &&
        _stack.back().arg1() == a && _stack.back().arg2() == b)
      memo_put(a, b, r);
    return r;
  }

  /**
   * apply(skip_line, xs) by DropJet's transitions (TreeCalculus/Check.lean):
   * skip each element that is not the newline, and answer what follows the
   * first that is, or the leaf ending the list. A stem ending the list has no
   * transition: 0, and xs the stem, for the rules.
   */
  Tree skip_line(Tree &xs) {
    const Tree b = xs;
    ++stats_counters.jets;
    for (Node n; (n = _arena[xs]).u; xs = n.v, ++stats_counters.skipped) {
      if (!n.v) return 0;
      if (n.u == _newline) { xs = n.v; break; }
    }
    return answered(_skip_line, b, xs);
  }

  /** Whether x is a natural without trailing △s — a list of the bits △ and △△,
   * least significant first (conventions/README.md) — and if so, its 64-bit
   * limbs in w, least significant first, the top one not 0. */
  bool limbs(Tree x, std::vector<uint64_t> &w) {
    w.clear();
    bool top = true; // the last bit, which is △△ unless there are none
    for (size_t i = 0;; ++i) {
      const Node n = _arena[x], h = _arena[n.u];
      if (!n.u) return top;
      if (!n.v || (n.u != leaf() && (h.u != leaf() || h.v))) return false; // a stem, or no bit
      if (i % 64 == 0) w.push_back(0);
      top = n.u != leaf();
      w.back() |= uint64_t(top) << i % 64;
      x = n.v;
    }
  }

  /** The natural whose bits w's limbs are, without trailing △s. */
  Tree natural(const std::vector<uint64_t> &w) {
    const auto bit = [&](size_t i) { return w[i / 64] >> i % 64 & 1; };
    size_t n = 64 * w.size();
    while (n && !bit(n - 1)) --n;
    const Tree one = stem(leaf());
    return list(n, [&, i = n]() mutable { return bit(--i) ? one : leaf(); }, leaf());
  }

  /**
   * apply(△ (△q) K, b), q = △C Z(a) — Nat.add a b — as the rules answer it
   * when a and b are naturals without trailing △s: a + b, likewise without.
   * Anything else, 0, for the rules. Z(a) holds a layer per cell of a
   * (intern_jets), so telling it apart walks a twice: to read a, and down Z(a).
   */
  [[gnu::noinline]] Tree add(Tree p, Tree q, Tree b) {
    Tree z = _arena[q].v;
    const Tree a = z == _add_nil ? leaf() : _arena[_arena[_arena[z].v].u].u; // Z(h:t)'s h:t
    if (!limbs(a, _x) || !limbs(b, _y)) return 0;
    for (Tree s = a; s != leaf(); s = _arena[s].v) { // z is Z(s)?
      const Node n = _arena[z], v = _arena[n.v], c = _arena[v.u], k = _arena[v.v];
      if (n.u != _add_bit[_arena[s].u != leaf()] || c.u != s || c.v != leaf() || k.u != leaf())
        return 0;
      z = k.v;
    }
    if (z != _add_nil) return 0;
    ++stats_counters.jets;
    if (_x.size() < _y.size()) _x.swap(_y);
    _x.push_back(0), _y.resize(_x.size());
    unsigned __int128 sum = 0;
    for (size_t i = 0; i < _x.size(); ++i, sum >>= 64)
      _x[i] = uint64_t(sum += (unsigned __int128)_x[i] + _y[i]);
    return answered(p, b, natural(_x));
  }

  /**
   * apply(△U (△△a), b) — Nat.mul a b — as the rules answer it when a and b
   * are naturals without trailing △s: a · b, likewise without. Schoolbook,
   * over limbs. Anything else, 0, for the rules.
   */
  [[gnu::noinline]] Tree mul(Tree p, Tree y, Tree b) {
    const Node k = _arena[y]; // △△a
    if (k.u != leaf() || !k.v || !limbs(k.v, _x) || !limbs(b, _y)) return 0;
    ++stats_counters.jets;
    _z.assign(_x.size() + _y.size(), 0);
    for (size_t i = 0; i < _x.size(); ++i) {
      unsigned __int128 t = 0;
      for (size_t j = 0; j < _y.size(); ++j, t >>= 64)
        _z[i + j] = uint64_t(t += (unsigned __int128)_x[i] * _y[j] + _z[i + j]);
      _z[i + _y.size()] = uint64_t(t);
    }
    return answered(p, b, natural(_z));
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
    _newest = ~0u;
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
    _arena = static_cast<Node *>(map_pages(ARENA_BYTES));
    _arena[0] = {0, 0}; // index 0 reserved: 0 is the null child sentinel
    _arena[1] = {0, 0}; // the shared leaf
    _head = 2;
    _free = 0;
    _newest = ~0u;
    _live = 0;
    _run = _run_end = 0;
  }

  /**
   * Empty the memo, and bound it by the budget, since it is the rest of what a
   * collection bounds: a slot per eight nodes of arena, which at 12 bytes a slot
   * is comparable to what the nodes cost — a long-lived module build is exactly
   * the workload where remembering more reductions beats touching less memory.
   * Capped, because a cache stops paying for itself well before it is the size
   * of the thing it is caching, and direct-mapped, so the capacity is exactly
   * what it occupies. An unbudgeted evaluator (a one-shot benchmark run that
   * never calls set_budget) keeps the small cap: it exits before a large memo
   * pays for its footprint.
   *
   * The bound is reached by growing (memo_put), not laid out up front: laying
   * a memo is a write to every slot — 96 MB at the default budget, which was
   * most of what starting a runner cost — and a small request never fills one.
   */
  void size_memo() {
    const size_t cap = _budget < ARENA_NODES ? MAX_MEMO : MIN_MEMO_CAP;
    _memo_cap = std::min(round_up_pow2(_budget / 8), cap);
    std::fill_n(_memo, MIN_TABLE, Memo{0, 0, 0});
    _memo_mask = MIN_TABLE - 1;
    _memo_puts = 0;
    std::fill(std::begin(_cold), std::end(_cold), 0);
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
  BasicEagerGraphNilMmap32() : _budget(ARENA_NODES) {
    map_arena();
    rebuild_interned(MIN_TABLE);
    size_memo();
    intern_jets();
  }

  ~BasicEagerGraphNilMmap32() {
    munmap(_arena, ARENA_BYTES);
    munmap(_interned, MAX_INTERNED * sizeof(Tree));
    munmap(_memo, MAX_MEMO * sizeof(Memo));
  }

  BasicEagerGraphNilMmap32(const BasicEagerGraphNilMmap32 &) = delete;
  BasicEagerGraphNilMmap32 &operator=(const BasicEagerGraphNilMmap32 &) = delete;

  /** Whether apply() takes jets, on by default. Off, their trees are not even
   * interned, so it reduces as EagerGraphNilMmap32 does. Clears, as clear(). */
  void set_jets(bool on) {
    _jets = on;
    clear();
  }

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
    intern_jets();
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
  [[gnu::noinline]] void collect() {
    for (Tree root : _roots) mark(root);
    for (const Frame &f : _stack) { // a reduction in progress is live
      mark(f.arg1());
      mark(f.arg2());
    }
    if constexpr (JETS)
      for (const Tree t : {_skip_line, _newline, _mul_u, _add_c, _add_k, _add_nil, _add_bit[0],
                           _add_bit[1]})
        mark(t);
    // Between mark and sweep is the one moment liveness is written on the
    // nodes themselves, which is what lets the memo be filtered rather than
    // dropped: nothing moves, so an entry whose operands and result all
    // survived still says exactly what it said, and an entry any of whose
    // nodes is about to be swept must go — the index may be reused. What the
    // filter keeps is reduction work; re-earning it was the old cost of every
    // collection.
    for (Memo *m = _memo; m <= _memo + _memo_mask; ++m) {
      if (!m->a) continue;
      if (marked(m->a) && marked(m->b) && marked(m->r)) continue;
      *m = {0, 0, 0};
    }
    // Swept indices will be reused by other functions.
    std::fill(std::begin(_cold), std::end(_cold), 0);
    sweep();
    while (_run_end > _run && !_arena[_run_end].u) --_run_end; // what survived of the run
    if (_run_end == _run) _run = _run_end = 0;
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
           std::to_string(_memo_mask + 1) + " memo slots";
  }

  Tree leaf() { return 1; }
  Tree stem(Tree u) { return intern(u, 0); }
  Tree fork(Tree u, Tree v) { return intern(u, v); }

  /**
   * fork(x1, fork(x2, … fork(xn, tail))), with last() giving xn, then x(n-1),
   * down to x1: what fork() would build a cell at a time, from the list's end,
   * for the one shape a caller builds in bulk — a marshalled string is a cell
   * per character. last() must not build a node.
   *
   * Hash-consed exactly like fork(), but a cell only has to be looked for
   * until one comes out new: a node that already existed cannot have a child
   * that did not, so every cell in front of a new one is new as well. (That
   * holds for a slot the free list hands back too: a collection frees only
   * what no live node reaches.) A few new cells just take a slot in a table
   * sized for all of them up front. RUN_MIN or more are written at the
   * high-water mark, one above the other, as last() gives them — so a string
   * is decoded straight into them — and all but the first are a run. Only
   * while nothing a collection freed is waiting to be reused: then those are
   * the indices alloc() would hand out anyway, so no reduction takes a step
   * more or fewer, and the budget still bounds the arena. Laid above a free
   * list, a run would grow the arena by every long string bound, with no
   * collection to come while the list lasts (collect_if_over_budget).
   */
  template <typename Last> Tree list(size_t n, Last last, Tree tail) {
    for (; n > 0; --n) { // cells that may exist already
      const Tree head = last();
      if (const Tree at = find(head, tail)) {
        tail = at;
        continue;
      }
      if (n < RUN_MIN || _free) { // cells that cannot
        reserve_interned(n);
        insert_interned(tail = alloc(head, tail));
        while (--n) insert_interned(tail = alloc(last(), tail));
        return tail;
      }
      if (_head + n > ARENA_NODES) throw std::runtime_error("arena exhausted: the list does not fit in 2^31 nodes");
      end_run(); // room first, while the cells are not yet allocated: a re-lay would take them in
      reserve_interned(1);
      const Tree first = _head, end = _head + n, newest = _newest;
      _arena[first] = {head, tail};
      for (Tree at = first + 1; at < end; ++at) _arena[at] = {last(), at - 1};
      if (_newest != newest) throw std::logic_error("list(): last() built a node");
      _head = end;
      insert_interned(first);
      _run = first;
      _run_end = end - 1;
      return _newest = end - 1;
    }
    return tail;
  }

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
   *
   * Out of line, so that the step loop has the registers to itself: inlined
   * into runner.cpp's parse_dag_into, it reloads the arena and the stack on
   * every step.
   */
  [[gnu::noinline]] Tree apply(Tree a, Tree b) {
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
          if (const Tree hit = recall(a, b)) { result = hit; goto dispatch; }
          if (JETS && an.u == _mul_u && (result = mul(a, y, b))) goto dispatch;
          if (JETS && y == _add_k && _arena[un.u].u == _add_c && (result = add(a, un.u, b)))
            goto dispatch;
          _stack.emplace_back(COMPUTE_AND_APPLY, un.u, b);
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
          if (const Tree hit = recall(a, b)) { result = hit; goto dispatch; }
          a = un.v;
          b = bn.u;
          goto reduce;
        }
        {                                                //   b = △de: apply(apply(y, d), e)
          if (const Tree hit = recall(a, b)) { result = hit; goto dispatch; }
          // After the memo, so that the jet never makes slower what it answers.
          if (JETS && a == _skip_line) {
            if ((result = skip_line(b))) goto dispatch;
            goto reduce;
          }
          _stack.emplace_back(APPLY_TO, bn.v, 0);
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
        if (f.tag() == MEMOIZE) {
          // A tail step (`b = △d`) shares its result with the step it became,
          // so consecutive MEMOIZE frames all record the same normal form.
          // Steps that resolved in a handful of rules are cheaper to redo than
          // to let their entries evict a slower one from the memo.
          if ((uint32_t)stats_counters.steps - f.meta() >= MEMO_MIN_STEPS)
            memo_put(f.arg1(), f.arg2(), result);
          continue;
        }
        if (f.tag() == APPLY_TO) {
          // `result` is unreachable from any root for exactly as long as it
          // takes to become `a`, which allocates nothing.
          a = result;
          b = f.arg1();
        } else { // COMPUTE_AND_APPLY: apply(apply(fn, arg), result)
          _stack.emplace_back(APPLY_TO, result, 0);
          a = f.arg1();
          b = f.arg2();
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

using EagerGraphNilMmap32 = BasicEagerGraphNilMmap32<false>;
using EagerGraphNilMmap32Jets = BasicEagerGraphNilMmap32<true>;
