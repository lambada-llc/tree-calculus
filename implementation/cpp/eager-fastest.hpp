#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif

#include "jets.hpp"

#define EAGER_LIKELY(x) __builtin_expect(!!(x), 1)
#define EAGER_UNLIKELY(x) __builtin_expect(!!(x), 0)

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
//     copy of, but for the K-absorber peek), so depth costs what any other
//     allocation costs.
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
//     been built before (`_groups`), so the arena holds the DAG rather than
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
// An index is 31 bits and the arena is sized to match: 2^31 nodes, 16 GiB.
// Nothing moves, so a Tree stays the index it was across a collection — which
// is what lets every Tree a caller is holding survive one without being
// registered anywhere.
//
// EagerFastestJets also answers lambada's skip_line natively, a jet
// (implementation/lean/Cpp/README.md); EagerFastest is the same code
// without it.
//
// This is eager-graph-nil-mmap-32.hpp (kept, for comparison) made faster on
// forest's build, measured by replaying the exact requests its compile phase
// and its expect tests send the runner, with the output checked byte for byte:
//
//   — The step loop keeps its state in registers: a frame is a store and a
//     bump onto an mmap'd stack (std::vector's emplace_back was an out-of-line
//     call a step), the step counter and the arena pointer are locals, the
//     nursery fast path of intern() is inlined, and a collection is a cold
//     call behind one byte. 184 instructions a step became 143.
//   — Rule (2)'s inner application is read off the function when its frame is
//     pushed (STEM, FORK_WITH, RETURN, FORK_RIGHT): a quarter fewer steps.
//   — A collection marks into a bitmap and re-inserts the survivors as it
//     sweeps: 109 ms instead of 267 at a 256 MB budget.
//   — The hash-consing table's EMPTY is 0, so an untouched page is empty.
//   — Hashing was 12.6% of the instructions, 21M hash() calls per 10M steps;
//     now 4.7%: a MEMOIZE frame carries the memo hash so the put never
//     hashes again, the cold index is one 32-bit multiply (the same 16 bits
//     hash(a, 0) gave), the table and the nursery hash with two 32-bit
//     multiplies by immediates (no 64-bit constant to load) and a nursery
//     slot carries that hash, so a flush reads neither the arena nor hashes.
//     7.2% fewer instructions on the build's heaviest compile, with
//     identical steps: the memo's hash is untouched (see hash()).
//   — Allocation and the nursery: a flush is one pass (a node inserted and
//     its slot cleared as it goes, the count added once, the log read
//     once rather than three times), `_young` is a byte per node rather than
//     a bit, alloc() tests the budget with a compare instead of recomputing
//     _due, and the flush threshold is precomputed: 3.5% fewer instructions
//     on the same compile (the flush 11%, apply 4%), 10% fewer D1 misses,
//     identical steps.
//   — The frames: a DONE frame under what a call pushes, so that a pop never
//     compares the top with the base; the tag in the low half of a frame's
//     second word, so that a constant tag is an immediate store and an
//     APPLY_TO frame's second word is 0 (the fusions onto it test one word);
//     the pop chain in pop-frequency order; the memo's growth a countdown;
//     the free-list allocation leaving `_due` alone until the list runs out.
//     6.4% fewer instructions on the build's heaviest compile, the same
//     steps and memo traffic.
//   — The hash-consing table is laid at the size its budget takes it to
//     (groups_for_budget) rather than doubled up to it: every node the arena
//     holds before a collection is interned, so the table reached that size
//     anyway, re-inserting everything in it at each doubling — 2.94M
//     insertions in 10 re-lays on the build's heaviest compile, 8.8% of its
//     instructions, with one collection; on the heavy expect tests, which
//     raise their budget, 11.7M. The budget is decided between a
//     collection's mark and its sweep, so the sweep's re-lay is the one that
//     sizes the table for it. And the search past the nursery calls nothing
//     and builds no struct on its way out (intern_old, intern_fresh, the
//     one-instruction tag): together 11% fewer instructions on that compile,
//     the expect tests 4-8% faster, peak memory the same on the build's
//     phases (a raise the live set forces lays no larger table: see
//     collect_at).
//   — The step itself, by the instruction: the memo keyed by the one word
//     hash() multiplies (compared in one, carried by the MEMOIZE frame), the
//     step's operands 64-bit locals (both in registers, no zero-extension to
//     index with; one had lived in a stack slot), a node read as one word, a
//     32-bit memo mask, the cold count stepped without a clamp, the step
//     counter kept scaled so a MEMOIZE frame's low word is the counter with
//     its tag, the pop's top pinned to one register (the compiler carried
//     two), tags tested in the frame's word as loaded, the nursery indexed
//     by the hash's low bits. 6.5% fewer instructions on the same compile,
//     the same steps, output, hashes and memo traffic.
//   — The flush: a flushed node's `_young` byte is left set, since nothing
//     reads it again until a collection clears them all (forgotten()); a
//     group's empty slots are one movemask off the tags' top bits (empties);
//     the flush loop keeps its members in registers and its prefetch has no
//     bounds test. 45 instructions a node flushed became 26: 4% fewer on
//     the heaviest compile. And the nursery is flushed a quarter full rather
//     than half: its probe chains are what a step waits on, and the shorter
//     ones cut 7% of apply()'s mispredicted branches and 7-15% off the
//     compile phase's time (two runs, on a shared machine).
//
// Measured and not taken, so that no one measures them again: sizing the
// table for the start budget only, not for a raised one (the heavy expect
// tests then gain nothing: the re-lays that cost are those after a raise);
// a frame's first word laid out as the memo key (a shift on every push and
// pop of a one-argument frame: 1% more instructions), choosing the
// result-only frames' intern operands by conditional moves rather than two
// compares (0.5% more instructions for 0.5M fewer of cachegrind's
// mispredicts), a collection flag in the step counter's sign bit (one
// instruction a step, but threaded through alloc()'s callers: not worth
// it), the stats counters as locals (0.7 instructions a step, and no
// callee-saved register left to hold them); 16-byte or set-associative memo
// entries, folding the MEMOIZE frame into the
// result-only frame pushed over it (FORK_WITH, STEM, RETURN with the memo's
// operands: one frame fewer on a third of the memoized steps, but the flag
// that chooses the frame, the wider frame and the operands kept live across
// rule (2) cost more than the frame saved — 2% more instructions, not 1%
// fewer), an L1 front memo, a prefetch of the memo slot at the top of a step
// (6-10% slower), memo regions per function (most
// closures are looked up once: 2.8M of 3.2M on a compile), MEMO_MIN_STEPS and
// MEMO_COLD sweeps, a smaller _cold table; growing the table 4x (more pages
// than re-lays saved), flushing the nursery in table order, a 2^18 nursery,
// a nursery log carrying hashes; 3-bit node kinds with 30-bit indices and a
// switch or computed goto in place of the rule branches (13% more
// instructions, the same mispredicts: which rule comes next is the same
// information either way); clang, -O2 (25% slower: -O3's inlining is what
// keeps the step in registers), PGO (nothing left to lay out), -march, branch
// hints, dropping the stack protector; prefaulting, a startup huge-page probe
// (cannot tell a cold VM from a warm one), DONTNEED above the live set;
// dropping the nursery's log (`_nursed`) for an SSE2 scan of its slots at
// each flush (2.4% more instructions and 16% slower: 512 KB read per flush,
// 3M more mispredicts); `_young` as a wrapped byte so that its stores alias
// no member (nothing: the members a nurse() loads it loads once anyway);
// the nursery flushed three quarters full (2% more instructions, 5% more
// mispredicts) or a ring of the slots read ahead so that a flush reads each
// once (one instruction a node at best); never memoizing an application of
// a closure built since the module was loaded (a compile 2.6% faster, the
// expect tests 6% slower and the heaviest of them 33%: recursion through
// fresh closures needs the memo). On the build's four heaviest expect tests,
// which run at
// 53 ns a step where a compile runs at 40 (their L2 misses are the table's
// lines and the memo's, 1.4 a step; the rules' own loads hit): huge pages
// (no faster: not the TLB), a prefetch of the next step's memo slot, a memo
// cap of 2^23 or 2^24 slots at a 256 MB budget (3% fewer steps, no faster),
// MEMO_COLD at 16, 200 or off (off: 2.3x the puts, 24% slower), a nursery of
// 2^13 or 2^14 slots (within noise, 0.4% more instructions on a compile),
// and keeping the memo entries hit since the last collection as roots (a
// flag in the entry, set on a hit): 11% fewer steps on those tests, and the
// re-derivation after a collection all but gone (test 162: 100M steps
// against 98M with no collection at all), but each kept entry roots 8-10
// nodes — 0.6-4M a collection, marked and re-inserted — for 2% less time on
// those tests, and on a compile 350K nodes kept per collection at a 16 MB
// budget, a third slower. See collect_at for the budget policies measured.
//
// Together: 184 instructions a step became 121 on the build's heaviest
// compile, with the same rules and the same output. What remains is the
// memory the tables take: at a 16 MB collection budget the compile phase
// runs 1.5x faster than at 512 MB although it takes 17% more steps, and the
// heaviest expect tests take three times the steps at 32 MB because their
// memo no longer fits — see collect_at and runner.md. Beyond that, jets:
// one for the compiler's identifier scanner, keyed like skip_line, took 7%
// of the instructions off a compile with identical output, and waits on its
// Lean check (a take-while over the 128 ASCII characters).

