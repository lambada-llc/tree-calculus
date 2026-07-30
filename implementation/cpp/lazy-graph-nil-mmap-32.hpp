#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>
#include <sys/mman.h>

// Lazy graph reduction over the nil-packed 32-bit mmap representation.
//
// Every other evaluator here is eager: apply(a, b) returns a normal form, so a
// value is only ever built out of values and a node needs no tag beyond the
// nil-packing that EagerTernaryNilMmap32 already uses. That is the right shape
// for running a program against data, and the wrong one for a *module*: a DAG
// bundle names hundreds of terms whose normal form is infinite (every recursive
// definition), and eagerly evaluating each binding as it is read diverges on the
// first one. What such a module needs is head normal form on demand — reduce a
// symbol only as far as someone looks at it — which is what the JavaScript
// `lazy-stacks` evaluator provides and what nothing here did.
//
// So: same representation, lazy reduction.
//
//   {0, 0}          — leaf   △
//   {u, 0}          — stem   △u        (u != 0)
//   {u, v}          — fork   △uv       (u, v != 0)
//   {u | APP, v}    — application u@v, not yet reduced
//
// An application is just a fourth node shape, marked by a tag bit in the left
// field. That costs a bit of address space and nothing else: the node stays 8
// bytes, and discriminating it is the branch the nil-packing already needs.
// (collect() claims a second bit, leaving 2^30 nodes — an 8 GiB arena.)
//
// Reduction is in place. whnf(x) rewrites the node x *is* into the value it
// denotes, so every other node that referenced x sees the result — the sharing a
// DAG is built to express, kept through reduction rather than re-derived. That
// is also what makes laziness affordable: a thunk is a node, forcing it is a
// write to that node, and there is no separate indirection to chase afterwards.
//
// Reduction itself frees nothing — no refcount on any child, no write barrier —
// and what reclaims is a mark-and-sweep over `roots()`, which the caller keeps
// filled because a Tree is a bare index this class cannot see anyone holding.
//
// Not freeing at all is not an option: a single request can allocate a thousand
// times what it keeps, and in-place update means the survivors are a tiny,
// long-lived core (the bundle's own nodes, progressively reduced) around a
// torrent of intermediate spine nodes. But the collector has to be *non-moving*,
// which is what makes it fit here. Reduction is recursive, and its C frames hold
// bare indices into the arena; so do the walks that drive it. Marking asks only
// what is reachable, which those frames already are — every index they hold
// belongs to the term being forced, hence to a root. Relocating, by contrast,
// would need every one of them registered and written back, at which point the
// evaluator's interface stops being an index.

class LazyGraphNilMmap32 {
public:
  using Tree = uint32_t;

private:
  // The two top bits of the left field are tags: APP marks an unreduced
  // application, MARK is set by a collection and cleared before it returns. An
  // index is therefore 30 bits, and the arena is sized to match.
  static constexpr uint32_t APP = 0x80000000u;
  static constexpr uint32_t MARK = 0x40000000u;
  static constexpr uint32_t IDX = 0x3fffffffu;
  static constexpr size_t ARENA_NODES = size_t(1) << 30;
  static constexpr size_t ARENA_BYTES = ARENA_NODES * 8;

  struct Node {
    uint32_t u;
    uint32_t v;
  };

  Node *_arena;
  uint32_t _head;      // one past the highest node ever allocated
  Tree _free = 0;      // head of the free list, chained through v
  size_t _live = 0;    // what the last collection found, for sizing the next one
  size_t _budget;      // collect once _head reaches this
  // The spine being unwound, reused across calls. Recursive whnf() calls nest
  // properly, so each takes the region above the size it found.
  std::vector<Tree> _spine;
  std::vector<Tree> _roots;
  std::vector<Tree> _grey; // mark stack, reused across collections

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

