#pragma once

#include <vector>
#include <cstdint>
#include <string>

// Eager evaluator derived from EagerTernaryNilVM32 (fixed-size nil-packed
// nodes, pointer sharing, explicit VM-style evaluation loop with 32-bit
// slots) with reference-counting garbage collection added on top.
//
// Why this baseline: of all the C++ variants it is the only one that combines
// (1) fixed-size nodes, so freed nodes drop onto an O(1) free list and are
// reused verbatim; (2) pointer sharing, so a refcount actually tracks
// something; and (3) an iterative apply() whose explicit continuation stack is
// where per-value ownership can be threaded — and which pairs naturally with
// the iterative dec() below, so neither reduction nor reclamation is bounded
// by the C stack. The vector arena (rather than the mmap one) is the right
// substrate too: RC exists to keep memory bounded, which a never-freeing
// 32 GiB mmap reservation defeats.
//
// Tree representation keeps EagerTernaryNilVM32's nil-packing for structure —
//   c0 == 0            — leaf   (only the shared leaf at index 1)
//   c0 != 0, c1 == 0   — stem(c0)
//   c0 != 0, c1 != 0   — fork(c0, c1)
// — and adds two bookkeeping words per node:
//   rc          — reference count (RC_SAT = pinned, never freed)
//   next_free   — free-list link (meaningful only while the node is free)
// Index 0 is reserved (0 is the null-child sentinel) and index 1 holds the
// shared leaf; both are pinned so inc()/dec() on them are no-ops.
//
// Ownership convention:
//   - leaf(), stem(u), fork(u, v) return an owned reference (rc already +1).
//   - apply(a, b) borrows its arguments (their rc is unchanged on return) and
//     returns an owned reference.
//   - Callers that want reclamation must dec() values they no longer need.
// The Evaluator/test harness drives this class through the plain
// leaf/stem/fork/apply/triage interface and never calls dec(), so the trees it
// builds simply accumulate — but apply()'s own intermediate garbage (the bulk
// of allocation in a reduction-heavy workload) is reclaimed internally as it
// runs, which is the point of the variant.

class EagerTernaryNilVM32RC {
private:
  static constexpr uint32_t RC_SAT = UINT32_MAX; // saturated — never freed

  struct Node {
    uint32_t c0;
    uint32_t c1;
    uint32_t rc;
    uint32_t next_free; // used only while the node is on the free list
  };

  std::vector<Node> _pool;
  std::vector<uint32_t> _dec_stack; // reused across dec() calls
  uint32_t _free_head;
  uint64_t _alloc_count;
  uint64_t _free_count;

  // Frame variants for the apply VM. The `owned` mask tracks which arg fields
  // are owned (inc'd on push, dec'd by the dispatcher when consumed). Subterms
  // of caller-borrowed values can be stored borrowed (no inc) because the
  // caller keeps them alive for the whole apply() call.
  //
  //   APPLY_TO(arg, [arg_owned]):
  //     when the current reduce lands its result r, begin apply(r, arg).
  //   COMPUTE_AND_APPLY(fn, arg, [fn_owned, arg_owned]):
  //     push APPLY_TO(r, owned) then begin apply(fn, arg); when that result
  //     lands, the APPLY_TO frame applies it to the earlier r.
  enum FrameTag : uint8_t { APPLY_TO, COMPUTE_AND_APPLY };

  static constexpr uint8_t ARG1_OWNED = 0x1;
  static constexpr uint8_t ARG2_OWNED = 0x2;

  struct Frame {
    FrameTag tag;
    uint8_t owned;
    uint32_t arg1;
    uint32_t arg2; // unused for APPLY_TO
  };

  // Raw allocation: pulls a slot off the free list (verbatim reuse) or grows
  // the pool. Children are stored as-is; callers own the inc discipline.
  uint32_t alloc_node(uint32_t c0, uint32_t c1) {
    uint32_t idx;
    if (_free_head != UINT32_MAX) {
      idx = _free_head;
      _free_head = _pool[idx].next_free;
    } else {
      idx = static_cast<uint32_t>(_pool.size());
      _pool.push_back({});
    }
    Node &n = _pool[idx];
    n.c0 = c0;
    n.c1 = c1;
    n.rc = 1;
    _alloc_count++;
    return idx;
  }

  void inc(uint32_t idx) {
    Node &n = _pool[idx];
    if (n.rc == RC_SAT) return;
    n.rc++;
  }