template <bool JETS> class BasicEagerFastest {
public:
  using Tree = uint32_t;

private:
  // Anonymous memory, reserved rather than committed: a page is only committed
  // the first time it is touched, so a region can be mapped at the most it will
  // ever need and cost what is used of it. (MAP_NORESERVE is not optional: the
  // default overcommit heuristic refuses a 16 GiB reservation on a smaller
  // machine.)
  //
  // Small pages by default. Huge ones (set_hugepages, on the three regions a
  // step reads at random: the arena, the hash-consing table and the memo) spare
  // the TLB and most of the faults, and the build's compile phase ran 13%
  // faster on them — with warm blocks. But a VM whose balloon reports free
  // memory to its host reports whole 2 MiB blocks, exactly what a huge-page
  // fault takes, and the host then backs each one 4 KiB at a time: 9.5 ms a
  // block, and the same phase took 19% longer in the state such a VM is usually
  // in (blocks are reported about two seconds after a process frees them, so no
  // probe at startup can tell a warm machine from a cold one). Hence a switch,
  // off unless the caller knows the machine.
  static void *map_pages(size_t bytes) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    void *mem = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mem == MAP_FAILED) throw std::runtime_error("mmap failed to reserve memory");
    return mem;
  }

  static constexpr size_t ARENA_NODES = size_t(1) << 31;
  static constexpr size_t ARENA_BYTES = ARENA_NODES * 8;

  // Smallest table either side of the memory management ever shrinks to, and
  // the largest the memo is allowed to reach whatever the budget says — the
  // smaller cap applying when no budget was set (see size_memo).
  static constexpr size_t MIN_TABLE = size_t(1) << 12;
  static constexpr size_t MAX_MEMO = size_t(1) << 24;
  static constexpr size_t MIN_MEMO_CAP = size_t(1) << 21;

  // The most groups the hash-consing table can reach: at its load factor
  // (fits), 2^31 nodes take 2^28 groups, 16 GiB.
  static constexpr size_t MAX_GROUPS = size_t(1) << 28;

  // Where a budgeted evaluator's budget starts, in nodes (16 MB of arena): the
  // budget is a locality knob before it is a memory knob. The arena, the
  // hash-consing table and the memo are read at random on every step, and a
  // reduction runs 1.6x faster when the three fit in the cache than when they
  // span the hundreds of MB a build-sized budget lets them (the build's compile
  // phase: 2.1 s at 16 MB against 3.4 s at 512, with 17% more steps). So every
  // reduction begins here, and only one that keeps collecting earns the
  // large budget (see collect_at). At the budget this sizes the memo to
  // 2^18 slots, 3 MB; the memo's size is most of the effect, since it is the
  // table a step reads least locally: at 16 MB with the memo forced to 2^23
  // slots the same phase took 3.4 s again.
  static constexpr size_t START_BUDGET = size_t(1) << 21;

  // A step that resolved in fewer rule applications than this is cheaper to
  // redo than to let its entry evict a slower one from the memo (see the
  // MEMOIZE pop in apply). Low, because a step of a few rules can still be
  // the one asked for most: a compiler's character-class test is asked of
  // every character of its input, and at 16 compile_file of a 5 MB source
  // took twice the steps.
  static constexpr uint32_t MEMO_MIN_STEPS = 4;

  // The step counter apply() keeps is the count shifted up by a tag's width
  // (STEP), so that a MEMOIZE frame's low word is the counter with the tag
  // in its low byte, no shift or mask: pushing one and testing admitted()
  // cost two instructions fewer. stats_counters.steps holds the count itself.
  static constexpr uint64_t STEP = 256;

  /** Whether a step that pushed its MEMOIZE frame when the counter was
   * `pushed` (its low word, tag included) took MEMO_MIN_STEPS rules or more
   * by `steps` now (none for a step a jet answered, which pops the frame in
   * the step that pushed it): the difference's low 24 bits of count. */
  static bool admitted(uint32_t pushed, uint64_t steps) { return uint32_t(steps) - (pushed - MEMOIZE) >= MEMO_MIN_STEPS * STEP; }

  // How many lookups in a row may miss for one function before it stops being
  // looked up at all (see recall).
  static constexpr uint8_t MEMO_COLD = 64;

  struct Node {
    uint32_t u;
    uint32_t v;
  };

  // A node as the one 8-byte word it is, for the step loop: loaded in one,
  // its left child the low half, its right the high — a shift that also
  // leaves "stem" in the flags, where two 4-byte loads shared an address
  // computation and each needed a test (0.8% fewer instructions on the
  // build's heaviest compile; the compiler still keeps a copy of the low
  // half and zero-extends it to index with).
  static uint64_t node_word(const Node *arena, uint64_t at) {
    uint64_t w;
    std::memcpy(&w, arena + at, sizeof w);
    return w;
  }
  static uint32_t left(uint64_t w) { return uint32_t(w); }
  static uint32_t right(uint64_t w) { return w >> 32; }

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
  //   CALL(fn):
  //     when it lands r, begin apply(fn, r).
  //   RETURN(x):
  //     when it lands r, the result is x, and r is dropped.
  //   STEM:
  //     when it lands r, the result is △r.
  //   FORK_WITH(x):
  //     when it lands r, the result is △xr.
  //   FORK_RIGHT(e):
  //     when it lands r, the result is △re — STEM landing on APPLY_TO(e),
  //     fused where STEM would be pushed onto APPLY_TO(e), so that △r, which
  //     only that APPLY_TO would read, is never built. FORK_WITH(△) onto
  //     APPLY_TO(e) likewise pushes nothing and drops the APPLY_TO: △△r
  //     applied to e is r.
  //
  // RETURN, STEM and FORK_WITH are CALL(fn) for an fn that is △△x, △ or △x,
  // and COMPUTE_AND_APPLY(fn, b) for an fn that is △△ or △ (then b, and
  // △b(y b)): the rule such an fn takes needs nothing but r, so it is read off
  // fn when the frame is pushed and applied when it is popped, as one frame
  // rather than one or two steps — and △△b, which only the K rule would read,
  // is never built. None of the four starts a reduction: the frame below gets
  // the result. Measured on the build's heaviest compile: a quarter fewer
  // steps, 2.4% fewer instructions, 9% fewer mispredicted branches.
  //
  // Every argument is a Tree the reduction still needs, which is what makes
  // this stack the collector's root set as well as the VM's.
  //   DONE:
  //     the frame under everything an apply() call pushes, pushed on entry:
  //     popping it is how the call knows it has given back everything it
  //     pushed, so no pop compares the top with the base. Its arguments are 0.
  //
  // APPLY_TO is 0 so that an APPLY_TO frame's whole second word is 0, and
  // the result-only tags are the ones with FORK_WITH's bit, so that one bit
  // of the word tells them from the rest.
  enum FrameTag : uint32_t { APPLY_TO, COMPUTE_AND_APPLY, MEMOIZE, CALL, DONE, FORK_WITH = 8, STEM, RETURN, FORK_RIGHT };

  // A frame is two 8-byte words, written whole and read back whole, because
  // it is usually popped within a few instructions of being pushed: a load
  // that the store buffer cannot answer from one earlier store (a 16-byte
  // Frame read back from its four 4-byte fields, say) waits for those stores
  // to retire instead — a stall on nearly every step.
  //
  // The arguments in the first word, the first in its low half, so that a
  // frame of one argument is that argument zero-extended (memo_key()'s
  // layout, the other way about, costs a shift on every push and pop of
  // such a frame: 1% more instructions); in the second, the tag (a byte), a
  // MEMOIZE frame's step counter at push (24 bits: the MEMO_MIN_STEPS test
  // needs a difference of a few) and, in its high half, the memo hash of
  // (a, b), so that the put when it is popped neither hashes again nor loads
  // anything but the frame. A frame of a constant tag is a store of each
  // word, the second an immediate, and an APPLY_TO frame's second word is 0.
  struct Frame {
    uint64_t lo, hi;
    Frame() = default;
    Frame(FrameTag tag, uint32_t arg1, uint32_t arg2 = 0) : lo(arg1 | uint64_t(arg2) << 32), hi(tag) {}
    /** MEMOIZE(a, b) for a step whose memo_key is `key`, pushed with the
     * (scaled) step counter at `steps`, hashing to `h`. */
    static Frame memoize(uint64_t key, uint32_t steps, uint32_t h) {
      Frame f;
      f.lo = swap_halves(key);
      f.hi = (steps | MEMOIZE) | uint64_t(h) << 32;
      return f;
    }
    FrameTag tag() const { return FrameTag(uint8_t(hi)); }
    uint32_t arg1() const { return uint32_t(lo); }
    uint32_t arg2() const { return lo >> 32; } // 0 where the frame has no second argument, which mark() ignores
    uint64_t key() const { return swap_halves(lo); } // MEMOIZE: memo_key(a, b)
    uint32_t steps() const { return uint32_t(hi); }      // MEMOIZE: low bits of the scaled step counter at push, tag included
    uint32_t meta() const { return hi >> 32; }           // MEMOIZE: low bits of memo_hash(a, b)
  };

  /** The memo's key for apply(a, b): the pair as the one word hash()
   * multiplies, which a lookup then compares in one. 0 for no pair, since no
   * tree is index 0. A MEMOIZE frame holds it with its halves swapped: a
   * rotate at the push and one at the put (see Frame for why the frame is
   * not laid out as the key). */
  static uint64_t memo_key(uint32_t a, uint32_t b) { return uint64_t(a) << 32 | b; }
  static uint32_t key_a(uint64_t key) { return key >> 32; }
  static uint32_t key_b(uint64_t key) { return uint32_t(key); }
  static uint64_t swap_halves(uint64_t x) { return x << 32 | x >> 32; }

  /** One memo entry: apply(a, b) normalizes to r, the key memo_key(a, b) in
   * its first 8 bytes (read and written whole, through memcpy, since the
   * slot is 4-byte aligned: 12 bytes, not 16). A key of 0 marks an empty slot. */
  struct Memo {
    uint32_t key[2];
    uint32_t r;
    uint64_t key_word() const { uint64_t k; std::memcpy(&k, key, 8); return k; }
    void set(uint64_t k, uint32_t result) { std::memcpy(key, &k, 8); r = result; }
  };

  Node *_arena;
  uint32_t _head;      // one past the highest node ever allocated
  Tree _free = 0;      // head of the free list, chained through v
  Tree _newest = ~0u;  // the node alloc() made last, if no collection since
  size_t _live = 0;    // what the last collection found, for sizing the next one
  size_t _budget;      // collect once _head reaches this
  size_t _ceiling;     // what set_budget() was given: the most collect_at raises _budget to on its own
  bool _due = false;   // !_free && _head >= _budget, kept by update_due()
  uint32_t _collections = 0; // by the top-level reduction in progress (see collect_at)

  // What set_limit() allows (0 MB: no limit): the most a collection may raise
  // the budget to, and the most frames the stack may take.
  size_t _limit_mb = 0;
  size_t _max_budget = ARENA_NODES;
  size_t _max_frames = SIZE_MAX;

public:
  // Counters for RUNNER_STATS; incrementing them is noise next to the memory
  // traffic they count, so they are unconditional.
  struct Stats {
    uint64_t steps = 0, memo_hits = 0, memo_puts = 0, gcs = 0, gc_marked = 0;
    uint64_t jets = 0, skipped = 0; // jets taken, and list elements they skipped
  };
  Stats stats_counters;

