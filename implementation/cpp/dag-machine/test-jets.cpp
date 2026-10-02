// The eager evaluator's jet (../eager-graph-nil-mmap-32.hpp): skip_line answers what the
// rules do, on lines built to catch the native loop out; the jet's answer goes into the memo, a
// stem ending the line is left to the rules, the jet survives collection and clear(), and off it
// is not there at all. Built and run by test.sh.

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

// skip_line read as the runner reads a module, each line an application, not by the jets' reader.
template <class E> static Tree skip_line(E &e) {
  std::unordered_map<std::string, Tree> env{{"\xe2\x96\xb3", e.leaf()}}; // △
  std::istringstream lines(jets::skipLine);
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

int main() {
  constexpr size_t N = 100000;
  {
    J e;
    const Tree skip = skip_line(e), nl = e.of_nat(10), a = e.of_nat(97), rest = list(e, {a});
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
    // The jet's trees are the evaluator's roots: skip_line built after a collection with no other
    // root, and after clear(), on slots a lost tree would have left, is still the jet's.
    J e;
    bool fired = true;
    for (int round = 0; round < 2; ++round) {
      if (round) e.clear();
      e.set_budget(4096);
      list(e, std::vector<Tree>(5000, e.of_nat(98))); // garbage past the budget: the next step collects
      e.apply(e.leaf(), e.leaf());
      const Tree a = e.of_nat(97), x = list(e, std::vector<Tree>(N, a), list(e, {e.of_nat(10), a}));
      e.roots().push_back(x);
      const Tree skip = skip_line(e);
      const auto [r, s] = apply(e, skip, x);
      fired &= r == list(e, {a}) && s < 10;
    }
    check("the jet survives collection and clear()", fired && e.stats_counters.gcs >= 2);
  }
  {
    // Off, step for step and node for node the evaluator without jets.
    J e;
    Evaluator<EagerGraphNilMmap32> m;
    e.set_jets(false);
    const bool fresh = e.allocated() == m.allocated();
    const Tree se = skip_line(e), sm = skip_line(m);
    const std::string x = std::string(N, 'a') + "\nb";
    check("off, it is the evaluator without jets",
          fresh && apply(e, se, e.of_string(x)) == apply(m, sm, m.of_string(x)));
  }
  return fail ? 1 : 0;
}
