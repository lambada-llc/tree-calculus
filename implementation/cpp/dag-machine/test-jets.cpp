// The eager evaluator's jets (../eager-graph-nil-mmap-32.hpp): skip_line and Quoted._whnf answer
// what the rules do, on lines built to catch the native loop out and on spines at every fuel up to
// one past where it runs out; a jet's answer goes into the memo, a shape the jet has no answer for
// is left to the rules, the jets survive collection and clear(), and off they are not there at
// all. Built and run by test.sh.

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

// Pseudo-random spines △ head args, `depth` deep at most: mostly headed by △, with up to five
// arguments, so that rules fire; sometimes by a variable △n. `odd` adds what spine.lamb never
// builds: a leaf or a stem for a spine, a list of arguments ending in a stem.
struct Spines {
  uint64_t seed;
  uint32_t next(uint32_t n) {
    seed = seed * 6364136223846793005u + 1442695040888963407u;
    return uint32_t(seed >> 33) % n;
  }
  template <class E> Tree operator()(E &e, int depth, bool odd) {
    if (odd && !next(16)) return next(2) ? e.leaf() : e.stem(e.leaf());
    const Tree head = next(8) ? e.leaf() : e.stem(e.of_nat(next(3)));
    std::vector<Tree> args(depth ? next(6) : 0);
    for (Tree &a : args) a = (*this)(e, depth - 1, odd);
    return e.fork(head, list(e, args, odd && !next(16) ? e.stem(e.leaf()) : e.leaf()));
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
    // The jets' trees are the evaluator's roots: skip_line and `_whnf fuel` built after a collection
    // with no other root, and after clear() — keying _whnf under a budget it overruns, so that a
    // collection comes in the middle — on slots a lost tree would have left, are still jets, and
    // answer as the rules do.
    J e;
    Evaluator<EagerGraphNilMmap32> m;
    const Tree wm = m.apply(tree(m, jets::spineWhnf), snat(m, 9));
    Spines spines{3};
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
      const Tree t = spines(e, 3, false), whnf = e.apply(tree(e, jets::spineWhnf), snat(e, 9));
      const uint64_t jets = e.stats_counters.jets;
      const auto [rw, sw] = apply(e, whnf, t);
      fired &= r == list(e, {a}) && s < 10 && sw < 10 && e.stats_counters.jets == jets + 1 &&
               copy(e, rw, m) == m.apply(wm, copy(e, t, m));
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
    Spines spines_e{5}, spines_m{5};
    const Tree we = e.apply(tree(e, jets::spineWhnf), snat(e, 40)), wm = m.apply(tree(m, jets::spineWhnf), snat(m, 40));
    check("off, it is the evaluator without jets",
          fresh && apply(e, se, e.of_string(x)) == apply(m, sm, m.of_string(x)) &&
              apply(e, we, spines_e(e, 4, false)) == apply(m, wm, spines_m(m, 4, false)));
  }
  {
    // Quoted._whnf against the rules, the evaluator without jets: each spine at every fuel from
    // none up to one past what it takes, so on both sides of where the fuel runs out, as far as
    // MAX_FUEL. The spines: pseudo-random ones; I (I (… z)), two rules an I; Ω = δ δ, which never
    // stops; and pseudo-random ones with odd shapes, which the jet must leave to the rules or
    // answer as they do — in an evaluator of their own, so that what the rules remember of them
    // answers nothing the jet is asked of the others.
    constexpr int MAX_FUEL = 48;
    Evaluator<EagerGraphNilMmap32> m;
    // △ and z as spines, and f x as one: K = △△, I = △(△K)K, δ = △(△I)I.
    const Tree node = m.fork(m.leaf(), m.leaf()), z = m.fork(m.stem(m.leaf()), m.leaf());
    const auto app = [&](Tree f, Tree x) { return m.fork(kids(m, f).first, list(m, m.to_list(kids(m, f).second), list(m, {x}))); };
    const auto ap2 = [&](Tree f, Tree x, Tree y) { return app(app(f, x), y); };
    const Tree k = app(node, node), i = ap2(node, app(node, k), k), delta = ap2(node, app(node, i), i);
    std::vector<std::pair<Tree, bool>> spines{{app(delta, delta), false}};
    for (Tree t = z; spines.size() < 24;) spines.push_back({t = app(i, t), false});
    Spines random{1};
    for (int n = 0; n < 400; ++n) spines.push_back({random(m, 4, n % 4 == 3), n % 4 == 3});
    J e, o;
    const Tree wm = tree(m, jets::spineWhnf), we = tree(e, jets::spineWhnf), wo = tree(o, jets::spineWhnf);
    std::unordered_set<Tree> seen;
    size_t calls = 0, agree = 0, answered = 0, odd = 0, runs_out = 0, most = 0;
    for (const auto &[tm, is_odd] : spines) {
      if (!seen.insert(tm).second) continue;
      J &x = is_odd ? o : e;
      const Tree tx = copy(m, tm, x), wx = is_odd ? wo : we;
      for (int fuel = 0; fuel <= MAX_FUEL; ++fuel) {
        const Tree rm = m.apply(m.apply(wm, snat(m, fuel)), tm);
        const uint64_t jets = x.stats_counters.jets;
        const Tree rx = x.apply(x.apply(wx, snat(x, fuel)), tx);
        ++calls, odd += is_odd;
        agree += copy(x, rx, m) == rm;
        answered += !is_odd && x.stats_counters.jets == jets + 1;
        runs_out += !is_odd && rm == m.leaf();
        // some (△ fuel' …): fuel' a stem is fuel to spare, so the fuel no longer runs out.
        const Tree some = kids(m, rm).first, left = some ? kids(m, some).first : 0;
        if (left && kids(m, left).first) {
          if (!is_odd) most = std::max(most, size_t(fuel - 1));
          break;
        }
      }
    }
    check("Quoted._whnf answers what the rules do, at every fuel to one past where it runs out",
          agree == calls && answered == calls - odd && runs_out > 0 && most >= 40);
    std::printf("     %zu calls, %zu with odd shapes, %zu out of fuel; a spine took up to %zu rules\n",
                calls, odd, runs_out, most);
    // A fork for fuel is no Snat: the rules' to answer, once K z z needs some.
    const Tree t = copy(m, ap2(k, z, z), e);
    const uint64_t jets = e.stats_counters.jets;
    e.apply(e.apply(we, e.fork(e.leaf(), e.leaf())), t);
    check("a fork for fuel is left to the rules", e.stats_counters.jets == jets);
    // The memo answers what the jet answered.
    const Tree whnf = e.apply(we, snat(e, MAX_FUEL + 1)), r = e.apply(whnf, t);
    const uint64_t hits = e.stats_counters.memo_hits;
    check("the memo answers Quoted._whnf again", e.apply(whnf, t) == r && e.stats_counters.memo_hits == hits + 1);
  }
  return fail ? 1 : 0;
}