private:
  // The continuations being unwound, reused across calls: a region mapped
  // once, like the arena, so a push is a store and a bump — no capacity check
  // and no copy — and set_limit() holds it with a limit pointer rather than
  // an allocator. Nested apply() calls take the region above the top they
  // found, so an outer reduction's frames stay where they are — and stay
  // roots — while an inner one runs. apply() keeps its top in a local and
  // writes it to _top before anything that reads the stack (collect).
  static constexpr size_t MAX_FRAMES = size_t(1) << 28; // 4 GiB of frames
  Frame *const _frames = static_cast<Frame *>(map_pages(MAX_FRAMES * sizeof(Frame)));
  Frame *_top = _frames;
  Frame *_frame_limit = _frames + MAX_FRAMES;

  /** Push a frame, or fail the reduction with "out of memory". */
  [[gnu::always_inline]] void push(Frame *&top, Frame *limit, Frame f) const {
    if (EAGER_UNLIKELY(top == limit)) out_of_memory("the stack");
    *top++ = f;
  }
  std::vector<Tree> _roots;
  std::vector<Tree> _grey; // mark stack, reused across collections

  // The collector's marks: a bit per index, set by mark() and cleared again by
  // the sweep, so that between collections every word is 0. Beside the nodes
  // rather than in them (the top bit of the left field, once): the live set is
  // a percent or so of the arena a collection walks — a module's bindings
  // under the garbage of normalizing them — and a bit per node is 64 times
  // denser than the node, so the memo filter's three lookups an entry hit
  // cache where they missed into the arena, and the sweep reads a word to pass
  // 64 nodes, and writes only the dead ones.
  uint64_t *const _marks = static_cast<uint64_t *>(map_pages(ARENA_NODES / 8));

  // Hash-consing: open addressing over node indices, keyed by the (u, v) of the
  // node a slot names, so the keys are the arena.
  //
  // Exact, and everything else here counts on it: two structurally equal trees
  // are one node, hence one index, always — across collections (the table is
  // re-laid from what survived, never dropped), clear(), and everything a
  // caller builds through stem(), fork() or list(). The memo is keyed on it,
  // list() skips searches because of it, and deciding that two trees are equal
  // by comparing their indices is sound only because of it. So no node is ever
  // made by alloc() alone: it is made by a search that came up empty, or where
  // no search could have found one (list(), and intern() over the node made
  // last). A search is the nursery's (below), then — unless a child of the
  // node is in the nursery — this table's, and for the cells of a run (further
  // below), where the cell would be. Every node is in exactly one of the three.
  //
  // A slot is a node index and a control byte: EMPTY, or a tag, 7 bits of the
  // node's hash that the group index does not use, under a set top bit. Slots
  // come in groups of GROUP to a cache line: 16 control bytes, the last 4 of
  // them no slot's, then the 12 indices. A search reads the group its hash
  // picks, compares every tag with its own at once (Word), and loads the node
  // of a slot only where they match: the node it is after, and 1 in 128
  // others. So a shape not built yet costs that line and no arena load, one
  // that was the line and its node, an insertion the line alone. One line,
  // because each line is a miss:
  // with a bare index for a slot, every occupied slot on the way to an empty
  // one was a load of its node, a cache miss of its own behind the one into
  // the table; with the control bytes in an array of their own, every hit and
  // every insertion was two lines. A search ends at the first group with an
  // empty slot: slots are only ever emptied all at once (rebuild_interned), so
  // a node that went in further along found this group full.
  //
  // Both tables are mapped once, at the most they can reach, and re-laid in
  // place: rebuild_interned() reads the arena rather than the old table, so
  // nothing needs the old copy. Memory already touched costs a memset to
  // re-lay; a fresh mapping costs a fault per page all over again — on a VM, a
  // host round trip per page — and holds both copies at once while it grows.
  // Under a budget the table is laid at the size the budget takes it to,
  // and grows between collections only for a list() past the budget
  // (groups_for_budget); without one it doubles from MIN_GROUPS as it fills.
  static constexpr size_t GROUP = 12;
  // EMPTY is 0, the byte a fresh page holds, so the part of the table no re-lay
  // has reached yet needs no memset (rebuild_interned); a tag is 128..255, so
  // that a group's occupancy is the sign bits of its control bytes (empties),
  // and the tag one instruction of the hash (tag_of).
  static constexpr uint8_t EMPTY = 0x00;
  struct alignas(64) Group {
    uint8_t ctrl[16]; // GROUP control bytes, then 4 that lanes() masks off
    Tree at[GROUP];
  };
  static_assert(sizeof(Group) == 64, "a group is a cache line");
  static constexpr size_t MIN_GROUPS = MIN_TABLE * sizeof(Tree) / sizeof(Group); // as many bytes as MIN_TABLE indices
  Group *const _groups = static_cast<Group *>(map_pages(MAX_GROUPS * sizeof(Group)));
  size_t _groups_mask = 0;
  unsigned _groups_shift = 32; // node_hash >> this is the group it picks
  size_t _groups_touched = 0; // groups a re-lay has ever written; above it the mapping is still zero
  size_t _interned_count = 0;
  size_t _interned_room = 0; // the most nodes the table and the nursery may hold together (fits), set by relay_interned

  // How many nodes ahead a loop of insertions asks for the line of the group a
  // node goes in (rebuild_interned, flush_nursery). None of them waits on
  // another, so their misses can overlap, once the line is asked for well
  // before the insertion that needs it rather than left to the out-of-order
  // core: re-laying took 30% less time over the build's test runner.
  static constexpr size_t AHEAD = 16;

  // The nursery: the nodes intern() made since it was last flushed into
  // `_groups`, in a table of their own — 8-byte slots, the node and its
  // node_hash, 0 the empty one — small enough to stay in cache.
  //
  // Where a node can be is what makes it pay, and what keeps it exact. A node
  // is made after its children, and nothing outside the nursery has a child in
  // it: a flush takes all of it at once, so does every re-lay of `_groups`
  // (rebuild_interned, which a collection ends with, before any index it freed
  // is handed out again), list() flushes it before it makes a cell, and
  // intern() puts a node in `_groups` itself only when neither child is in the
  // nursery. So a node with a child in the nursery can only be in the nursery,
  // and a search for one ends there. That is most of what intern() looks for,
  // built on what was just built: over the build's test runner it goes to
  // `_groups` for one node in five, where it went for every one. The nodes
  // made over the one made last (58% of all interning), which need no search,
  // no longer stall the steps after them on the slot they take either.
  //
  // A flush still costs a line of `_groups` a node, but none waits on another:
  // it is a loop over the slots the nursery logged as it filled (`_nursed`),
  // asking for lines AHEAD. It is still the most of anything here, a quarter
  // of the time over that runner at 55-60 ns a node, and what bounds it is the
  // page walk to a line anywhere in up to 512 MB of small pages: AHEAD at 16,
  // 32 or 64 took the same time, and with huge pages under the table it took
  // 33 ns (see map_pages for why not). The same log empties the nursery, a
  // slot at a time: clearing all of it at every flush made a session of 5,000
  // small `bind`s, each of which flushes it, take a quarter longer.
  //
  // It is flushed a quarter full (NURSERY_LOAD). What that sets is the length
  // of its probe chains, which a step waits on: each slot of a chain is a load
  // the next compare and branch depend on, and the branch is the one a
  // predictor cannot learn. Flushed half full it read 1.48 slots a probe on
  // the build's heaviest compile; a quarter full, 1.17, with 7% fewer
  // mispredicted branches in apply() and the compile phase 7-15% faster (two
  // runs, on a shared machine; the heavy expect tests 4-7%), for 2%
  // more searches of `_groups` (a node that would have been found here has
  // been flushed) and 1.5% more nodes flushed (fewer die in it before a
  // collection). An eighth full read 1.07 slots, with 11% fewer mispredicts
  // and 5% more searches, and timed the same within noise. A nursery of half
  // the slots flushed half full — the same nodes a flush, the same chains —
  // timed as the half-full one, so it is the chains and not the count. It is
  // MIN_TABLE slots to begin with, doubled at each flush up to NURSERY:
  // hashed, a few hundred nodes touch every page of it, and at its full size
  // a runner answering one small request faulted in 90 pages more, 5% of its
  // time.
  //
  // `_young` is a byte per index, set for the nodes in the nursery — a byte
  // rather than a bit, so that setting, testing and clearing one is a plain
  // store, load or store rather than a read-modify-write and a shift (6%
  // fewer instructions over the build's heaviest compile; 2 GiB reserved, of
  // which a page per 4096 nodes nursed is touched). Every one of them is at
  // least `_young_lo`, what alloc() would have handed out when the nursery was
  // last emptied: the free list is in ascending order (sweep), and the
  // high-water mark above it. That keeps the bytes of the old nodes, most of
  // the children asked about, out of the cache.
  static constexpr size_t NURSERY = size_t(1) << 16;
  static constexpr size_t NURSERY_LOAD = 4; // flushed at a slot in this many
  struct Nurse {
    Tree at;    // 0: empty
    uint32_t h; // node_hash of at's shape: compared before at's node is loaded, and what a flush inserts by
  };
  Nurse *const _nursery = static_cast<Nurse *>(map_pages(NURSERY * sizeof(Nurse)));
  // AHEAD entries past what a flush can hold, so that its prefetch reads the
  // log past the end without a bounds test: a stale entry is a slot index, a
  // fresh one 0, and the line of a group that no node is going in is a
  // harmless prefetch.
  static constexpr size_t NURSED = NURSERY / NURSERY_LOAD + AHEAD;
  Tree *const _nursed = static_cast<Tree *>(map_pages(NURSED * sizeof(Tree)));
  size_t _nursery_mask = MIN_TABLE - 1;
  size_t _nursery_count = 0;
  size_t _nursery_full = (MIN_TABLE - 1) / NURSERY_LOAD; // flushed past this many nodes: _nursery_mask / NURSERY_LOAD
  uint8_t *const _young = static_cast<uint8_t *>(map_pages(ARENA_NODES));
  Tree _young_lo = 2;

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
  uint32_t _memo_mask = 0; // 32 bits, like the hash it masks: one `and` with a memory operand, no zero-extension
  size_t _memo_cap = 0;  // the bound (see size_memo)
  size_t _memo_room = 0; // puts until it next grows (see memo_put)

  // Per function (the `a` of apply(a, b), by hash), how many of its lookups in
  // a row have missed, up to MEMO_COLD, and past it how many it has skipped
  // since one was last made (see recall). Small on purpose: it is read on every
  // lookup, so it has to stay in cache where the memo cannot.
  // Wrapped, so that a store to a count is not a character store, which
  // would alias every member and have each reloaded after it.
  struct Cold { uint8_t n; };
  Cold _cold[1 << 16];

  // Whether the arena, the table and the memo are to be backed by huge pages
  // (see map_pages): applied to each region when it is mapped, and again to
  // the arena whenever clear() remaps it.
  bool _huge = false;
  void advise_huge(void *mem, size_t bytes) const {
#ifdef MADV_HUGEPAGE
    if (_huge) madvise(mem, bytes, MADV_HUGEPAGE);
#endif
  }

  // jets.hpp's trees, interned on a fresh arena and marked by every collection,
  // so that each stays the index of its tree: interning is exact, so comparing
  // an index with them compares trees. 0, which no tree is, while jets are off.
  Tree _skip_line = 0, _newline = 0;
  bool _jets = JETS;

  static size_t round_up_pow2(size_t n) {
    size_t p = MIN_TABLE;
    while (p < n) p <<= 1;
    return p;
  }

  /**
   * The memo's hash. The pair of indices is the key, and it needs mixing — but
   * every step hashes before it can load anything, so the mixing is on the
   * critical path. One multiply (Fibonacci hashing), with the high half, where
   * a product mixes best, folded onto the low bits the memo indexes by.
   *
   * Which leaves the bits of `u` at and above the memo's index width out of
   * the index: bit 32 + j of the product depends on bits 0..j of u only, so in
   * a memo of 2^k slots (u, v) and (u + 2^k, v) always share a slot, and since
   * the memo is smaller than the arena by design, such pairs evict each other.
   * Folding a 128-bit product instead keeps every bit, and took 0.15% fewer
   * steps on the build's three heaviest tests — in 35.9 s against this hash's
   * 35.4 s. The fold itself is not optional: the high half alone, unfolded,
   * took 0.2% more steps on the build's compile phase and 0.8% more on its
   * heavy tests, for the two instructions it saves.
   */
  static uint64_t hash(uint64_t key) {
    const uint64_t x = key * 0x9e3779b97f4a7c15ULL;
    return x ^ (x >> 32);
  }

  /** The hash-consing table's and the nursery's hash of a node's shape, which
   * only has to spread (a search compares the key): a 32-bit multiply per
   * child, by an immediate, so no 64-bit constant is loaded at every site the
   * way hash()'s is. Three instructions where hash() is seven. The table and
   * the nursery index by its top bits (top_bits), the mixed ones of a
   * product, and the control tag is its low 7 bits — of one product's low
   * bits xor the other's, distinct for consecutive indices. It reads 1.06
   * groups a search on the build's heaviest compile, where the memo's hash
   * read 1.10. 32 bits, so that a nursery slot carries it: a flush then
   * neither reads the arena nor hashes, and a probe compares it before it
   * loads a node. Not the memo's hash: that one's slot must stay hash()'s
   * (see memo_hash). */
  static uint32_t node_hash(uint32_t u, uint32_t v) { return u * 0x9e3779b1u ^ v * 0x85ebca6bu; }
  /** The group or nursery slot a node_hash picks: its top bits. */
  static size_t top_bits(uint32_t h, unsigned shift) { return h >> shift; }
  static unsigned shift_for(size_t slots) { return 32 - unsigned(__builtin_ctzll(slots)); } // slots a power of two

  /** Whether the next step has to collect: no free node, and the high-water
   * mark at the budget. Recomputed wherever one of the three changes, so that
   * a step tests one byte rather than loads all three. */
  void update_due() { _due = !_free && _head >= _budget; }

  /** A node, from the free list or the high-water mark. Not interned. _due
   * was false while the free list had a node, so it is recomputed only once
   * the list runs out, or the high-water mark moves. */
  Tree alloc(uint32_t u, uint32_t v) {
    // update_due() folded in: only the free list running out, or the
    // high-water mark reaching the budget, can change _due here, and both are
    // rare, so a step allocates with a compare rather than a recomputation.
    Tree result = _free;
    if (result) {
      _free = _arena[result].v;
      if (EAGER_UNLIKELY(!_free)) _due = _head >= _budget;
    } else {
      result = _head++;
      if (EAGER_UNLIKELY(_head >= _budget)) _due = true;
    }
    _arena[result] = {u, v};
    return _newest = result;
  }

  // A word of a group's control bytes, compared with one tag all at once, the
  // has-zero-byte way: `match` is the bytes that may hold `tag`, `empty` those
  // that are EMPTY, as a mask with the top bit of each such byte set. Exact for
  // empty. A match can also be a byte holding tag ^ 1 just above one holding
  // tag (the borrow), never an empty one: a candidate the key comparison turns
  // down.
  //
  // Two words a group, on every target. SSE2, which any x86-64 has, compares
  // all 16 bytes in one: 3% fewer instructions and 7% fewer mispredicted
  // branches, but only about 2% less time best of N (2-6% by median, within
  // this VM's spread), since what a search waits on is the line it reads rather
  // than the comparing. Too little, so far, for a Word per target.
  struct Word {
    static constexpr unsigned WIDTH = 8, SHIFT = 3;
    static constexpr uint64_t LSB = 0x0101010101010101ull, MSB = 0x8080808080808080ull;
    uint64_t bytes;
    explicit Word(const uint8_t *at) {
      std::memcpy(&bytes, at, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      bytes = __builtin_bswap64(bytes);
#endif
    }
    uint64_t match(uint8_t tag) const {
      const uint64_t x = bytes ^ (LSB * tag);
      return (x - LSB) & ~x & MSB;
    }
    uint64_t empty() const { return (bytes - LSB) & ~bytes & MSB; } // exact for the lowest
  };

  /** The bits of the mask of the control word at `w` that are slots of a group. */
  static constexpr uint64_t lanes(size_t w) {
    return GROUP - w >= Word::WIDTH ? ~uint64_t(0) : (uint64_t(1) << ((GROUP - w) << Word::SHIFT)) - 1;
  }
  static size_t lane(uint64_t mask) { return size_t(__builtin_ctzll(mask)) >> Word::SHIFT; }
  /** The control byte of a node_hash: its low 7 bits under a set top bit,
   * never EMPTY. One instruction; `t + !t` over 1..127 was three, on every
   * search and insertion. (Word's byte tricks are exact for 0 and borrow the
   * same way whatever the top bit; the set top bit is what lets SSE2's
   * empties() read a group's occupancy straight off a movemask.) */
  static uint8_t tag_of(uint32_t h) { return uint8_t(h) | 0x80; }

  /** Fill slot `i` of `group`, which is empty. */
  static void put(Group &group, size_t i, uint8_t tag, Tree at) {
    group.ctrl[i] = tag;
    group.at[i] = at;
  }

#ifdef __SSE2__
  /** The slots of `group` whose control byte is `b`, as a bit per slot. */
  static unsigned bytes_equal(const Group &group, uint8_t b) {
    const __m128i ctrl = _mm_load_si128(reinterpret_cast<const __m128i *>(group.ctrl));
    // The byte spread over a word first: a movd and a shuffle, where
    // _mm_set1_epi8 of a variable is a movd and three unpacks.
    const __m128i tag = _mm_set1_epi32(int(b * 0x01010101u));
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, tag))) & ((1u << GROUP) - 1);
  }
  /** The empty slots of `group`, as a bit per slot: the control bytes whose
   * top bit is clear, since a tag's is set and EMPTY's clear, read as the
   * group's sign bits — no compare, and the 4 bytes past GROUP are always
   * 0. (As a mask rather than a first index, so that a loop looking for a
   * group with one tests the mask: "first index, or GROUP" was a second
   * compare after the ctz, 2% more instructions on the compile, half in the
   * flush and half in the re-lay.) */
  static unsigned empties(const Group &group) {
    const __m128i ctrl = _mm_load_si128(reinterpret_cast<const __m128i *>(group.ctrl));
    return unsigned(_mm_movemask_epi8(ctrl)) ^ ((1u << GROUP) - 1);
  }
