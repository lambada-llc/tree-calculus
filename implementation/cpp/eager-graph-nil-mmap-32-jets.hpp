#pragma once

#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// The jets of eager-graph-nil-mmap-32.hpp that take more than a loop: what each
// computes, natively, over that evaluator's nodes. The evaluator decides when
// one fires, records its answer in the memo, and keeps its trees alive; this is
// the rest, written against nothing but its arena, stem() and fork(). Index 0,
// which no node is, reads from the arena as △.

namespace jets {

using Tree = uint32_t;

/**
 * `f fuel`, for a function f whose first argument is a fuel: a tree with the
 * fuel in it at fixed places — holes — and the rest the same for every fuel.
 * A jet for f is asked apply(f fuel, x), so it is keyed on this template
 * (key), and finds the fuel by matching it (fuel_of).
 */
class Partial {
public:
  /** Derive the template from the function's tree, applied to two fuels: by
   * the rules, a few hundred steps on every clear(). */
  template <typename E> void key(E &e, Tree function) {
    const Tree zero = e.leaf(), one = e.stem(zero);
    e.roots().insert(e.roots().end(), {function, one});
    _partial = e.apply(function, zero); // a root of the evaluator's from here on
    const Tree other = e.apply(function, one);
    e.roots().resize(e.roots().size() - 2);
    // Where the two agree the template is that tree; where they hold the two
    // fuels it is a hole; anywhere else the fuel is no mere argument.
    _template.assign(1, {});
    std::vector<std::tuple<uint32_t, Tree, Tree>> todo{{0, _partial, other}};
    const auto add = [&](Tree x, Tree y) {
      todo.emplace_back(uint32_t(_template.size()), x, y);
      _template.emplace_back();
      return uint32_t(_template.size() - 1);
    };
    while (!todo.empty()) {
      const auto [i, x, y] = todo.back();
      todo.pop_back();
      if (x == y) {
        _template[i].tree = x;
      } else if (x != zero || y != one) {
        const auto xn = e._arena[x], yn = e._arena[y];
        if (!xn.u || !yn.u || !xn.v != !yn.v) throw std::logic_error("a jet's partial holds the fuel other than as is");
        const uint32_t left = add(xn.u, yn.u), right = xn.v ? add(xn.v, yn.v) : 0;
        _template[i].left = left;
        _template[i].right = right;
      }
    }
  }

  /** The fuel at every hole of the template, if `a` fills it so; else 0. */
  template <typename E> Tree fuel_of(E &e, Tree a) {
    Tree fuel = 0;
    _match.assign(1, {0, a});
    while (!_match.empty()) {
      const auto [i, x] = _match.back();
      _match.pop_back();
      const Pattern p = _template[i];
      if (p.tree) {
        if (x != p.tree) return 0;
      } else if (!p.left) {
        if (fuel && x != fuel) return 0;
        fuel = x;
      } else {
        const auto n = e._arena[x];
        if (!n.u || !n.v != !p.right) return 0;
        _match.push_back({p.left, n.u});
        if (p.right) _match.push_back({p.right, n.v});
      }
    }
    return fuel;
  }

  /** The right child of every partial, if it is the same for every fuel; else 0. */
  Tree right() const { return _template[0].right ? _template[_template[0].right].tree : 0; }
  /** `f △`, which holds every tree the template compares with. */
  Tree partial() const { return _partial; }

private:
  // A tree with holes: `tree` if not 0, else a hole if `left` is 0, else a
  // stem of `left` or a fork of `left` and `right` (indices into _template).
  struct Pattern {
    Tree tree = 0;
    uint32_t left = 0, right = 0;
  };
  std::vector<Pattern> _template;
  std::vector<std::pair<uint32_t, Tree>> _match;
  Tree _partial = 0;
};

/**
 * arboretum's Quoted._normalize (src/quoted/eval.lamb), `_normalize fuel q`:
 * q's normal form in applicative order, a unit of fuel a rule — with
 * _apply_nf and _classify, which it calls. Its tree is
 * `trees/quotedNormalize.dag` in implementation/lean/Cpp. Not proven: no
 * theorem says the answer is the tree's, as runtimeJets_sound says of
 * skip_line's.
 *
 * `_normalize fuel` is △(△x)y with the fuel in x and y the same for every
 * fuel: what apply() compares before the template is matched.
 */
class QuotedNormalize {
public:
  template <typename E> void key(E &e, Tree function) {
    _partial.key(e, function);
    if (!(_y = _partial.right()))
      throw std::logic_error("Quoted._normalize: its partial is no △(△x)y with the fuel in x");
  }

  /** What apply(a, q) normalizes to, if `a` is `_normalize fuel` and the fuel
   * holds out as a Snat; else 0, and the rules take it. */
  template <typename E> Tree answer(E &e, Tree a, Tree q) {
    const Tree fuel = _partial.fuel_of(e, a);
    return fuel ? normalize(e, fuel, q) : 0;
  }

