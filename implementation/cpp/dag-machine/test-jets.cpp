// The eager evaluator's jets (../eager-graph-nil-mmap-32.hpp): skip_line and divmod answer what
// the rules do, on arguments built to catch the native code out; a jet's answer goes into the
// memo, what it has no answer for is left to the rules, the jets survive collection and clear(),
// and off they are not there at all. Built and run by test.sh.

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../eager-graph-nil-mmap-32.hpp"
#include "../evaluator.hpp"

using J = Evaluator<EagerGraphNilMmap32Jets>;
using M = Evaluator<EagerGraphNilMmap32>;
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

// The children of a fork.
template <class E> static std::pair<Tree, Tree> kids(E &e, Tree t) {
  using P = std::pair<Tree, Tree>;
  return e.triage([] { return P{}; }, [](Tree u) { return P{u, 0}; },
                  [](Tree u, Tree v) { return P{u, v}; }, t);
}

// A list of bits as Evaluator::of_ternary reads it: a cell is 2, its bit 0 (△) or 10 (△△) — or,
// for a 2, 200: no bit at all — and the list ends in `end`.
static std::string bits(const std::vector<int> &bs, const char *end = "0") {
  std::string s;
  for (int b : bs) s += b == 0 ? "20" : b == 1 ? "210" : "2200";
  return s + end;
}

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
    // divmod, on naturals across the limb boundaries and on what is not one — a trailing △, a cell
    // holding no bit, a stem ending the list, b = 0 — which the jet leaves to the rules.
    J e;
    M m;
    const Tree de = tree(e, jets::divmod), dm = tree(m, jets::divmod);
    uint64_t seed = 1; // xorshift
    const auto rand = [&](uint64_t n) {
      seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
      return seed % n;
    };
    const auto arg = [&] {
      std::vector<int> bs(rand(140));
      for (int &b : bs) b = rand(2);
      while (!bs.empty() && !bs.back()) bs.pop_back();
      switch (rand(8)) {
        case 0: bs.push_back(0); break;
        case 1: if (!bs.empty()) bs[rand(bs.size())] = 2; break;
        case 2: return bits(bs, "10");
        case 3: bs.clear(); break;
      }
      return bits(bs);
    };
    bool alike = true;
    const uint64_t jets = e.stats_counters.jets;
    for (int i = 0; i < 1000; ++i) {
      const std::string a = arg(), b = arg();
      alike &= e.to_ternary(e.apply(e.apply(de, e.of_ternary(a)), e.of_ternary(b))) ==
               m.to_ternary(m.apply(m.apply(dm, m.of_ternary(a)), m.of_ternary(b)));
    }
    check("divmod answers what the rules do", alike && e.stats_counters.jets - jets > 300);
    // Trees a cell off a partial △U (△V (△△a)) of divmod.
    const auto off = [](auto &x, Tree d, int i) {
      const Tree a = x.of_nat(1000), t = x.stem(x.leaf());
      const auto [u, y] = kids(x, x.apply(d, a));
      const auto [v, k] = kids(x, y);
      const Tree odd[] = {x.fork(v, t), x.fork(v, x.fork(t, a)), x.fork(v, x.stem(a)),
                          x.fork(x.stem(v), k)};
      return x.fork(u, odd[i]);
    };
    const uint64_t taken = e.stats_counters.jets;
    for (int i = 0; i < 4; ++i)
      alike &= e.to_ternary(e.apply(off(e, de, i), e.of_nat(7))) ==
               m.to_ternary(m.apply(off(m, dm, i), m.of_nat(7)));
    check("what only looks like a partial of divmod is left to the rules",
          alike && e.stats_counters.jets == taken);
    const int64_t x = 0x123456789abcdef, y = 1000003;
    const Tree p = e.apply(de, e.of_nat(x)), qr = e.fork(e.of_nat(x / y), e.of_nat(x % y));
    const auto [r, s] = apply(e, p, e.of_nat(y));
    const uint64_t hits = e.stats_counters.memo_hits;
    check("divmod answers in a step, and the memo again",
          r == qr && s == 1 && e.apply(p, e.of_nat(y)) == qr && e.stats_counters.memo_hits == hits + 1);
  }
  {
    // The jets' trees are the evaluator's roots: skip_line and divmod built after a collection with
    // no other root, and after clear(), on slots a lost tree would have left, are still the jets'.
    J e;
    bool fired = true;
    for (int round = 0; round < 2; ++round) {
      if (round) e.clear();
      e.set_budget(4096);
      list(e, std::vector<Tree>(5000, e.of_nat(98))); // garbage past the budget: the next step collects
      e.apply(e.leaf(), e.leaf());
      const Tree a = e.of_nat(97), x = list(e, std::vector<Tree>(N, a), list(e, {e.of_nat(10), a}));
      e.roots().push_back(x);
      const Tree skip = tree(e, jets::skipLine);
      const auto [r, s] = apply(e, skip, x);
      const Tree p = e.apply(tree(e, jets::divmod), e.of_nat(1000));
      fired &= r == list(e, {a}) && s < 10 &&
               apply(e, p, e.of_nat(7)) == std::pair(e.fork(e.of_nat(142), e.of_nat(6)), uint64_t(1));
    }
    check("the jets survive collection and clear()", fired && e.stats_counters.gcs >= 2);
  }
  {
    // Off, step for step and node for node the evaluator without jets.
    J e;
    M m;
    e.set_jets(false);
    const bool fresh = e.allocated() == m.allocated();
    const Tree se = tree(e, jets::skipLine), sm = tree(m, jets::skipLine);
    const Tree de = tree(e, jets::divmod), dm = tree(m, jets::divmod);
    const std::string x = std::string(N, 'a') + "\nb";
    check("off, it is the evaluator without jets",
          fresh && apply(e, se, e.of_string(x)) == apply(m, sm, m.of_string(x)) &&
              apply(e, e.apply(de, e.of_nat(1000)), e.of_nat(7)) ==
                  apply(m, m.apply(dm, m.of_nat(1000)), m.of_nat(7)));
  }
  return fail ? 1 : 0;
}