#else
  /** The empty slots of `group`, as a bit per slot. */
  static unsigned empties(const Group &group) {
    unsigned e = 0;
    for (size_t w = 0; w < GROUP; w += Word::WIDTH)
      for (uint64_t m = Word(group.ctrl + w).empty() & lanes(w); m; m &= m - 1) e |= 1u << (w + lane(m));
    return e;
  }
#endif
  /** The first slot of an empties() mask, which must have one. */
  static size_t first(unsigned empties) { return size_t(__builtin_ctz(empties)); }

  /** The node for the shape, if the table has it; else what `miss` makes of
   * the slot it would take, the first empty one of the first group that has
   * one. A continuation rather than a struct of both: returned as a struct,
   * the slot was built and stored on the way out of every search that found
   * its node. */
  template <typename Miss> Tree search(uint32_t h, uint32_t u, uint32_t v, Miss miss) const {
    const uint8_t tag = tag_of(h);
    for (size_t g = top_bits(h, _groups_shift);; g = (g + 1) & _groups_mask) {
      Group &group = _groups[g];
#ifdef __SSE2__
      for (unsigned m = bytes_equal(group, tag); m; m &= m - 1) {
        const Tree at = group.at[__builtin_ctz(m)];
        const Node n = _arena[at];
        if (n.u == u && n.v == v) return at;
      }
#else
      for (size_t w = 0; w < GROUP; w += Word::WIDTH)
        for (uint64_t m = Word(group.ctrl + w).match(tag) & lanes(w); m; m &= m - 1) {
          const Tree at = group.at[w + lane(m)];
          const Node n = _arena[at];
          if (n.u == u && n.v == v) return at;
        }
#endif
      if (const unsigned e = empties(group)) return miss(group, first(e));
    }
  }

  /** Put a live node in the hash-consing table, which must have room for it. */
  void insert_interned(Tree at) {
    const Node n = _arena[at];
    insert_hashed(at, node_hash(n.u, n.v));
    ++_interned_count;
  }

  /** insert_interned for a node whose node_hash is known to be `h`, not counted:
   * a loop of these adds its count once. */
  void insert_hashed(Tree at, uint32_t h) {
    size_t g = top_bits(h, _groups_shift);
    unsigned e;
    while (!(e = empties(_groups[g]))) g = (g + 1) & _groups_mask;
    put(_groups[g], first(e), tag_of(h), at);
  }

  /** Ask for the line of the group `at` goes in, unless it is a swept node. */
  void prefetch_group(Tree at) const {
    const Node n = _arena[at];
    if (n.u) __builtin_prefetch(&_groups[top_bits(node_hash(n.u, n.v), _groups_shift)], 1);
  }

  /** Re-lay the hash-consing table at `groups` from the arena's live nodes
   * outside the run, the nursery's among them, which `groups` must hold: an
   * insertion into a full table looks for an empty slot for ever. Out of line,
   * as is everything else apply() reaches only now and then — end_run,
   * grow_memo, collect: inlined, they made apply() twice the size, and which of
   * its helpers the compiler inlined then hung on any edit to it, for as much
   * as 13% more instructions a step. */
  [[gnu::noinline]] void rebuild_interned(size_t groups) {
    relay_interned(groups, false);
    empty_nursery();
    // A swept node is {0, next-free}: the leaf is the only live node whose left
    // field is 0, and it is at index 1, below where interning starts.
    const auto insert_live = [&](Tree from, Tree to) {
      for (Tree at = std::max(from, Tree(2)); at < to; ++at) {
        if (at + AHEAD < to) prefetch_group(at + AHEAD);
        if (_arena[at].u) insert_interned(at);
      }
    };
    insert_live(2, _run + 1);
    insert_live(_run_end + 1, _head);
  }

  // Lines written whole, past the cache (SSE2's non-temporal stores): a
  // regular store of a line reads it first, and the lines are what the stores
  // then cost; a table re-laid after a collection, or the arena swept, is far
  // more than any cache holds, and what the next thing to touch them wants is
  // not what is there. Below STREAM_MIN a memset, which keeps it in the cache.
  static constexpr size_t STREAM_MIN = size_t(8) << 20;
  static void zero_lines(void *p, size_t bytes) {
#ifdef __SSE2__
    if (bytes >= STREAM_MIN) {
      __m128i *q = static_cast<__m128i *>(p);
      const __m128i z = _mm_setzero_si128();
      for (size_t i = 0, n = bytes / 16; i < n; i += 4) {
        _mm_stream_si128(q + i, z);
        _mm_stream_si128(q + i + 1, z);
        _mm_stream_si128(q + i + 2, z);
        _mm_stream_si128(q + i + 3, z);
      }
      _mm_sfence();
      return;
    }
#endif
    std::memset(p, 0, bytes);
  }

  /** Empty the table at `groups` groups, for a re-lay to fill — past the
   * cache if `stream`: after a collection, where a percent of what was in it
   * goes back in; grown between collections, all of it does, and a table the
   * cache holds is where those inserts find it. */
  void relay_interned(size_t groups, bool stream) {
    // Whole lines, slots and all: a store that writes every byte of a line
    // never has to read it first. Only as far as a re-lay has ever written:
    // above that the mapping is untouched, every byte of it 0 — EMPTY.
    static_assert(EMPTY == 0, "a zeroed line is an empty group");
    const size_t bytes = std::min(groups, _groups_touched) * sizeof(Group);
    if (stream) zero_lines(_groups, bytes); else std::memset(_groups, EMPTY, bytes);
    _groups_touched = std::max(_groups_touched, groups);
    _groups_mask = groups - 1;
    _groups_shift = shift_for(groups);
    _interned_count = 0;
    _interned_room = groups * GROUP * 14 / 15;
  }

  /** Whether `at` is a cell of the run, which the table leaves out. */
  bool in_run_range(Tree at) const { return at > _run && at <= _run_end; }

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
    const uint32_t h = node_hash(u, v);
    if (const Tree at = _nursery[nursery_slot(h, u, v)].at) return at;
    return search(h, u, v, [](Group &, size_t) { return Tree(0); });
  }

  /** Put the run's cells in the table, so that it no longer takes a run to find them. */
  [[gnu::noinline]] void end_run() {
    const size_t groups = capacity_for(_interned_count + _nursery_count + (_run_end - _run));
    if (groups != _groups_mask + 1) rebuild_interned(groups); // which leaves the run out
    for (Tree at = _run + 1; at <= _run_end; ++at) insert_interned(at);
    _run = _run_end = 0;
  }

  /**
   * Whether `groups` hold `nodes`: the load factor, 14/15 of the slots, 11.2
   * nodes a line. That is 5.7 bytes a node, what a table of bare 4-byte slots
   * takes at 0.7, so the table grows at the node counts that one did, and a
   * budget costs no more than it did with it (footprint). A group can be that
   * full where a slot could not, since a search goes on past a group only when
   * all 12 of its slots are taken: over the build's test runner it reads 1.08
   * groups, on single heavy reductions 1.1-1.3. At 7/8 it read 1.04 and
   * 1.05-1.2, and took 2% less time over that runner and up to 5% less on a
   * heavy reduction; but at 6.1 bytes a node the table doubled early, and just
   * under each power of two was twice the size: the build's compile runner
   * peaked at 275 MB rather than 211 MB.
   */
  static bool fits(size_t nodes, size_t groups) { return nodes <= groups * GROUP * 14 / 15; }

  /** Grow the table, if need be, so that the nursery and `more` further nodes
   * fit. Grown, it has room for the run's cells as well, as if they were in it:
   * a reduction that outgrows the table is typically one working through the
   * run, and would otherwise take it through every size its cells' insertion
   * would have skipped, probing fuller tables on the way (5% more instructions
   * compiling a 5 MB source with a compiler that walks every character of it). */
  void reserve_interned(size_t more) {
    const size_t nodes = _interned_count + _nursery_count + more;
    if (EAGER_UNLIKELY(nodes > _interned_room)) rebuild_interned(capacity_for(nodes + (_run_end - _run)));
  }

  /** The smallest table that holds `nodes`. */
  static size_t groups_for(size_t nodes) {
    size_t groups = MIN_GROUPS;
    while (!fits(nodes, groups)) groups *= 2;
    return groups;
  }

  /** The table's size in groups, doubled as often as it takes to hold `nodes`. */
  size_t capacity_for(size_t nodes) const { return std::max(_groups_mask + 1, groups_for(nodes)); }

  /** The table a budget takes: one that holds every node the arena can hold
   * before the next collection, since every one of them is interned. Laid at
   * that size when the budget is set or lowered (set_budget, clear) and when
   * a collection raises it (collect_at), rather than reached by doubling from
   * MIN_GROUPS: each doubling re-inserted everything in the table, about
   * twice the final count in all — on the build's heaviest compile 2.94M
   * insertions in 10 re-lays, 8.8% of its instructions, for a table that
   * ends at exactly this size. An untouched page of the table is empty
   * (EMPTY is 0), so the larger table costs nothing until an insertion lands
   * on a page, and it would have landed on every page of this size before
   * the collection anyway; a reduction that stops short of its budget has
   * touched pages it would not have, which is memory rather than time: a
   * module load that raised its budget to 64 MB and used half of it held
   * 32 MB more, in the same time, and the build's compile and test phases
   * peak at what they peaked at. Without a budget, MIN_GROUPS and doubling,
   * as before: a table sized for the whole arena would be zeroed whole at a
   * re-lay. */
  size_t groups_for_budget() const { return _budget < ARENA_NODES ? groups_for(_budget) : MIN_GROUPS; }

  /** The nursery slot a node_hash picks: its low bits — one `and` with the
   * mask in memory, where the top bits were a shift by a count loaded into
   * cl and the result moved back out (0.5% fewer instructions on the build's
   * heaviest compile, the same probe lengths: the nursery is small, and the
   * low bits of a product by an odd constant are distinct for consecutive
   * indices, which is what its children mostly are). The table keeps the top
   * bits (top_bits). */
  size_t nursery_home(uint32_t h) const { return h & _nursery_mask; }

  /** The slot of the nursery holding the node for this shape, whose hash is
   * `h`, or the empty one it would take. */
  size_t nursery_slot(uint32_t h, uint32_t u, uint32_t v) const {
    for (size_t i = nursery_home(h);; i = (i + 1) & _nursery_mask) {
      const Nurse s = _nursery[i];
      if (!s.at) return i;
      if (s.h != h) continue;
      const Node n = _arena[s.at];
      if (n.u == u && n.v == v) return i;
    }
  }

  /** Whether `at` is in the nursery. */
  bool young(Tree at) const { return at >= _young_lo && _young[at]; }

  /** Put a node no search could find in the nursery's slot `i`, which is empty. */
  Tree nurse(size_t i, uint32_t h, Tree at) {
    _nursery[i] = {at, h};
    _young[at] = 1;
    _nursed[_nursery_count] = Tree(i);
    if (++_nursery_count > _nursery_full) flush_nursery();
    return at;
  }

  /** Take the node out of nursery slot `i`, which holds one. Its `_young`
   * byte stays: see forgotten(). */
  Nurse forget(size_t i) {
    Nurse &slot = _nursery[i];
    const Nurse s = slot;
    slot.at = 0;
    return s;
  }

  /** Forget what is in the nursery, wherever it went. */
  void empty_nursery() {
    for (size_t j = 0; j < _nursery_count; ++j) forget(_nursed[j]);
    forgotten();
  }

  /** The nursery has been emptied: what goes in next is what alloc() hands
   * out next, from here on up — and everything that was in it is below.
   * The free list is ascending and alloc() takes it in order, so an epoch's
   * nodes are either all off the list below its head now, or the list ran
   * out and they are all below the high-water mark. So the `_young` bytes of
   * the nodes flushed are never read again (young() tests the bound first),
   * and a flush leaves them set rather than clearing one per node: 2.5%
   * fewer instructions on the build's heaviest compile. Only a collection
   * moves the bound down, when the sweep rebuilds the free list, and it
   * clears all the bytes up to the high-water mark instead (collect). */
  void forgotten() {
    _young_lo = _free ? _free : _head;
    _nursery_count = 0;
  }

  /** Move the nursery into `_groups` — by a re-lay, if the table has to grow
   * for it, which takes the nursery in with the rest — and double it, if it
   * has not reached NURSERY. */
  [[gnu::noinline]] void flush_nursery() {
    reserve_interned(0); // a re-lay takes the nursery with it, and empties it
    // One pass: a node is inserted and its slot cleared as it goes, rather
    // than the log read again to empty the nursery afterwards. 26
    // instructions a node (was 45): the members in locals (a byte store into
    // `_young` aliased every one of them, and there is none now, see
    // forgotten()), the prefetch unguarded (see _nursed), the group's empty
    // slots one movemask (empties).
    const size_t count = _nursery_count;
    Nurse *const nursery = _nursery;
    Group *const groups = _groups;
    const unsigned shift = _groups_shift;
    const size_t mask = _groups_mask;
    const Tree *const end = _nursed + count;
    for (ptrdiff_t j = -ptrdiff_t(count); j != 0; ++j) {
      __builtin_prefetch(&groups[top_bits(nursery[end[j + ptrdiff_t(AHEAD)]].h, shift)], 1); // past the end: see _nursed
      Nurse &slot = nursery[end[j]];
      const Nurse s = slot;
      slot.at = 0;
      size_t g = top_bits(s.h, shift);
      unsigned e;
      while (!(e = empties(groups[g]))) g = (g + 1) & mask;
      put(groups[g], first(e), tag_of(s.h), s.at);
    }
    _interned_count += count;
    forgotten();
    _nursery_mask = std::min(2 * _nursery_mask + 1, NURSERY - 1);
    _nursery_full = _nursery_mask / NURSERY_LOAD;
  }

  /**
   * The node for this shape: the one that already exists, or a new one.
   *
   * No search when a child is the node made last: a node is made after its
   * children, so nothing can have that one as a child yet (list()'s argument,
   * a cell at a time). That is a quarter to a half of all the nodes a
   * reduction builds — whatever it builds on what it just built. Nor, past the
   * nursery, when a child is in the nursery, which is most of the rest. A new
   * node goes in the nursery, unless the search for it went on into `_groups`:
   * then it takes the slot that search ended on, already in cache.
   */
  [[gnu::always_inline]] Tree intern(uint32_t u, uint32_t v) {
    const uint32_t h = node_hash(u, v);
    if (u == _newest || v == _newest) {
      size_t i = nursery_home(h);
      while (_nursery[i].at) i = (i + 1) & _nursery_mask;
      return nurse(i, h, alloc(u, v));
    }
    const size_t i = nursery_slot(h, u, v);
    if (const Tree at = _nursery[i].at) return at;
    if (young(u) || young(v)) return nurse(i, h, alloc(u, v));
    return intern_old(h, u, v);
  }

  /** intern() past the nursery: the table, the run, or a fresh node in the
   * slot the search ended on. Out of line, like the rest of what a step
   * reaches only now and then. */
  [[gnu::noinline]] Tree intern_old(uint32_t h, uint32_t u, uint32_t v) {
    return search(h, u, v, [&](Group &group, size_t i) { return intern_fresh(u, v, group, i); });
  }

  /** intern_old() past a search that found nothing: the run, or a fresh node
   * in slot `i` of `group`, where the search ended. Its own function, a tail
   * call that takes no hash (three instructions to redo here, one node in six),
   * so that intern_old() calls nothing, keeps nothing live past its search
   * and saves one register (the table's base): with the re-lay this one may
   * run reached from it, the four searches in five that find their node
   * paid a prologue and epilogue of six instructions. */
  [[gnu::noinline]] Tree intern_fresh(uint32_t u, uint32_t v, Group &group, size_t i) {
    if (const Tree cell = in_run(u, v)) return cell;
    const Tree fresh = alloc(u, v);
    put(group, i, tag_of(node_hash(u, v)), fresh);
    ++_interned_count;
    reserve_interned(0);
    return fresh;
  }

  /** The memo's hash of the step's operands: hash()'s low 32 bits, which a
   * slot index comes from (the memo is at most 2^24 slots), and what a
   * MEMOIZE frame carries (Frame::meta). */
  static uint32_t memo_hash(uint64_t key) { return uint32_t(hash(key)); }

  Tree memo_get(uint32_t h, uint64_t key) {
    const Memo &m = _memo[h & _memo_mask];
    if (m.key_word() == key) { ++stats_counters.memo_hits; return m.r; }
    return 0; // no tree is index 0, so 0 is "miss"
  }

  /** Record an entry whose memo_hash is `h`, and double the memo once it has
   * taken as many entries as it has slots, until it reaches its bound. The
   * memo only ever doubles, so the slot `h` picks stays the one hash(a, b)
   * would pick now, whatever it has grown by since `h` was taken. */
  void memo_put(uint32_t h, uint64_t key, uint32_t r) {
    ++stats_counters.memo_puts;
    _memo[h & _memo_mask].set(key, r);
    if (EAGER_UNLIKELY(--_memo_room == 0)) memo_full();
  }

  /** As many puts as the memo has slots since it last grew: grow it, or at
   * its bound, wait — a raised bound lets it grow at the very next put. */
  [[gnu::noinline]] void memo_full() {
    if (size_t(_memo_mask) + 1 < _memo_cap) grow_memo();
    else _memo_room = SIZE_MAX;
  }

  /**
   * Double the memo in place, keeping its entries: at twice the size, an entry
   * hashes to the slot it is in or to the one `half` above it, in the half being
   * added — so no entry moves onto another.
   */
  [[gnu::noinline]] void grow_memo() {
    const size_t half = size_t(_memo_mask) + 1;
    std::fill_n(_memo + half, half, Memo{});
    _memo_mask = uint32_t(2 * half - 1);
    for (Memo *m = _memo; m < _memo + half; ++m)
      if (const uint64_t key = m->key_word(); key && (memo_hash(key) & half)) {
        m[half] = *m;
        *m = Memo{};
      }
    _memo_room = size_t(_memo_mask) + 1;
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
   *
   * The count is below MEMO_COLD at every lookup made: the sampled one puts it
   * back to MEMO_COLD - 1 first, so that a miss counts the same step up as
   * any other, with no clamp — a sampled miss then counts afresh at
   * MEMO_COLD, as before, for four instructions fewer on every miss.
   */
  [[gnu::always_inline]] Tree recall(Tree a, Tree b, Frame *&top, Frame *limit, uint32_t steps) {
    Cold &cold = _cold[cold_index(a)];
    if (cold.n >= MEMO_COLD) {
      if (++cold.n < MEMO_COLD + 16) return 0;
      cold.n = MEMO_COLD - 1;
    }
    const uint64_t key = memo_key(a, b);
    const uint32_t h = memo_hash(key);
    if (const Tree hit = memo_get(h, key)) {
      cold.n = 0;
      return hit;
    }
    ++cold.n;
    push(top, limit, Frame::memoize(key, steps, h));
    return 0;
  }

  /** The function's cold count: the top 16 bits of hash(a, 0), which is (P << 32 | P)
   * for P = a * 0x7f4a7c15 mod 2^32 (the low word of the product is 0, the high
   * one P), so the same 16 bits come out of one 32-bit multiply. */
  static uint32_t cold_index(Tree a) {
    static_assert(sizeof(_cold) / sizeof(Cold) == size_t(1) << 16, "cold_index yields 16 bits");
    return (a * 0x7f4a7c15u) >> 16;
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
      _skip_line = _newline = 0;
      if (_jets) _skip_line = intern_dag(jets::skipLine), _newline = intern_dag(jets::newline);
    }
  }

  /**
   * apply(skip_line, xs) by DropJet's transitions (TreeCalculus/Check.lean):
   * skip each element that is not the newline, and answer what follows the
   * first that is, or the leaf ending the list, recorded as RStep's put for the
   * MEMOIZE frame recall() pushed, if it did: the jet takes no steps, so that
   * frame would not. A stem ending the list has no transition: 0, and xs the
   * stem, for the rules.
   */
  [[gnu::noinline]] Tree skip_line(Tree &xs, const Frame *top) {
    const Tree b = xs;
    ++stats_counters.jets;
    for (Node n; (n = _arena[xs]).u; xs = n.v, ++stats_counters.skipped) {
      if (!n.v) return 0;
      if (n.u == _newline) { xs = n.v; break; }
    }
    if (const uint64_t key = memo_key(_skip_line, b); top[-1].tag() == MEMOIZE && top[-1].key() == key)
      memo_put(memo_hash(key), key, xs);
    return xs;
  }

  /** Whether a collection's mark phase found `at` reachable. Indices 0 and 1
   * are permanent (padding and the shared leaf), so they count as live. */
  bool marked(Tree at) const {
    return at < 2 || (_marks[at >> 6] >> (at & 63) & 1);
  }

  /** Mark everything reachable from x, counting it in _live. Iterative: the
   * live set is deep. */
  void mark(Tree x) {
    _grey.push_back(x);
    while (!_grey.empty()) {
      const Tree at = _grey.back();
      _grey.pop_back();
      // Index 0 is padding and index 1 the shared leaf: neither is ever swept,
      // so neither is marked. It is also what lets an unused frame argument be
      // marked as a plain 0.
      if (at < 2) continue;
      uint64_t &word = _marks[at >> 6];
      const uint64_t bit = uint64_t(1) << (at & 63);
      if (word & bit) continue;
      word |= bit;
      ++_live;
      const Node n = _arena[at];
      _grey.push_back(n.u);
      if (n.v) _grey.push_back(n.v);
    }
  }

  /**
   * Chain every unmarked node onto the free list, put every marked one but the
   * run's cells in the table, which relay_interned() must have emptied with
   * room for them, and clear the marks: one pass over the arena, where it was a
   * sweep and then rebuild_interned's scan of what the sweep had left — each
   * reading every node, for the percent of them that are live.
   *
   * The free list is rebuilt from scratch, so nodes freed by an earlier sweep
   * simply turn up again as unmarked — no separate "already free" state to
   * track. Downwards, so the list comes out in ascending order and allocation
   * walks the arena forwards rather than backwards. A word of marks at a time:
   * a word of none is 64 nodes written without being read, and the nodes of a
   * word with some have the lines of their groups asked for together, before
   * any is inserted — the live nodes are dense where they are (a module's
   * bindings, interned one after another), so that is a batch of up to 64
   * misses in flight, where rebuild_interned's scan keeps AHEAD.
   */
  void sweep_and_insert() {
    Node *const arena = _arena;
    const Tree head = _head;
    Tree free = 0;
    for (size_t w = (head + 63) >> 6; w-- > 0;) {
      const uint64_t live = _marks[w];
      _marks[w] = 0;
      const Tree base = Tree(w << 6);
      const Tree lo = std::max(base, Tree(2)); // 0 and 1 are never swept
      const Tree hi = Tree(std::min<size_t>(base + 64, head));
      if (!live) {
#ifdef __SSE2__
        if (lo == base && hi == base + 64) { // a whole block: 8 lines, streamed
          __m128i *line = reinterpret_cast<__m128i *>(arena + base);
          for (Tree at = base; at < base + 64; at += 2, ++line) // nodes at, at + 1
            _mm_stream_si128(line, _mm_set_epi32(int(at + 2 < base + 64 ? at + 2 : free), 0, int(at + 1), 0));
          free = base;
          continue;
        }
#endif
        for (Tree at = hi; at-- > lo;) { arena[at] = {0, free}; free = at; }
        continue;
      }
      for (uint64_t m = live; m; m &= m - 1) {
        const Tree at = base + Tree(__builtin_ctzll(m));
        if (!in_run_range(at)) prefetch_group(at);
      }
      for (Tree at = hi; at-- > lo;) {
        if (live >> (at - base) & 1) {
          if (!in_run_range(at)) insert_interned(at);
        } else {
          arena[at] = {0, free};
          free = at;
        }
      }
    }
#ifdef __SSE2__
    _mm_sfence();
#endif
    _free = free;
    _newest = ~0u;
  }

  void map_arena() {
    _arena = static_cast<Node *>(map_pages(ARENA_BYTES));
    advise_huge(_arena, ARENA_BYTES);
    _arena[0] = {0, 0}; // index 0 reserved: 0 is the null child sentinel
    _arena[1] = {0, 0}; // the shared leaf
    _head = 2;
    _free = 0;
    _newest = ~0u;
    _live = 0;
    _run = _run_end = 0;
    update_due();
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
   * It follows the budget up as collect_at raises it (raise_budget), so the
   * memo is as small as the arena is until a reduction earns them both.
   */
  void size_memo() {
    _memo_cap = memo_cap(_budget);
    std::fill_n(_memo, MIN_TABLE, Memo{});
    _memo_mask = MIN_TABLE - 1;
    _memo_room = MIN_TABLE;
    std::fill(std::begin(_cold), std::end(_cold), Cold{0});
  }

  static size_t memo_cap(size_t budget) {
    return std::min(round_up_pow2(budget / 8), budget < ARENA_NODES ? MAX_MEMO : MIN_MEMO_CAP);
  }

  /** What a budget of `nodes` holds at most, in bytes: the arena up to it, the
   * hash-consing table and memo at the sizes it takes them to, and a nursery
   * byte for each of the nodes. (The nursery itself is at most 384 KB, like
   * `_cold` not something a reduction grows.) The table at the size the budget
   * takes it to (groups_for), whatever size it is now: set_limit() lowers a
   * budget by this, and the table laid for the larger one follows it down
   * (set_budget) — counted, no limit under its size could ever be met. */
  static size_t footprint(size_t nodes) {
    return nodes * sizeof(Node) + groups_for(nodes) * sizeof(Group) + memo_cap(nodes) * sizeof(Memo) + nodes;
  }

  /** `what` would grow past what set_limit() allows, or with no limit, the arena. */
  [[noreturn, gnu::noinline]] void out_of_memory(const char *what) const {
    throw std::runtime_error(std::string("out of memory: ") + what + " would pass " +
                             (_limit_mb ? "RUNNER_RSS_LIMIT_MB=" + std::to_string(_limit_mb) : "2^31 nodes"));
  }

  /**
   * Collect if the arena has grown past its budget, and raise the budget if that
   * did not leave much room, or if this reduction keeps needing to.
   *
   * Called once per reduction step, which allocates at most one node, so the
   * arena stays within a node of the budget rather than only being tidied up
   * once a request is over. `a` and `b` are the step's own operands: everything
   * else it can still reach is a Frame or a caller's root, but those two are
   * held in registers and have to be named.
   *
   * The budget quadruples at every collection but the first two of a
   * top-level reduction, up to what set_budget() was given. A reduction that
   * collects once or twice is the common one — a binding of a build, a compile
   * of one source — and ran its allocations in START_BUDGET's cache-sized
   * arena; one that collects a third time is a big one, and what it needs is
   * to collect rarely: not for the collections' own cost, but because each
   * drops the memo entries whose results it reaped, and a reduction that asks
   * the same questions again re-derives them. The build's four heaviest tests
   * take 3x the steps at a fixed 32 MB budget that they take at 512, whatever
   * the memo's cap. Growing by a large factor keeps the climb short — three
   * raises from 16 MB to 512, each of its collections dropping entries — and
   * those four tests ran in 30 s against 31 at a fixed 512 MB (and 35 with the
   * budget doubling from its second collection on), with a reduction that
   * never collects three times paying for no memory it does not use. The
   * budget is never lowered: the arena keeps its high-water mark, and
   * allocation below the budget is what a free list over a larger arena
   * cannot give back.
   *
   * Not raised sooner, or by more: raised from the first or second collection
   * the compile phase runs 1.5x or 1.2x slower (its sources lose the 16 MB
   * budget), and x16 or straight to the ceiling the four heaviest tests take
   * 2.5% more or 10% fewer steps but 4-12% longer, the load's third collection
   * having put every test after it on a 256 or 512 MB budget.
   */
  [[gnu::noinline, gnu::cold]] void collect_at(Tree a, Tree b) {
    _roots.push_back(a);
    _roots.push_back(b);
    mark_all();
    _roots.pop_back();
    _roots.pop_back();
    // Growing the budget when the live set turns out to be a large share of it
    // is what keeps a genuinely big term from turning into a collection per
    // allocation. There is nothing left to grow into at the ceiling: the
    // reduction fails, once the sweep has left the arena whole.
    const bool forced = _live * 2 > _budget;
    while (_live * 2 > _budget && _budget < _max_budget) raise_budget(2, _max_budget);
    if (++_collections > 2 && _budget < _ceiling) raise_budget(4, _ceiling);
    // The budget decided first, so that the one re-lay is at the size the
    // raised budget takes the table to (groups_for_budget) — unless the live
    // set forced the raise: that is mostly a run (a 5 MB string bound, say),
    // whose cells never enter the table, and a table laid for the headroom
    // over it is memory committed for nothing (a 128 MB table for 62 nodes,
    // 90 MB more at peak over that bind and light work after it).
    sweep_into(std::max(capacity_for(_live - (_run_end - _run)), forced ? MIN_GROUPS : groups_for_budget()));
    if (_live * 2 > _budget) out_of_memory("the live set");
  }

  /** Multiply the budget by `factor`, to at most `bound`, and let the memo grow
   * to what the larger budget allows. Its entries stay: a collection just
   * filtered them. */
  void raise_budget(size_t factor, size_t bound) {
    _budget = std::min(_budget * factor, bound);
    _memo_cap = memo_cap(_budget);
    if (_memo_room > size_t(_memo_mask) + 1) _memo_room = 1; // the memo was waiting at its bound
  }

public:
  // No collection until set_budget() says so: an unregistered root set makes
  // everything look garbage, so opting in has to be the caller's decision.
  BasicEagerFastest() : _budget(ARENA_NODES), _ceiling(ARENA_NODES) {
    map_arena();
    rebuild_interned(groups_for_budget());
    size_memo();
    intern_jets();
  }

  ~BasicEagerFastest() {
    munmap(_arena, ARENA_BYTES);
    munmap(_groups, MAX_GROUPS * sizeof(Group));
    munmap(_nursery, NURSERY * sizeof(Nurse));
    munmap(_nursed, NURSED * sizeof(Tree));
    munmap(_young, ARENA_NODES);
    munmap(_marks, ARENA_NODES / 8);
    munmap(_memo, MAX_MEMO * sizeof(Memo));
    munmap(_frames, MAX_FRAMES * sizeof(Frame));
  }

  /** Give the stack's pages back, once no reduction is left on it. */
  void release_frames() { madvise(_frames, MAX_FRAMES * sizeof(Frame), MADV_DONTNEED); }

  BasicEagerFastest(const BasicEagerFastest &) = delete;
  BasicEagerFastest &operator=(const BasicEagerFastest &) = delete;

  /** Back the arena, the hash-consing table and the memo with huge pages
   * (see map_pages), off by default. Takes effect on pages not yet touched. */
  void set_hugepages(bool on) {
    _huge = on;
    advise_huge(_arena, ARENA_BYTES);
    advise_huge(_groups, MAX_GROUPS * sizeof(Group));
    advise_huge(_memo, MAX_MEMO * sizeof(Memo));
  }

  /** Whether apply() takes jets, on by default. Off, their trees are not even
   * interned, so it reduces as EagerGraphNilMmap32 does. Clears, as clear(). */
  void set_jets(bool on) {
    _jets = on;
    clear();
  }

  /** Drop everything allocated so far. Every Tree handed out becomes invalid. */
  void clear() {
    Node *old = _arena;
    const uint32_t head = _head;
    map_arena(); // first, so a failed mapping leaves the old arena intact
    munmap(old, ARENA_BYTES);
    zero_lines(_young, (head + 63) & ~size_t(63));
    _top = _frames;
    release_frames();
    _roots.clear();
    _grey.clear();
    _grey.shrink_to_fit();
    release_interned(MIN_GROUPS);
    rebuild_interned(groups_for_budget());
    size_memo();
    intern_jets();
  }

  /** Give back the pages of the table above `groups` groups that a re-lay has
   * touched: they are untouched again, every byte 0, to be faulted in only if
   * a re-lay reaches them (a fresh mapping costs a fault per page; a table
   * that is not coming back should not keep them). */
  void release_interned(size_t groups) {
    if (_groups_touched > groups) {
      madvise(_groups + groups, (_groups_touched - groups) * sizeof(Group), MADV_DONTNEED);
      _groups_touched = groups;
    }
  }

  /** Re-lay the table at `groups` groups, which must hold what it holds,
   * giving back what it had above that. */
  void lay_interned(size_t groups) {
    release_interned(groups);
    rebuild_interned(groups);
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
   * The memo is filtered by the marks (mark_all): an entry any of whose
   * nodes is swept goes, the rest stay.
   */
  [[gnu::noinline]] void collect() {
    mark_all();
    sweep_into(capacity_for(_live - (_run_end - _run)));
  }

  /** The first half of a collection: mark what the roots and the frames
   * reach, filter the memo by it and trim the run to it. Between it and
   * sweep_into() is where collect_at() decides the budget, which the table the
   * sweep re-lays is sized for. */
  void mark_all() {
    _live = 0;
    for (Tree root : _roots) mark(root);
    for (const Frame *f = _frames; f != _top; ++f) { // a reduction in progress is live
      mark(f->arg1());
      mark(f->arg2());
    }
    if constexpr (JETS) mark(_skip_line), mark(_newline);
    // Between mark and sweep is the one moment liveness is written down, which
    // is what lets the memo be filtered rather than dropped: nothing moves, so
    // an entry whose operands and result all survived still says exactly what
    // it said, and an entry any of whose nodes is about to be swept must go —
    // the index may be reused. What the filter keeps is reduction work;
    // re-earning it was the old cost of every collection.
    for (Memo *m = _memo, *const end = _memo + size_t(_memo_mask) + 1; m != end; ++m) {
      const uint64_t key = m->key_word();
      if (!key) continue;
      if (marked(key_a(key)) && marked(key_b(key)) && marked(m->r)) continue;
      *m = Memo{};
    }
    // Swept indices will be reused by other functions.
    std::fill(std::begin(_cold), std::end(_cold), Cold{0});
    while (_run_end > _run && !marked(_run_end)) --_run_end; // what survived of the run
    if (_run_end == _run) _run = _run_end = 0;
    ++stats_counters.gcs;
    stats_counters.gc_marked += _live;
  }

  /** The second half: the table re-laid at `groups`, which must hold what
   * survived outside the run, and the arena swept into it. Re-laid at the size
   * it already had rather than at the size of what survived: the arena keeps
   * its high-water mark, so the free list will fill back up to about here
   * before the next collection, and shrinking now only buys a run of rehashes
   * on the way back. Grown, though, if what survived does not fit: the
   * nursery's nodes go in here too, and nothing made room for them as they
   * went in. */
  void sweep_into(size_t groups) {
    relay_interned(groups, true);
    sweep_and_insert();
    zero_lines(_young, (_head + 63) & ~size_t(63)); // the stale bytes of every flush since: see forgotten()
    empty_nursery();
    update_due();
  }

  /** The arena's high-water mark in nodes, which is what it costs in memory. */
  size_t allocated() const { return _head - 1; } // index 0 is padding, not a node

  /** How many nodes the last collection found reachable. */
  size_t live() const { return _live; }

  /** Collect at least every `nodes` allocations, lowered to what set_limit()
   * allows — and at first every START_BUDGET, the budget raising itself from
   * there as reductions need it (collect_at). 0 never collects, unless a limit
   * says otherwise. */
  void set_budget(size_t nodes) {
    _ceiling = std::min(nodes ? nodes : ARENA_NODES, _max_budget);
    _budget = _ceiling == ARENA_NODES ? ARENA_NODES : std::min(START_BUDGET, _ceiling);
    // The table at the size this budget takes it to, up or down (set_limit
    // lowers one), and never below what it holds.
    const size_t groups = std::max(groups_for_budget(), groups_for(_interned_count + _nursery_count + (_run_end - _run)));
    if (groups != _groups_mask + 1) lay_interned(groups);
    size_memo();
    update_due();
  }

  /**
   * Fail a reduction, with "out of memory", rather than let it hold more than
   * `mb` MiB (0: no limit) in the arena up to its budget, the hash-consing table
   * and memo that budget sizes, and the stack — all a reduction grows. It runs
   * away by a live set that keeps raising the budget, or a stack that never
   * unwinds, so each gets a ceiling, fixed here so that whether a request fits
   * does not depend on what ran before it: for the budget, the largest power of
   * two whose footprint fits in three quarters of the limit (and the budget is
   * lowered to it if above), for the stack, the frames that fit in the rest with
   * room for the copy it is moved into as it grows.
   */
  void set_limit(size_t mb) {
    const size_t bytes = mb ? mb << 20 : SIZE_MAX;
    _limit_mb = mb;
    _max_budget = ARENA_NODES;
    while (footprint(_max_budget) > bytes / 4 * 3) _max_budget /= 2;
    _max_frames = (bytes - footprint(_max_budget)) / sizeof(Frame) / 3 * 2;
    _frame_limit = _frames + std::min(_max_frames, MAX_FRAMES);
    set_budget(_ceiling);
  }

  std::string stats() {
    return std::to_string(allocated()) + " nodes in arena, " +
           std::to_string(_interned_count + _nursery_count) + " shared, " +
           std::to_string(size_t(_memo_mask) + 1) + " memo slots";
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
      // The cells go in the table or the run, where no node may have a child in
      // the nursery, and their heads may be in it.
      if (_nursery_count) flush_nursery();
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
      update_due();
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
  [[gnu::noinline, gnu::no_stack_protector]] Tree apply(Tree a_, Tree b_) {
    // The operands as 64-bit locals: an index that is known to be zero-
    // extended is an addressing mode's index register as it is, where a
    // 32-bit one is a zero-extension and an address computation on every
    // node read.
    uint64_t a = a_, b = b_;
    // The step's hot state lives in locals: the evaluator is a global, and
    // stores into the arena, the tables and the frames may alias its members,
    // so a member would be reloaded on every step. The top is written back
    // before anything that reads the stack (collect_at) and on the way out.
    Frame *const base = _top;
    Frame *top = base;
    Frame *const limit = _frame_limit;
    if (base == _frames) _collections = 0; // a top-level reduction begins
    uint64_t steps = stats_counters.steps * STEP;
    Node *const arena = _arena; // never remapped while a reduction runs
    Tree result;

    try {
      push(top, limit, Frame(DONE, 0)); // popped last: see dispatch
    reduce: // ---- evaluate apply(a, b) ----
      {
        steps += STEP;
        if (EAGER_UNLIKELY(_due)) {
          _top = top;
          stats_counters.steps = steps / STEP;
          collect_at(Tree(a), Tree(b));
        }

        const uint64_t an = node_word(arena, a);

        if (!left(an)) {                                 // apply(△, b) = △b
          result = stem(Tree(b));
          goto dispatch;
        }
        if (!right(an)) {                                // apply(△u, b) = △ub
          result = fork(left(an), Tree(b));
          goto dispatch;
        }

        // a = fork(u, y)
        const Tree y = right(an);
        const uint64_t un = node_word(arena, left(an));

        if (!left(un)) {                                 // apply(△△y, b) = y
          result = y;
          goto dispatch;
        }
        if (EAGER_LIKELY(!right(un))) { // apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
          if (const Tree hit = recall(Tree(a), Tree(b), top, limit, uint32_t(steps))) { result = hit; goto dispatch; }
          // A K-absorber u' answers apply(u', b) without looking at b, so that
          // step is skipped. apply(y, b) is still reduced, as rule (2) says:
          // peek.hpp's S K peek drops it, and with it a reduction that might
          // not terminate, which a runner must not paper over.
          const uint64_t xn = node_word(arena, left(un));
          if (EAGER_LIKELY(right(xn) && !left(node_word(arena, left(xn))))) { // u' = △△x₂: apply(x₂, apply(y, b))
            const uint64_t x2 = node_word(arena, right(xn));
            // top[-1] is at least the DONE frame; an APPLY_TO frame's second word is 0.
            if (!left(x2)) {                                                     //   x₂ = △:   △(y b)
              if (top[-1].hi == APPLY_TO) top[-1].hi = FORK_RIGHT;                //     then △(y b)e
              else push(top, limit, Frame(STEM, 0));
            } else if (!right(x2)) {                                             //   x₂ = △x₃: △x₃(y b)
              if (left(x2) == 1 && top[-1].hi == APPLY_TO) --top;                 //     x₃ = △, then (y b)
              else push(top, limit, Frame(FORK_WITH, left(x2)));
            } else if (!left(node_word(arena, left(x2)))) push(top, limit, Frame(RETURN, right(x2))); // x₂ = △△z: z
            else push(top, limit, Frame(CALL, right(xn)));
          } else if (!left(xn)) {                        // u' = △:  △b(y b)
            push(top, limit, Frame(FORK_WITH, Tree(b)));
          } else if (!right(xn) && !left(node_word(arena, left(xn)))) { // u' = △△: b
            push(top, limit, Frame(RETURN, Tree(b)));
          } else {
            push(top, limit, Frame(COMPUTE_AND_APPLY, left(un), Tree(b)));
          }
          a = y;
          goto reduce;
        }

        // apply(△(△wx)y, b) — triage on b
        const uint64_t bn = node_word(arena, b);

        if (!left(bn)) {                                 //   b = △:  w
          result = left(un);
          goto dispatch;
        }
        if (!right(bn)) {                                //   b = △d: apply(x, d)
          if (const Tree hit = recall(Tree(a), Tree(b), top, limit, uint32_t(steps))) { result = hit; goto dispatch; }
          a = right(un);
          b = left(bn);
          goto reduce;
        }
        {                                                //   b = △de: apply(apply(y, d), e)
          if (const Tree hit = recall(Tree(a), Tree(b), top, limit, uint32_t(steps))) { result = hit; goto dispatch; }
          // After the memo, so that the jet never makes slower what it answers.
          if (JETS && EAGER_UNLIKELY(a == _skip_line)) {
            Tree xs = Tree(b);
            result = skip_line(xs, top);
            b = xs;
            if (result) goto dispatch;
            goto reduce;
          }
          push(top, limit, Frame(APPLY_TO, right(bn)));
          a = y;
          b = left(bn);
          goto reduce;
        }
      }

    dispatch: // ---- feed the result to the pending continuation ----
      // The frames below `base` belong to an apply() further out; this one is
      // done when it pops the DONE frame it pushed on entry, so no pop compares
      // the top with the base.
      for (;;) {
        // Popped over the build's heaviest compile: APPLY_TO 23%, MEMOIZE 20%,
        // FORK_WITH 18%, COMPUTE_AND_APPLY 14%, CALL 13%, STEM 7%, RETURN 4%,
        // and compared in that order, one at a time, on purpose (the asm is a
        // barrier to the compiler's if-to-switch): as a switch, the tag was
        // an indirect jump, mispredicted two times in three.
        //
        // The tests read the frame's second word as it was loaded: whole, or
        // its low byte in place (a MEMOIZE frame's holds more than the tag),
        // never the tag extracted into a register of its own — that was a
        // move and a mask on every pop.
        --top;
        asm("" : "+r"(top)); // one register for the top across the pop: without it, two and a move per pop
        Frame f = *top;
        if (f.hi == APPLY_TO) { // the whole word: see Frame
          // `result` is unreachable from any root for exactly as long as it
          // takes to become `a`, which allocates nothing.
          a = result;
          b = f.arg1();
          goto reduce;
        }
        asm("" : "+r"(f.hi));
        if (f.hi & FORK_WITH) { // the result-only frames: the rule fn takes with r
          // One intern() for the three that build a node (a STEM frame's
          // argument is 0, so △r△ reads △r), chosen by two compares.
          const Tree x = f.arg1();
          Tree u = x, v = result;
          if (f.hi != FORK_WITH) { // the whole word, as for APPLY_TO: these carry nothing but the tag
            if (f.hi == RETURN) { result = x; continue; }
            u = result, v = x;
          }
          result = intern(u, v);
          continue;
        }
        asm("" : "+r"(f.hi));
        if (f.tag() == MEMOIZE) {
          // A tail step (`b = △d`) shares its result with the step it became,
          // so consecutive MEMOIZE frames all record the same normal form.
          // Steps that resolved in a handful of rules are cheaper to redo than
          // to let their entries evict a slower one from the memo.
          if (admitted(f.steps(), steps)) memo_put(f.meta(), f.key(), result);
          continue;
        }
        asm("" : "+r"(f.hi));
        if (f.hi == COMPUTE_AND_APPLY) {
          *top++ = Frame(APPLY_TO, result); // apply(apply(fn, arg), result): into the slot just popped, so no limit test
          a = f.arg1();
          b = f.arg2();
          goto reduce;
        }
        if (f.hi == CALL) {
          a = f.arg1();
          b = result;
          goto reduce;
        }
        break; // DONE: top is back at base
      }
      _top = base;
      stats_counters.steps = steps / STEP;
      return result;
    } catch (...) {
      // An exhausted arena leaves half a reduction on the stack; drop it, so the
      // frames of a request that failed do not stay roots for every later one,
      // and once no reduction is left, the memory they took: a runaway's is most
      // of the limit.
      stats_counters.steps = steps / STEP;
      _top = base;
      if (base == _frames) release_frames();
      throw;
    }
  }
};

using EagerFastest = BasicEagerFastest<false>;
using EagerFastestJets = BasicEagerFastest<true>;
