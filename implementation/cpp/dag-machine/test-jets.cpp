// The eager evaluator's jets (../eager-graph-nil-mmap-32.hpp): skip_line, add and mul answer what
// the rules do, on arguments built to catch the native code out; a jet's answer goes into the
// memo, what it has no answer for is left to the rules, the jets survive collection and clear(),
// and off they are not there at all. Built and run by test.sh.

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
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

// The children of a fork, a stem's and 0, or two 0s.
template <class E> static std::pair<Tree, Tree> kids(E &e, Tree t) {
  using P = std::pair<Tree, Tree>;
  return e.triage([] { return P{}; }, [](Tree u) { return P{u, 0}; },
                  [](Tree u, Tree v) { return P{u, v}; }, t);
}

// t with what is at path — u a stem's or fork's left child, v a fork's right — swapped for r.
template <class E> static Tree at(E &e, Tree t, const char *path, Tree r) {
  if (!*path) return r;
  const auto [u, v] = kids(e, t);
  if (*path == 'v') return e.fork(u, at(e, v, path + 1, r));
  const Tree x = at(e, u, path + 1, r);
  return v ? e.fork(x, v) : e.stem(x);
}

// What is at path in t.
template <class E> static Tree sub(E &e, Tree t, const char *path) {
  for (; *path; ++path) t = *path == 'u' ? kids(e, t).first : kids(e, t).second;
  return t;
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
    // add and mul, on naturals across the limb boundaries and on what is not one — a trailing △,
    // a cell holding no bit, a stem ending the list — which the jets leave to the rules. Not the
    // last two for mul's b: on those the rules build trees too deep to compare, in up to 48M steps.
    J e;
    M m;
    uint64_t seed = 1; // xorshift
    const auto rand = [&](uint64_t n) {
      seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
      return seed % n;
    };
    const auto arg = [&](uint64_t n, bool odd) {
      std::vector<int> bs(rand(n));
      for (int &b : bs) b = rand(2);
      while (!bs.empty() && !bs.back()) bs.pop_back();
      switch (rand(8)) {
        case 0: bs.push_back(0); break;
        case 1: if (odd && !bs.empty()) bs[rand(bs.size())] = 2; break;
        case 2: if (odd) return bits(bs, "10"); break;
        case 3: bs.clear(); break;
      }
      return bits(bs);
    };
    for (const auto &[name, dag, n, odd] :
         {std::tuple{"add", jets::add, 140, true}, {"mul", jets::mul, 70, false}}) {
      const Tree fe = tree(e, dag), fm = tree(m, dag);
      bool alike = true;
      const uint64_t jets = e.stats_counters.jets;
      for (int i = 0; i < 1000; ++i) {
        const std::string a = arg(n, true), b = arg(n, odd);
        alike &= e.to_ternary(e.apply(e.apply(fe, e.of_ternary(a)), e.of_ternary(b))) ==
                 m.to_ternary(m.apply(m.apply(fm, m.of_ternary(a)), m.of_ternary(b)));
      }
      check((std::string(name) + " answers what the rules do").c_str(),
            alike && e.stats_counters.jets - jets > 300);
    }
    // Trees a cell off partials of add, △ (△ (△C z)) K, z = Z(a) (intern_jets): a layer of z
    // with the other bit, a cell that is not a's, no △ beside it, another tree than △ beside the
    // next layer, a layer too many, another tree than C or K; and of mul, △U (△△a): a stem for
    // △△a, △△△ for its △. The rules reduce them, in more than a step, though inside they may
    // meet a partial that is one.
    const auto off = [](auto &x, int i) {
      const Tree add = tree(x, jets::add), mul = tree(x, jets::mul), t = x.stem(x.leaf());
      const auto p = [&](Tree f, int64_t n) { return x.apply(f, x.of_nat(n)); };
      const Tree odd[] = {
          at(x, p(add, 1000), "uuvu", sub(x, p(add, 1001), "uuvu")),
          at(x, p(add, 1000), "uuvvvvvu", sub(x, p(add, 1002), "uuvvu")),
          at(x, p(add, 1000), "uuvvuv", t),
          at(x, p(add, 1000), "uuvvvu", t),
          at(x, p(add, 1), "uuvvvv", sub(x, p(add, 1), "uuv")),
          at(x, p(add, 1000), "uuu", t),
          at(x, p(add, 1000), "v", t),
          at(x, p(mul, 1000), "v", t),
          at(x, p(mul, 1000), "vu", t),
      };
      return odd[i];
    };
    bool alike = true;
    for (int i = 0; i < 9; ++i) {
      const auto [r, s] = apply(e, off(e, i), e.of_nat(7));
      alike &= s > 1 && e.to_ternary(r) == m.to_ternary(m.apply(off(m, i), m.of_nat(7)));
    }
    check("what only looks like a partial of add or mul is left to the rules", alike);
    const int64_t x = 0x123456789, y = 1000003;
    bool once = true;
    for (const auto &[dag, xy] : {std::pair{jets::add, x + y}, {jets::mul, x * y}}) {
      const Tree p = e.apply(tree(e, dag), e.of_nat(x)), r = e.of_nat(xy);
      const auto [rp, s] = apply(e, p, e.of_nat(y));
      const uint64_t hits = e.stats_counters.memo_hits;
      once &= rp == r && s == 1 && e.apply(p, e.of_nat(y)) == r &&
              e.stats_counters.memo_hits == hits + 1;
    }
    check("add and mul answer in a step, and the memo again", once);
  }
  {
    // The jets' trees are the evaluator's roots: skip_line, add and mul built after a collection
    // with no other root, and after clear(), on slots a lost tree would have left, are still the
    // jets'.
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
      fired &= r == list(e, {a}) && s < 10;
      for (const auto &[dag, xy] : {std::pair{jets::add, 1007}, {jets::mul, 7000}})
        fired &= apply(e, e.apply(tree(e, dag), e.of_nat(1000)), e.of_nat(7)) ==
                 std::pair(e.of_nat(xy), uint64_t(1));
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
    const std::string x = std::string(N, 'a') + "\nb";
    bool alike = fresh && apply(e, se, e.of_string(x)) == apply(m, sm, m.of_string(x));
    for (const char *dag : {jets::add, jets::mul})
      alike &= apply(e, e.apply(tree(e, dag), e.of_nat(1000)), e.of_nat(7)) ==
               apply(m, m.apply(tree(m, dag), m.of_nat(1000)), m.of_nat(7));
    check("off, it is the evaluator without jets", alike);
  }
  return fail ? 1 : 0;
}
