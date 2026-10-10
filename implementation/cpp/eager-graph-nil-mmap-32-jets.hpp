#pragma once

#include <cstdint>
#include <stdexcept>
#include <tuple>
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
 * arboretum's Quoted._whnf (src/quoted/spine.lamb), `_whnf fuel t`: head
 * reduction of the spine t at its root, a unit of fuel a rule. Its tree is
 * `trees/spineWhnf.dag` in implementation/lean/Cpp. Not proven: no theorem
 * says the answer is the tree's, as runtimeJets_sound says of skip_line's.
 *
 * A call is apply(_whnf fuel, t), and `_whnf fuel` is a tree with the fuel in
 * it — at fixed places, the rest the same for every fuel: so the jet is keyed
 * on that template (key), and finds the fuel by matching it (fuel_of).
 */
class SpineWhnf {
public:
  /** Derive the template from the function's tree, applied to two fuels: by
   * the rules, about 500 steps on every clear(). */
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
        if (!xn.u || !yn.u || !xn.v != !yn.v) throw std::logic_error("Quoted._whnf: its partial holds the fuel other than as is");
        const uint32_t left = add(xn.u, yn.u), right = xn.v ? add(xn.v, yn.v) : 0;
        _template[i].left = left;
        _template[i].right = right;
      }
    }
    // A call's function is △(△x)y, x the same for every fuel: what apply()
    // compares before it matches the rest.
    const auto ln = e._arena[e._arena[_partial].u];
    if (!_template[0].right || !_template[_template[0].left].tree || !ln.u || ln.v)
      throw std::logic_error("Quoted._whnf: its partial is no △(△x)y with the fuel in y");
    _x = ln.u;
  }

  /** What apply(a, t) normalizes to, if `a` is `_whnf fuel` and t is a spine
   * the native loop takes; else 0, and the rules take it. */
  template <typename E> Tree answer(E &e, Tree a, Tree t) {
    const Tree fuel = fuel_of(e, a);
    return fuel ? whnf(e, fuel, t) : 0;
  }

  /** x of △(△x)y, the left of every partial; 0 while unkeyed. */
  Tree x() const { return _x; }
  /** `_whnf △`, which holds every tree the template compares with. */
  Tree partial() const { return _partial; }

private:
  // A tree with holes: `tree` if not 0, else a hole if `left` is 0, else a
  // stem of `left` or a fork of `left` and `right` (indices into _template).
  struct Pattern {
    Tree tree = 0;
    uint32_t left = 0, right = 0;
  };
  std::vector<Pattern> _template;
  Tree _partial = 0, _x = 0;

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
  std::vector<std::pair<uint32_t, Tree>> _match;

  // A reduction waiting on one of its rule's arguments: on a, while a2 is 0;
  // then, rule 3's, on c, with a reduced to a2 = △ △ [x, y, …].
  struct Frame {
    Tree b, c, rest, a2, x, y;
  };
  std::vector<Frame> _frames;
  std::vector<Tree> _args;

  /**
   * spine.lamb's _whnf, but for the calls it makes on a and c: those are
   * frames here, so that how deep they nest — up to the fuel — costs heap,
   * not C stack. A term is a spine △ head args; none is △ and some x is △x;
   * the answer is none once the fuel runs out, else some (△ fuel (△ t' b)).
   * Any shape spine.lamb does not build — a list ending in a stem, fuel that
   * is a fork — answers 0, for the rules to take from the start.
   */
  template <typename E> Tree whnf(E &e, Tree fuel, Tree t) {
    const Tree none = e.leaf();
    Tree blocker;
    _frames.clear();

  reduce: // ---- t's root: a head normal form, or a rule's argument to reduce first ----
    {
      const auto tn = e._arena[t];
      if (!tn.v) return 0; // no spine, or 0 from a push that found none
      if (const auto h = e._arena[tn.u]; h.u) { // a variable is some n, the head itself
        blocker = h.v ? none : tn.u;
        goto dispatch;
      }
      Tree arg[3], rest = tn.v; // △'s a, b, c
      for (Tree &x : arg) {
        const auto r = e._arena[rest];
        if (!r.v) {
          if (r.u) return 0;
          blocker = none; // fewer than three
          goto dispatch;
        }
        x = r.u;
        rest = r.v;
      }
      const auto f = e._arena[fuel];
      if (f.v) return 0;
      if (!f.u) return none;
      fuel = f.u;
      _frames.push_back({arg[1], arg[2], rest, 0, 0, 0});
      t = arg[0];
      goto reduce;
    }

  dispatch: // ---- (fuel, t, blocker) to the frame waiting on it ----
    while (!_frames.empty()) {
      const Frame f = _frames.back();
      _frames.pop_back();
      const Tree a2 = f.a2 ? f.a2 : t, c = f.a2 ? t : f.c;
      if (blocker != none || e._arena[e._arena[t].u].u) { // on a variable, or headed by a fork: stuck
        t = e.fork(none, e.fork(a2, e.fork(f.b, e.fork(c, f.rest))));
        continue;
      }
      const auto as = e._arena[e._arena[t].v]; // a2's [x, y, …], or c's [u, v, …]
      if (as.u && !as.v) return 0;
      const auto as1 = e._arena[as.v]; // △ if as is
      if (as1.u && !as1.v) return 0;
      if (!f.a2) {
        if (!as.u) t = push(e, f.b, f.rest); // rule 1
        else if (!as1.u) { // rule 2
          const Tree bc = push(e, f.b, e.fork(c, none));
          t = bc ? push(e, as.u, e.fork(c, e.fork(bc, f.rest))) : 0;
        } else { // rule 3, which depends on c
          _frames.push_back({f.b, c, f.rest, a2, as.u, as1.u});
          t = c;
        }
      } else if (!as.u) t = push(e, f.x, f.rest); // c = △
      else if (!as1.u) t = push(e, f.y, e.fork(as.u, f.rest)); // c = △u
      else t = push(e, f.b, e.fork(as.u, e.fork(as1.u, f.rest))); // c = △uv
      goto reduce;
    }
    return e.stem(e.fork(fuel, e.fork(t, blocker)));
  }

  /** The spine x, with `more` after its arguments: 0 if x is no spine. */
  template <typename E> Tree push(E &e, Tree x, Tree more) {
    const auto xn = e._arena[x];
    if (!xn.v) return 0;
    _args.clear();
    for (auto n = e._arena[xn.v]; n.u; n = e._arena[n.v]) {
      if (!n.v) return 0;
      _args.push_back(n.u);
    }
    for (auto arg = _args.rbegin(); arg != _args.rend(); ++arg) more = e.fork(*arg, more);
    return e.fork(xn.u, more);
  }
};

} // namespace jets
