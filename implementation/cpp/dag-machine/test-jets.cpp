// The eager evaluator's jets (../eager-graph-nil-mmap-32.hpp): skip_line and Quoted._normalize
// answer what the rules do, on lines built to catch the native loop out and on terms at every fuel
// up to one past where it runs out; a jet's answer goes into the memo, a shape the jet has no
// answer for is left to the rules, the jets survive collection and clear(), and off they are not
// there at all. Built and run by test.sh.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../eager-graph-nil-mmap-32.hpp"
#include "../evaluator.hpp"

using J = Evaluator<EagerGraphNilMmap32Jets>;
using Tree = J::Tree;

static int fail = 0;
static void check(const char *what, bool ok) {
  std::printf("%s jets %s\n", ok ? "PASS" : "FAIL", what);
  fail += !ok;
}

// xs, then tail.
template <class E> static Tree list(E &e, std::vector<Tree> xs, Tree tail = 1) {
  for (auto x = xs.rbegin(); x != xs.rend(); ++x) tail = e.fork(*x, tail);
  return tail;
}

// A jets.hpp tree read as the runner reads a module, each line an application, not by the jets'
// reader.
template <class E> static Tree tree(E &e, const char *dag) {
  std::unordered_map<std::string, Tree> env{{"\xe2\x96\xb3", e.leaf()}}; // △
  std::istringstream lines(dag);
  Tree value = 0;
  for (std::string line; std::getline(lines, line);) {
    std::istringstream words(line);
    const std::vector<std::string> w{std::istream_iterator<std::string>(words), {}};
    if (w.size() == 3) env[w[0]] = e.apply(env.at(w[1]), env.at(w[2]));
    else if (w.size() == 1) value = env.at(w[0]);
  }
  return value;
}

// apply(f, x) in e, and the steps it took.
template <class E> static std::pair<Tree, uint64_t> apply(E &e, Tree f, Tree x) {
  const uint64_t before = e.stats_counters.steps;
  const Tree r = e.apply(f, x);
  return {r, e.stats_counters.steps - before};
}

// n as a Snat: △ applied n times to △.
template <class E> static Tree snat(E &e, int n) {
  Tree x = e.leaf();
  while (n--) x = e.stem(x);
  return x;
}

// x's children, 0 for each it lacks: △ is {0, 0}, △u is {u, 0}.
template <class E> static std::pair<Tree, Tree> kids(E &e, Tree x) {
  return e.triage([] { return std::pair<Tree, Tree>{0, 0}; }, [](Tree u) { return std::pair<Tree, Tree>{u, 0}; },
                  [](Tree u, Tree v) { return std::pair{u, v}; }, x);
}

// x of `from`, built in `to`: so that the trees of two evaluators compare by index in one.
template <class E, class F> static Tree copy(E &from, Tree x, F &to) {
  std::unordered_map<Tree, Tree> made{{from.leaf(), to.leaf()}};
  for (std::vector<Tree> todo{x}; !todo.empty();) {
    const Tree t = todo.back();
    const auto [u, v] = kids(from, t);
    if (made.count(t)) todo.pop_back();
    else if (!made.count(u)) todo.push_back(u);
    else if (v && !made.count(v)) todo.push_back(v);
    else made[t] = v ? to.fork(made[u], made[v]) : to.stem(made[u]);
  }
  return made.at(x);
}

// x, but for the first place it differs from y — x holding △ there and y △△ — which is y's.
template <class E> static Tree splice(E &e, Tree x, Tree y, bool &first) {
  if (x == y) return x;
  if (x == e.leaf()) return first ? (first = false, y) : x;
  const auto [xu, xv] = kids(e, x);
  const auto [yu, yv] = kids(e, y);
  const Tree u = splice(e, xu, yu, first);
  return xv ? e.fork(u, splice(e, xv, yv, first)) : e.stem(u);
}

// Pseudo-random quoted terms (src/reflect/quote.lamb: f x is △ f x, a variable △n), `depth` deep
// at most: applications of △, of variables △0 to △2, and of `atoms`, so that rules fire.
struct Terms {
  uint64_t seed;
  std::vector<Tree> atoms;
  uint32_t next(uint32_t n) {
    seed = seed * 6364136223846793005u + 1442695040888963407u;
    return uint32_t(seed >> 33) % n;
  }
  template <class E> Tree operator()(E &e, int depth) {
    if (depth && next(3)) return e.fork((*this)(e, depth - 1), (*this)(e, depth - 1));
    const uint32_t i = next(uint32_t(atoms.size()) + 4);
    if (i < atoms.size()) return atoms[i];
    return i == atoms.size() ? e.leaf() : e.stem(e.of_nat(i - atoms.size() - 1));
  }
};