  /** y of △(△x)y, the right of every partial; 0 while unkeyed. */
  Tree y() const { return _y; }
  Tree partial() const { return _partial.partial(); }

private:
  Partial _partial;
  Tree _y = 0;

  // What is left to do, the next on top: normalize x; apply x to y, normal
  // forms both; apply the normal form on top of _values to y, or the two on
  // top to each other; remember x's normal form, on top, if normalizing it
  // spent no fuel, y when it began.
  enum Tag : uint32_t { NORMALIZE, APPLY, APPLY_TO, APPLY_TOP, REMEMBER };
  struct Task {
    Tag tag;
    Tree x, y;
  };
  std::vector<Task> _tasks;
  std::vector<Tree> _values;
  // The normal forms of the subterms that spent no fuel: never having looked
  // at it, they are the same at any fuel. A term shares its subterms, and the
  // rules' memo answers each pair of a fuel and a subterm once; walked as a
  // tree instead, a DAG n deep is 2^n subterms.
  std::unordered_map<Tree, Tree> _normal;

  // x as _classify sees a quoted term (src/reflect/quote.lamb): △, △ p or
  // △ p r, `children` 0, 1 or 2; or 3 for anything else, which in a normal
  // form is an application stuck on a variable.
  struct Quoted {
    uint32_t children;
    Tree p, r;
  };
  template <typename E> static Quoted quoted(E &e, Tree x) {
    const auto n = e._arena[x];
    if (!n.u) return {0, 0, 0};
    if (n.v) {
      const auto h = e._arena[n.u];
      if (!h.u) return {1, n.v, 0};
      if (h.v && !e._arena[h.u].u) return {2, h.v, n.v};
    }
    return {3, 0, 0};
  }

  /**
   * eval.lamb's _normalize, but for the calls it makes, its own and
   * _apply_nf's: those are tasks here, so that how deep they nest costs heap,
   * not C stack. Each is an Option.bind on the one before, so the fuel threads
   * through them in the order they run, and the first none is the answer. A
   * fuel that turns out to be no Snat where a rule needs a unit of it answers
   * 0, for the rules to take from the start.
   */
  template <typename E> Tree normalize(E &e, Tree fuel, Tree q) {
    _tasks.assign(1, {NORMALIZE, q, 0});
    _values.clear();
    _normal.clear();
    const auto pop = [&] {
      const Tree x = _values.back();
      _values.pop_back();
      return x;
    };
    while (!_tasks.empty()) {
      const Task t = _tasks.back();
      _tasks.pop_back();
      Tree a = t.x, b = t.y;
      if (t.tag == NORMALIZE) { // both halves of an application, then the application
        const auto n = e._arena[a];
        if (!n.v) { // △ or a variable
          _values.push_back(a);
        } else if (const auto known = _normal.find(a); known != _normal.end()) {
          _values.push_back(known->second);
        } else {
          _tasks.insert(_tasks.end(),
                        {{REMEMBER, a, fuel}, {APPLY_TOP, 0, 0}, {NORMALIZE, n.v, 0}, {NORMALIZE, n.u, 0}});
        }
        continue;
      }
      if (t.tag == REMEMBER) {
        if (fuel == b) _normal[a] = _values.back();
        continue;
      }
      if (t.tag == APPLY_TOP) b = pop();
      if (t.tag != APPLY) a = pop();
      // _apply_nf: a b. A rule fires on a = △ a1 a2 alone, for a unit of fuel;
      // any other a b is normal as it stands, and so is one stuck on a
      // variable where the rule needs a shape.
      const Quoted qa = quoted(e, a);
      if (qa.children != 2) {
        _values.push_back(e.fork(a, b));
        continue;
      }
      const auto f = e._arena[fuel];
      if (f.v) return 0;
      if (!f.u) return e.leaf(); // none
      fuel = f.u;
      const Quoted q1 = quoted(e, qa.p), qb = quoted(e, b);
      if (q1.children == 0) _values.push_back(qa.r); // △ △ a2 b = a2
      else if (q1.children == 1) // △ (△ x) a2 b = x b (a2 b)
        _tasks.insert(_tasks.end(), {{APPLY_TOP, 0, 0}, {APPLY, qa.r, b}, {APPLY, q1.p, b}});
      else if (q1.children == 3 || qb.children == 3) _values.push_back(e.fork(a, b));
      else if (qb.children == 0) _values.push_back(q1.p); // △ (△ w x) a2 △ = w
      else if (qb.children == 1) _tasks.push_back({APPLY, q1.r, qb.p}); // … (△ u) = x u
      else _tasks.insert(_tasks.end(), {{APPLY_TO, 0, qb.r}, {APPLY, qa.r, qb.p}}); // … (△ u v) = a2 u v
    }
    return e.stem(e.fork(fuel, _values.back()));
  }
};

} // namespace jets