  // Iterative dec so a deep tree's cascade of frees never blows the C stack.
  void dec(uint32_t idx) {
    if (idx == 0) return; // reserved null-child sentinel
    _dec_stack.push_back(idx);
    while (!_dec_stack.empty()) {
      uint32_t i = _dec_stack.back();
      _dec_stack.pop_back();
      Node &n = _pool[i];
      if (n.rc == RC_SAT) continue; // pinned (leaf / reserved)
      if (--n.rc != 0) continue;    // still referenced elsewhere
      uint32_t c0 = n.c0, c1 = n.c1;
      n.next_free = _free_head;
      _free_head = i;
      _free_count++;
      if (c0) _dec_stack.push_back(c0);
      if (c1) _dec_stack.push_back(c1);
    }
  }

public:
  using Tree = uint32_t;

  EagerTernaryNilVM32RC()
      : _free_head(UINT32_MAX), _alloc_count(0), _free_count(0) {
    _pool.push_back({0, 0, RC_SAT, 0}); // index 0 reserved: null-child sentinel
    _pool.push_back({0, 0, RC_SAT, 0}); // index 1: shared leaf, pinned
  }

  std::string stats() {
    return std::to_string(_alloc_count) + " allocs, " +
           std::to_string(_free_count) + " frees, " +
           std::to_string(_alloc_count - _free_count) + " live, " +
           std::to_string(_pool.size()) + " pool size";
  }

  Tree leaf() {
    return 1;
  }

  Tree stem(Tree u) {
    inc(u);
    return alloc_node(u, 0);
  }

  Tree fork(Tree u, Tree v) {
    inc(u);
    inc(v);
    return alloc_node(u, v);
  }

