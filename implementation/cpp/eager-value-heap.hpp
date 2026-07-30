#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "reduce-recursive.hpp"

// Eager evaluator with no arena at all: the direct C++ transcription of the
// OCaml implementation's
//
//   type t = Leaf | Stem of t | Fork of t * t
//
// A tree is an individually heap-allocated node carrying a tag and its child
// links; nothing owns a buffer, and nodes are freed as soon as nothing refers
// to them. It is the naive baseline the arena variants are optimizations of --
// EagerValueMem is this same three-variant node with the links turned into
// indices into three parallel vectors that never shrink.
//
// Why the links must be shared_ptr and not unique_ptr or a raw pointer:
//
//   — Reduction shares aggressively. stem(b)/fork(u, b) place their arguments
//     under a new node without copying them, and two of the five rules return
//     a subtree of an input verbatim. So a node genuinely has many owners and
//     no single one of them knows when it is the last; unique ownership cannot
//     express the value graph reduction produces.
//   — Sharing makes the value a DAG, but never a cycle: nodes are immutable
//     and built strictly bottom-up, so a child always predates its parent.
//     Reference counting is therefore complete here -- it cannot leak the way
//     it would on a mutable graph, and no tracing collector is needed. That is
//     exactly the part of OCaml's GC this variant has to reproduce.
//   — A raw `new` with no delete would compile and run fine, but that is just
//     the mmap arena with a slower allocator: memory would only ever grow.
//
// The cost that buys: every child handed to triage is a shared_ptr copy, i.e.
// an atomic increment and decrement per node inspected per reduction step, on
// top of one allocation (and eventually one free) per node built. The arena
// variants pay a pointer bump for the same node and nothing at all to look at
// one. What comes back for it is the only property the eager variants here
// otherwise lack: live memory tracks the value actually reachable, so a long
// reduction runs in bounded space instead of retaining all its garbage.
// (EagerTernaryNilVM32RC buys that back inside an arena, by hand.)
//
// Both halves of that show up on merge-sort of 2000 elements, against
// EagerValueMem -- the same node, arena-allocated -- on the same machine:
// 1.85s vs 0.25s wall, and 25 MB vs 667 MB peak RSS. Reduction here allocates
// ~30x the nodes it ends up holding, and this is the variant that gives them
// back. Cheap enough on memory to outlive the arenas on a long enough run;
// never the one to reach for on speed.
//
// Freeing is recursive, as the OCaml original's is: dropping the last
// reference to a deep tree destroys its spine one stack frame per level. That
// matches the recursion depth ReduceRecursive::apply already needs, so it adds
// no new constraint on the stack.

class EagerValueHeap : public ReduceRecursive<EagerValueHeap> {
public:
  struct Node;
  using Tree = std::shared_ptr<const Node>;

  enum Tag : uint8_t { LEAF, STEM, FORK };

  struct Node {
    Tag tag;
    Tree u; // child of a stem, left child of a fork
    Tree v; // right child of a fork

    Node(Tag tag, Tree u, Tree v) : tag(tag), u(std::move(u)), v(std::move(v)) { ++_live; ++_allocated; }
    ~Node() { --_live; }
  };

private:
  // Nodes outlive no evaluator in particular -- a Tree handed out by one
  // instance stays valid after it dies -- so the accounting is process-wide
  // rather than per-instance, and stats() says so.
  static inline uint64_t _live = 0;
  static inline uint64_t _allocated = 0;

  Tree _leaf;

public:
  EagerValueHeap() : _leaf(std::make_shared<const Node>(LEAF, nullptr, nullptr)) {}

  std::string stats() {
    return std::to_string(_live) + " nodes live, " + std::to_string(_allocated) + " allocated (process-wide)";
  }

  // The leaf is shared rather than reallocated, matching every other backend
  // here (and OCaml, where Leaf is an immediate).
  Tree leaf() {
    return _leaf;
  }

  Tree stem(Tree u) {
    return std::make_shared<const Node>(STEM, std::move(u), nullptr);
  }

  Tree fork(Tree u, Tree v) {
    return std::make_shared<const Node>(FORK, std::move(u), std::move(v));
  }

  // No invariant check, unlike EagerValueMem: there a Tree is an index that
  // could name anything, here the three constructors above are the only way a
  // Node exists at all, so the tag cannot be out of range.
  //
  // x is taken by reference so dispatch itself costs no refcount traffic; the
  // children still cost one copy each, since ReduceRecursive's and Peek's
  // lambdas take them by value.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, const Tree &x)
      -> decltype(leaf_case()) {
    switch (x->tag) {
      case LEAF: return leaf_case();
      case STEM: return stem_case(x->u);
      default:   return fork_case(x->u, x->v);
    }
  }

  // apply() is inherited from ReduceRecursive<EagerValueHeap>.
};