int main() {
  constexpr size_t N = 100000;
  {
    J e;
    const Tree skip = tree(e, jets::skipLine), nl = e.of_nat(10), a = e.of_nat(97), rest = list(e, {a});
    // The newline is told apart by its index alone: so a line of every other index.
    std::vector<Tree> every;
    for (Tree t = 1; t <= e.allocated(); ++t)
      if (t != nl) every.push_back(t);
    check("skip_line drops a line through its first newline",
          e.apply(skip, e.of_string("no newline")) == e.leaf() &&
              e.apply(skip, e.of_string("\nab\ncd")) == e.of_string("ab\ncd") &&
              e.apply(skip, list(e, every, list(e, {nl, a}))) == rest);
    const Tree x = list(e, std::vector<Tree>(N, a), list(e, {nl, a}));
    e.apply(skip, x);
    const uint64_t hits = e.stats_counters.memo_hits;
    check("the memo answers it again", e.apply(skip, x) == rest && e.stats_counters.memo_hits == hits + 1);
    // DropJet has no transition for a stem; the jet walks the line up to it.
    const uint64_t s = apply(e, skip, list(e, std::vector<Tree>(N, a), e.stem(a))).second;
    check("a stem ending a long line is left to the rules", s > 1 && s < 100);
  }
  {
    // The jets' trees are the evaluator's roots: skip_line and `_normalize fuel` built after a
    // collection with no other root, and after clear() — keying _normalize under a budget it
    // overruns, so that a collection comes in the middle — on slots a lost tree would have left, are
    // still jets, and answer as the rules do.
    J e;
    Evaluator<EagerGraphNilMmap32> m;
    const Tree nm = m.apply(tree(m, jets::quotedNormalize), snat(m, 9));
    Terms terms{3, {}};
    bool fired = true;
    for (int round = 0; round < 2; ++round) {
      if (round) e.set_budget(256), e.clear();
      e.set_budget(4096);
      list(e, std::vector<Tree>(5000, e.of_nat(98))); // garbage past the budget: the next step collects
      e.apply(e.leaf(), e.leaf());
      const Tree a = e.of_nat(97), x = list(e, std::vector<Tree>(N, a), list(e, {e.of_nat(10), a}));
      e.roots().push_back(x);
      const Tree skip = tree(e, jets::skipLine);
      const auto [r, s] = apply(e, skip, x);
      const Tree q = terms(e, 5), normalize = e.apply(tree(e, jets::quotedNormalize), snat(e, 9));
      const uint64_t jets = e.stats_counters.jets;
      const auto [rn, sn] = apply(e, normalize, q);
      fired &= r == list(e, {a}) && s < 10 && sn < 10 && e.stats_counters.jets == jets + 1 &&
               copy(e, rn, m) == m.apply(nm, copy(e, q, m));
    }
    check("the jets survive collection and clear()", fired && e.stats_counters.gcs >= 3);
  }
  {
    // Off, step for step and node for node the evaluator without jets.
    J e;
    Evaluator<EagerGraphNilMmap32> m;
    e.set_jets(false);
    const bool fresh = e.allocated() == m.allocated();
    const Tree se = tree(e, jets::skipLine), sm = tree(m, jets::skipLine);
    const std::string x = std::string(N, 'a') + "\nb";
    Terms terms_e{5, {}}, terms_m{5, {}};
    const Tree ne = e.apply(tree(e, jets::quotedNormalize), snat(e, 40)),
               nm = m.apply(tree(m, jets::quotedNormalize), snat(m, 40));
    check("off, it is the evaluator without jets",
          fresh && apply(e, se, e.of_string(x)) == apply(m, sm, m.of_string(x)) &&
              apply(e, ne, terms_e(e, 6)) == apply(m, nm, terms_m(m, 6)));
  }
  {
    // Quoted._normalize against the rules, the evaluator without jets: each term at every fuel from
    // none up to one past what it takes, so on both sides of where the fuel runs out, as far as
    // MAX_FUEL. The terms: I (I (… z)), two rules an I; Ω = δ δ, which takes every unit there is;
    // and pseudo-random ones, applications of △, variables, K, I, δ and △ (△ w x), which triages.
    constexpr int MAX_FUEL = 48;
    Evaluator<EagerGraphNilMmap32> m;
    // K = △△, I = △(△K)K, δ = △(△I)I, quoted; z a variable.
    const Tree node = m.leaf(), z = m.stem(m.leaf());
    const auto ap2 = [&](Tree f, Tree x, Tree y) { return m.fork(m.fork(f, x), y); };
    const auto triage = [&](Tree w, Tree x) { return m.fork(node, ap2(node, w, x)); };
    const Tree k = m.fork(node, node), i = ap2(node, m.fork(node, k), k);
    const Tree delta = ap2(node, m.fork(node, i), i);
    std::vector<Tree> qs{m.fork(delta, delta)};
    for (Tree q = z; qs.size() < 24;) qs.push_back(q = m.fork(i, q));
    Terms random{1, {k, i, delta, triage(k, i), triage(node, k), triage(i, z)}};
    while (qs.size() < 600) qs.push_back(random(m, 6));
    J e;
    const Tree nm = tree(m, jets::quotedNormalize), ne = tree(e, jets::quotedNormalize);
    std::unordered_set<Tree> seen;
    size_t calls = 0, agree = 0, answered = 0, runs_out = 0, most = 0;
    for (const Tree qm : qs) {
      if (!seen.insert(qm).second) continue;
      const Tree qe = copy(m, qm, e);
      for (int fuel = 0; fuel <= MAX_FUEL; ++fuel) {
        const Tree rm = m.apply(m.apply(nm, snat(m, fuel)), qm);
        const uint64_t jets = e.stats_counters.jets;
        const Tree re = e.apply(e.apply(ne, snat(e, fuel)), qe);
        ++calls;
        agree += copy(e, re, m) == rm;
        answered += e.stats_counters.jets == jets + 1;
        runs_out += rm == m.leaf();
        // some (△ fuel' …): fuel' a stem is fuel to spare, so the fuel no longer runs out.
        const Tree some = kids(m, rm).first, left = some ? kids(m, some).first : 0;
        if (left && kids(m, left).first) {
          most = std::max(most, size_t(fuel - 1));
          break;
        }
      }
    }
    check("Quoted._normalize answers what the rules do, at every fuel to one past where it runs out",
          agree == calls && answered == calls && runs_out > 0 && most >= 20);
    std::printf("     %zu calls, %zu out of fuel; a term took up to %zu rules\n", calls, runs_out, most);
    // Fuel that is no Snat: △ … △ (△△), the fork `depth` units down, for I (I (… z)). The rules' to
    // answer once a rule needs the fork, the jet's before; in an evaluator of its own, so that what
    // the rules remember of it answers nothing the jet is asked of the others.
    J o;
    const Tree no = tree(o, jets::quotedNormalize);
    size_t odd = 0, odd_agree = 0, odd_answered = 0;
    for (size_t n = 1; n < 24; ++n)
      for (int depth = 0; depth < 8; ++depth) {
        Tree fm = m.fork(m.leaf(), m.leaf()), fo = o.fork(o.leaf(), o.leaf());
        for (int d = 0; d < depth; ++d) fm = m.stem(fm), fo = o.stem(fo);
        const uint64_t jets = o.stats_counters.jets;
        const Tree ro = o.apply(o.apply(no, fo), copy(m, qs[n], o));
        ++odd;
        odd_agree += copy(o, ro, m) == m.apply(m.apply(nm, fm), qs[n]);
        odd_answered += o.stats_counters.jets == jets + 1;
      }
    check("fuel that is no Snat is left to the rules once a rule needs it",
          odd_agree == odd && odd_answered > 0 && odd_answered < odd);
    // `_normalize fuel` but for one of the places the fuel is in, which holds another: no partial
    // the rules build, so theirs (which the jet may answer the calls of).
    bool first = true;
    const Tree forged = splice(o, o.apply(no, o.leaf()), o.apply(no, snat(o, 1)), first);
    const auto [rf, sf] = apply(o, forged, copy(m, qs[1], o));
    check("a partial with two fuels in it is left to the rules",
          sf > 10 && copy(o, rf, m) == m.apply(copy(o, forged, m), qs[1]));
    // A DAG of applications of a variable, 40 deep: 2^40 subterms as a tree, normal as it stands.
    Tree dag = e.stem(e.of_nat(7));
    for (int d = 0; d < 40; ++d) dag = e.fork(dag, dag);
    const Tree normalize = e.apply(ne, snat(e, MAX_FUEL));
    check("a term is normalized as the DAG it is",
          apply(e, normalize, dag).first == e.stem(e.fork(snat(e, MAX_FUEL), dag)));
    // The memo answers what the jet answered.
    const Tree q = copy(m, qs[10], e), r = e.apply(normalize, q);
    const uint64_t hits = e.stats_counters.memo_hits;
    check("the memo answers Quoted._normalize again",
          e.apply(normalize, q) == r && e.stats_counters.memo_hits == hits + 1);
  }
  return fail ? 1 : 0;
}