  // Callables are template parameters (not std::function) so Evaluator's triage
  // uses (parse/print) inline; this VM keeps its own iterative apply() below.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    Node n = _pool[x];
    if (n.c0 == 0) return leaf_case();
    if (n.c1 == 0) return stem_case(n.c0);
    return fork_case(n.c0, n.c1);
  }

  // Iterative VM-style apply with refcounting. a and b are borrowed (their rc
  // is unchanged on return); the returned reference is owned.
  //
  // Ownership invariant, held on every (re)entry to `reduce`:
  //   pa != 0  <=>  pa == a and `a` is an owned intermediate this call must
  //                 release; pa == 0 means `a` is borrowed (kept alive by the
  //                 caller, or by an owned frame arg) and must NOT be dec'd.
  //   likewise pb / `b`.
  // Because pa/pb always name the *current* a/b exactly, the transfer and
  // sole-owner-reuse fast paths below can trust `pa`/`pb` to mean "I solely own
  // this exact node." Whenever a rule descends into a subterm of an owned a/b,
  // it inc()s the subterm(s) it keeps and dec()s the parent right away, so the
  // invariant is re-established before the next `reduce`. (This is the key
  // divergence from a deferred-dec scheme, where pa/pb would drift to naming an
  // ancestor and the transfer path would steal a refcount it doesn't hold.)
  Tree apply(Tree a, Tree b) {
    std::vector<Frame> stack;
    uint32_t result = 0;
    uint32_t pa = 0; // if nonzero, == a and a is an owned intermediate
    uint32_t pb = 0; // if nonzero, == b and b is an owned intermediate

  reduce: // ---- evaluate apply(a, b) ----
    {
      uint32_t a_c0 = _pool[a].c0;
      uint32_t a_c1 = _pool[a].c1;

      if (a_c0 == 0) {                                    // apply(△, b) = △b
        // Own b? Move its ref straight into the new stem instead of inc'ing in
        // stem() and dec'ing again at dispatch.
        if (pb) { result = alloc_node(b, 0); pb = 0; }
        else    { result = stem(b); }
        goto dispatch; // a is leaf: dec(pa) at dispatch is a pinned no-op
      }

      if (a_c1 == 0) {                                    // apply(△u, b) = △ub
        uint32_t u = a_c0;
        // Sole-owner reuse: we own this exact stem and nobody else holds it, so
        // rewrite it in place — stem(u) becomes fork(u, b). u stays in c0 (its
        // ref moves from the stem to the fork, net zero).
        if (pa && _pool[a].rc == 1) {
          _pool[a].c1 = b;
          if (!pb) inc(b);   // borrowed b: the fork needs its own ref
          result = a;        // else: b's owned ref transfers into c1
          pa = 0; pb = 0;
          goto dispatch;
        }
        // Fresh fork. u is a subterm of a (which survives or is shared), so the
        // fork needs its own ref to it. Transfer b if owned, else inc it.
        inc(u);
        if (pb) { result = alloc_node(u, b); pb = 0; }
        else    { inc(b); result = alloc_node(u, b); }
        goto dispatch; // dec(pa) at dispatch releases a if it was owned
      }

      // a = fork(u, y)
      uint32_t u = a_c0;
      uint32_t y = a_c1;
      uint32_t u_c0 = _pool[u].c0;
      uint32_t u_c1 = _pool[u].c1;

      if (u_c0 == 0) {                                    // apply(△△y, b) = y
        // Sole-owner shortcut: free `a` alone (do not cascade into y) and let
        // y's ref transfer out of a into the result.
        if (pa && _pool[a].rc == 1) {
          _pool[a].next_free = _free_head;
          _free_head = a;
          _free_count++;
          result = y;
          pa = 0;
          goto dispatch; // dec(pb) at dispatch releases b if it was owned
        }
        inc(y);          // a survives (shared or borrowed): result needs its ref
        result = y;
        goto dispatch;
      }

      if (u_c1 == 0) { // apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
        uint32_t u_inner = u_c0;
        // Reduce apply(y, b) now; defer apply(u', b) on the stack. b is used by
        // both, so if we own b the deferred use needs its own ref.
        uint8_t flags = 0;
        if (pa) { inc(u_inner); flags |= ARG1_OWNED; } // deferred use keeps u'
        if (pb) { inc(b);       flags |= ARG2_OWNED; } // ...and its own copy of b
        stack.push_back({COMPUTE_AND_APPLY, flags, u_inner, b});
        // Descend a -> y. If we owned a, keep y and release a now so pa again
        // names exactly the current a. b is unchanged, so pb still names it.
        if (pa) { inc(y); dec(a); pa = y; }
        a = y;
        goto reduce;
      }

      // apply(△(△wx)y, b) — triage on b
      uint32_t w = u_c0;
      uint32_t x = u_c1;
      uint32_t b_c0 = _pool[b].c0;
      uint32_t b_c1 = _pool[b].c1;

      if (b_c0 == 0) {                                    //   b = △:  w
        inc(w);          // result needs its own ref to w (a subterm of a)
        result = w;
        goto dispatch;   // dec(pa)/dec(pb) at dispatch release a/b if owned
      }
      if (b_c1 == 0) {                                    //   b = △d: apply(x, d)
        uint32_t d = b_c0;
        // Descend a -> x and b -> d, keeping each subterm and releasing its
        // owned parent so pa/pb again name exactly the new a/b.
        if (pa) { inc(x); dec(a); pa = x; }
        if (pb) { inc(d); dec(b); pb = d; }
        a = x;
        b = d;
        goto reduce;
      }
      // b = △de: apply(apply(y, d), e)
      uint32_t d = b_c0;
      uint32_t e = b_c1;
      // Reduce apply(y, d) now; defer apply(_, e) on the stack. If we own b,
      // e outlives b's release, so give the frame its own ref first.
      uint8_t flags = 0;
      if (pb) { inc(e); flags |= ARG1_OWNED; }
      stack.push_back({APPLY_TO, flags, e, 0});
      if (pa) { inc(y); dec(a); pa = y; }
      if (pb) { inc(d); dec(b); pb = d; }
      a = y;
      b = d;
      goto reduce;
    }

  dispatch: // ---- release consumed inputs, then step the continuation stack ----
    if (pa) { dec(pa); pa = 0; }
    if (pb) { dec(pb); pb = 0; }

    if (stack.empty()) return result;
    Frame f = stack.back(); stack.pop_back();
    switch (f.tag) {
      case APPLY_TO: // apply(result, f.arg1)
        a = result;
        pa = result; // a freshly-computed result is always owned
        b = f.arg1;
        pb = (f.owned & ARG1_OWNED) ? f.arg1 : 0;
        goto reduce;

      case COMPUTE_AND_APPLY:
        // Compute apply(f.arg1, f.arg2) first; when that lands, the APPLY_TO
        // frame applies it to the previously-computed `result`.
        stack.push_back({APPLY_TO, ARG1_OWNED, result, 0});
        a = f.arg1;
        pa = (f.owned & ARG1_OWNED) ? f.arg1 : 0;
        b = f.arg2;
        pb = (f.owned & ARG2_OWNED) ? f.arg2 : 0;
        goto reduce;
    }
    __builtin_unreachable();
  }
};
