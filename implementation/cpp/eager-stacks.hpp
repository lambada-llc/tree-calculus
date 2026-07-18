#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <stdexcept>
#include <sys/mman.h>

// Eager evaluator modeled on the TypeScript `eager-stacks` reducer
// (implementation/typescript/src/evaluator/eager-stacks.mts). Where the other
// C++ backends reduce with a *recursive* apply() over immutable value nodes,
// this one is an explicit-work-list abstract machine:
//
//   * Every term is kept in applicative-spine form. The head is always △, so a
//     node is just "△ applied to a reversed list of argument nodes". A node
//     with fewer than 3 arguments is already a value (leaf / stem / fork); a
//     node with >= 3 arguments has a redex at its head.
//
//   * A `todo` stack holds the nodes that still carry a head redex. reduce_one
//     pops the top node, fires exactly one reduction rule on its head three
//     arguments (rewriting the node *in place*), and — for rule 2 (S) — pushes
//     the freshly created (y z) subterm so it is reduced too. Because that
//     subterm node is shared by identity between the spine and the work-list,
//     it is reduced once and the result is seen everywhere it appears (the
//     call-by-need / DAG sharing the array version gets from object identity).
//
// Representation (this is the part worth modeling): the reversed argument spine
// is a singly-linked list of immutable cons cells, each carrying a length
// counter so a node's arity is O(1):
//
//     Cell { Node* arg; Cell* next; uint32_t len; }   // len = 1 + next.len
//     Node { Cell* spine; }                            // arity = spine ? spine->len : 0
//
// A Node is the only mutable object: reduction just repoints its `spine`. Cells
// are never mutated once built, so they can be *shared*: when a rule splices a
// value's spine onto an empty tail, the value's cell chain is reused as-is (no
// copy). When the tail is non-empty the spliced cells must be copied so the
// last one can continue into the tail — the same O(spliced length) cost the
// array version pays for `s.push(...value)`. Splicing only ever copies a value's
// spine (length <= 2), so in practice a rule allocates a small constant number
// of cells.
//
// The `apply assumes its arguments are already fully reduced` invariant (from
// the reference) is preserved by the driver's left fold and by rule 2 draining
// each (y z) subterm — via LIFO order — before the parent node is revisited, so
// the head argument inspected by a rule is always a value.

class EagerStacks {
public:
  struct Node;
  struct Cell {
    Node *arg;
    Cell *next;
    uint32_t len; // number of cells in the list starting at this one
  };
  struct Node {
    Cell *spine; // reversed argument spine, head = first-applied arg; null = leaf
  };
  using Tree = Node *;

private:
  template <typename T> struct Arena {
    T *base = nullptr;
    size_t head = 0;
    void init(size_t n) {
      void *m = mmap(nullptr, n * sizeof(T), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
      if (m == MAP_FAILED)
        throw std::runtime_error("EagerStacks: mmap failed to reserve arena");
      base = static_cast<T *>(m);
    }
    T *alloc() { return &base[head++]; }
  };

  static constexpr size_t NODE_CAP = size_t(1) << 31; // 2^31 nodes  (~16 GiB reserved)
  static constexpr size_t CELL_CAP = size_t(1) << 31; // 2^31 cells  (~48 GiB reserved)

  Arena<Node> _nodes;
  Arena<Cell> _cells;
  Node *_leaf;
  std::vector<Node *> _todo; // work-list of nodes with a head redex
  std::vector<Node *> _tmp;  // scratch buffer reused by splice()

  Cell *cons(Node *arg, Cell *next) {
    Cell *c = _cells.alloc();
    c->arg = arg;
    c->next = next;
    c->len = 1 + (next ? next->len : 0);
    return c;
  }

  Node *node(Cell *spine) {
    Node *n = _nodes.alloc();
    n->spine = spine;
    return n;
  }

  // Build (src's cells) followed by (tail's cells). When tail is empty the src
  // chain already ends in null, so it is returned unchanged (shared, no copy);
  // otherwise src's cells are copied so the last one continues into tail.
  Cell *splice(Cell *src, Cell *tail) {
    if (!tail)
      return src;
    _tmp.clear();
    for (Cell *c = src; c; c = c->next)
      _tmp.push_back(c->arg);
    Cell *res = tail;
    for (size_t i = _tmp.size(); i-- > 0;)
      res = cons(_tmp[i], res);
    return res;
  }

  static uint32_t arity(Node *n) { return n->spine ? n->spine->len : 0; }

  // Fire one reduction rule on the head of the node currently on top of _todo.
  void reduce_one() {
    Node *s = _todo.back();
    if (arity(s) < 3) { // already a value — drop it
      _todo.pop_back();
      return;
    }
    // s stays on the stack (the reference re-pushes it); read its first three
    // arguments and the remaining tail.
    Cell *c = s->spine;
    Node *x = c->arg;
    Node *y = c->next->arg;
    Node *z = c->next->next->arg;
    Cell *tail = c->next->next->next;

    switch (arity(x)) {
    case 0: // rule 1:  △△y z  ->  y
      s->spine = splice(y->spine, tail);
      break;
    case 1: { // rule 2:  △(△x0)y z  ->  x0 z (y z)
      Node *x0 = x->spine->arg;
      Node *yz = node(splice(y->spine, cons(z, nullptr)));
      s->spine = splice(x0->spine, cons(z, cons(yz, tail)));
      _todo.push_back(yz);
      break;
    }
    default: { // x = fork(w, x'):  rule 3, dispatch on z
      Node *w = x->spine->arg;
      Node *xp = x->spine->next->arg;
      switch (arity(z)) {
      case 0: // 3a:  z = leaf        ->  w
        s->spine = splice(w->spine, tail);
        break;
      case 1: // 3b:  z = stem d      ->  x' d
        s->spine = splice(xp->spine, cons(z->spine->arg, tail));
        break;
      default: // 3c:  z = fork d e    ->  y d e
        s->spine = splice(y->spine,
                          cons(z->spine->arg, cons(z->spine->next->arg, tail)));
        break;
      }
    }
    }
  }

  Tree reduce(Node *s) {
    _todo.push_back(s);
    while (!_todo.empty())
      reduce_one();
    return s;
  }

public:
  EagerStacks() {
    _nodes.init(NODE_CAP);
    _cells.init(CELL_CAP);
    _leaf = node(nullptr);
    _todo.reserve(1 << 16);
    _tmp.reserve(64);
  }

  EagerStacks(const EagerStacks &) = delete;
  EagerStacks &operator=(const EagerStacks &) = delete;

  std::string stats() {
    return std::to_string(_nodes.head) + " nodes, " +
           std::to_string(_cells.head) + " cells";
  }

  Tree leaf() { return _leaf; }
  Tree stem(Tree u) { return node(cons(u, nullptr)); }
  Tree fork(Tree u, Tree v) { return node(cons(u, cons(v, nullptr))); }

  // apply(a, b) = reduce([b, ...a]) : a applied to b, fully normalized.
  // a is a value (arity <= 2) so its spine is copied cheaply; a is untouched.
  Tree apply(Tree a, Tree b) {
    return reduce(node(splice(a->spine, cons(b, nullptr))));
  }

  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case()) {
    Cell *sp = x->spine;
    if (!sp)
      return leaf_case();
    if (!sp->next)
      return stem_case(sp->arg);
    return fork_case(sp->arg, sp->next->arg);
  }
};