  /** Set MARK on everything reachable from x. Iterative: the live set is deep. */
  void mark(Tree x) {
    _grey.push_back(x);
    while (!_grey.empty()) {
      const Tree at = _grey.back();
      _grey.pop_back();
      // Node 1 is the shared leaf: never swept, so never marked either — a mark
      // left on it would outlive the collection that set it.
      if (at == 1) continue;
      Node &n = _arena[at];
      if (n.u & MARK) continue;
      n.u |= MARK;
      const uint32_t left = n.u & IDX;
      if (n.u & APP) {                 // application: both fields are children
        _grey.push_back(left);
        _grey.push_back(n.v);
      } else if (left) {               // stem or fork
        _grey.push_back(left);
        if (n.v) _grey.push_back(n.v);
      }
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

public:
  // No collection until set_budget() says so: an unregistered root set makes
  // everything look garbage, so opting in has to be the caller's decision.
  LazyGraphNilMmap32() : _budget(ARENA_NODES) { map_arena(); }

  ~LazyGraphNilMmap32() { munmap(_arena, ARENA_BYTES); }

  LazyGraphNilMmap32(const LazyGraphNilMmap32 &) = delete;
  LazyGraphNilMmap32 &operator=(const LazyGraphNilMmap32 &) = delete;

  /** Drop everything allocated so far. Every Tree handed out becomes invalid. */
  void clear() {
    Node *old = _arena;
    map_arena(); // first, so a failed mapping leaves the old arena intact
    munmap(old, ARENA_BYTES);
    _spine.clear();
    _spine.shrink_to_fit();
    _roots.clear();
    _grey.clear();
    _grey.shrink_to_fit();
  }

  /**
   * What a collection treats as live, on top of what those reach.
   *
   * A module evaluator puts every binding here once and never touches it again:
   * nothing moves, so a root stays the index it was. Anything else a caller
   * holds that is *not* reachable from a binding — a freshly marshalled
   * argument, the application about to be reduced against it — belongs here too,
   * for as long as it is held.
   */
  std::vector<Tree> &roots() { return _roots; }

  /** Reclaim every node no root can reach. Indices survive: nothing moves. */
  void collect() {
    for (Tree root : _roots) mark(root);
    for (Tree pending : _spine) mark(pending); // a reduction in progress is live
    sweep();
  }

  /**
   * Collect if the arena has grown past its budget, and raise the budget if that
   * did not leave much room.
   *
   * Called from the reduction loop, so a single long request stays bounded rather
   * than only being tidied up once it is over. Growing the budget when the live
   * set turns out to be a large share of it is what keeps a genuinely big term
   * from turning into a collection per allocation.
   */
  void collect_if_over_budget() {
    if (_free || _head < _budget) return; // room left, either reused or untouched
    collect();
    while (_live * 2 > _budget) _budget *= 2;
  }

  /** The arena's high-water mark in nodes, which is what it costs in memory. */
  size_t allocated() const { return _head - 1; } // index 0 is padding, not a node

  /** How many nodes the last collection found reachable. */
  size_t live() const { return _live; }

  /** Collect at most every `nodes` allocations. 0 never collects. */
  void set_budget(size_t nodes) { _budget = nodes ? nodes : ARENA_NODES; }

  std::string stats() { return std::to_string(allocated()) + " nodes in arena"; }

  Tree leaf() { return 1; }
  Tree stem(Tree u) { return alloc(u, 0); }
  Tree fork(Tree u, Tree v) { return alloc(u, v); }

  /** An application, unreduced: laziness is the whole point of this evaluator. */
  Tree apply(Tree a, Tree b) { return alloc(a | APP, b); }

  /**
   * Rewrite x in place into its head normal form (leaf, stem or fork).
   *
   * Children are left alone — a stem's child may well still be an application —
   * so this terminates on terms whose normal form does not exist, which is the
   * point. Callers that want more force what they look at, one triage at a time.
   *
   * A collection can happen part-way through, so everything this holds has to be
   * reachable from a root while it holds it: x goes on the root stack for the
   * duration, each pending redex stays on the spine until its rewrite is
   * finished, and nothing is allocated across a nested force.
   */
  void whnf(Tree x) {
    if (!(_arena[x].u & APP)) return;
    _roots.push_back(x);
    const size_t base = _spine.size();
    Tree cur = x;
    for (;;) {
      collect_if_over_budget();
      // Unwind the left spine down to the head, which is a value.
      while (_arena[cur].u & APP) {
        _spine.push_back(cur);
        cur = _arena[cur].u & IDX;
      }
      if (_spine.size() == base) break; // head normal form, and it is x's node

      // s stays on the spine until it has been rewritten: it is what keeps cur,
      // b and everything below them reachable while the rules force their parts.
      const Tree s = _spine.back();
      const Tree b = _arena[s].v;
      const Node head = _arena[cur];

      if (head.u == 0) {          // △ @ b = △b
        _arena[s] = {b, 0};
      } else if (head.v == 0) {   // △u @ b = △ u b
        _arena[s] = {head.u, b};
      } else {
        // △ u y @ b — the reduction rules, dispatching on u and then on b.
        const Tree y = head.v;
        whnf(head.u);
        const Node u = _arena[head.u];
        if (u.u == 0) {           // K: △ △ y @ b = y
          whnf(y);
          _arena[s] = _arena[y];
        } else if (u.v == 0) {    // S: △ (△ x) y @ b = (x @ b) (y @ b)
          // Peeking at x, as peek.hpp does for the eager evaluators: the reduct
          // is that application whatever x turns out to be, so looking one level
          // further down lets the common shapes land on their answer directly
          // instead of allocating a redex and unwinding back into it. The one
          // that matters is x = △ △ x2, which absorbs b — two of every five
          // reductions in a real program. See peek.hpp for the full table.
          const Tree x = u.u;
          whnf(x);
          const Node xn = _arena[x];
          const Tree w = xn.u;
          if (w == 0) {                   // x = △        -> △ b (y @ b)
            _arena[s] = {b, apply(y, b)};
            _spine.pop_back();
            cur = s;
            continue;
          }
          bool absorber = false;          // x = △ △ x2, the shape that discards b
          if (xn.v != 0) {
            whnf(w);
            absorber = _arena[w].u == 0;
          }
          if (absorber) {
            // x = △ △ x2: x @ b = x2, so the reduct is x2 @ (y @ b). Forcing x2
            // comes first: r would be unreachable across it, and a collection can
            // happen in there.
            const Tree x2 = xn.v;
            whnf(x2);
            const Node x2n = _arena[x2];
            const Tree r = apply(y, b);
            if (x2n.u == 0)      _arena[s] = {r, 0};          // x2 = △    -> △ r
            else if (x2n.v == 0) _arena[s] = {x2n.u, r};      // x2 = △ d  -> △ d r
            else                 _arena[s] = {x2 | APP, r};   // x2 a fork -> x2 @ r
          } else {
            _arena[s] = {apply(x, b) | APP, apply(y, b)};
          }
        } else {                  // F: △ (△ w x) y @ b — triage on b
          whnf(b);
          const Node arg = _arena[b];
          if (arg.u == 0) {       // b = △     -> w
            whnf(u.u);
            _arena[s] = _arena[u.u];
          } else if (arg.v == 0) { // b = △d    -> x @ d
            _arena[s] = {u.v | APP, arg.u};
          } else {                // b = △ d e -> (y @ d) @ e
            _arena[s] = {apply(y, arg.u) | APP, arg.v};
          }
        }
      }
      _spine.pop_back();
      cur = s; // rewritten: either a value to feed the next argument, or a new redex
    }
    _roots.pop_back();
  }

  // Callables are template parameters (not std::function) so the reduction
  // driving this — to_dag, marshalling, Evaluator's utilities — inlines straight
  // through the dispatch, as it does for the eager backends.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    whnf(x);
    const Node n = _arena[x];
    if (!n.u) return leaf_case();
    if (!n.v) return stem_case(n.u);
    return fork_case(n.u, n.v);
  }
};
