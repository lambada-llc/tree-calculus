// The eager evaluator's hash-consing (../eager-graph-nil-mmap-32.hpp) is exact: whichever way a
// shape was built — stem(), fork(), list(), in a run or not — and wherever its node went since —
// the nursery, the table, a run — building it again gives the same index and makes no node,
// through every growth of the table and every collection. A search that never ends fails the
// test rather than hanging it. Built and run by test.sh.

#include <cstdint>
#include <cstdio>
#include <random>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../eager-graph-nil-mmap-32.hpp"

using E = EagerGraphNilMmap32;
using Tree = E::Tree;

static int fail = 0;
static void check(const char *what, bool ok) {
  std::printf("%s intern %s\n", ok ? "PASS" : "FAIL", what);
  fail += !ok;
}

static std::pair<Tree, Tree> children(E &e, Tree t) {
  return e.triage([] { return std::pair<Tree, Tree>{0, 0}; }, [](Tree u) { return std::pair<Tree, Tree>{u, 0}; },
                  [](Tree u, Tree v) { return std::pair<Tree, Tree>{u, v}; }, t);
}

// Whether each of `trees`, built again from its children, is itself, without a node made.
static bool rebuilt(E &e, const std::vector<Tree> &trees) {
  const size_t before = e.allocated();
  bool same = true;
  for (const Tree t : trees)
    if (const auto [u, v] = children(e, t); u) same &= (v ? e.fork(u, v) : e.stem(u)) == t;
  return same && e.allocated() == before;
}

int main() {
  alarm(60); // a search that never ends: killed, rather than test.sh hanging
  {
    // Trees built mostly on what was just built, as a reduction builds them, which is what the
    // nursery holds; lists, some long enough to be runs; collections keeping a random half.
    E e;
    std::mt19937_64 rng(1);
    std::vector<Tree> pool{e.leaf()};
    const auto pick = [&] {
      return pool[rng() % 4 ? pool.size() - 1 - rng() % std::min<size_t>(pool.size(), 64) : rng() % pool.size()];
    };
    bool lists = true, built = true, collected = true;
    for (int round = 0; round < 24; ++round) {
      for (int i = 0; i < 60000; ++i) pool.push_back(rng() % 5 ? e.fork(pick(), pick()) : e.stem(pick()));
      if (round % 3 == 1) {
        const size_t n = rng() % 2 ? rng() % 1000 : (size_t(1) << 16) + rng() % 5000;
        std::vector<Tree> heads(n);
        for (Tree &h : heads) h = pick();
        const Tree tail = pick();
        size_t k = 0;
        Tree t = e.list(n, [&] { return heads[k++]; }, tail), again = tail;
        for (const Tree h : heads) again = e.fork(h, again); // last() gives the innermost first
        lists &= t == again;
        for (; t != tail; t = children(e, t).second) pool.push_back(t);
      }
      built &= rebuilt(e, pool);
      if (round % 4 == 3) {
        e.roots().clear();
        for (const Tree t : pool)
          if (rng() % 2) e.roots().push_back(t);
        e.collect();
        // What survived is what the roots reach; the rest of the pool may be reused.
        std::unordered_set<Tree> seen;
        std::vector<Tree> todo = e.roots();
        pool.clear();
        while (!todo.empty()) {
          const Tree t = todo.back();
          todo.pop_back();
          if (!seen.insert(t).second) continue;
          pool.push_back(t);
          const auto [u, v] = children(e, t);
          if (u) todo.push_back(u);
          if (v) todo.push_back(v);
        }
        collected &= rebuilt(e, pool);
      }
    }
    check("a list is the cells fork() builds", lists);
    check("a shape built again is the node it was", built);
    check("and still is after a collection", collected);
  }
  {
    // Chains, each node built on the one made just before it: they go in without a search, so
    // nothing makes the table grow for them as they go in. A list between two takes the first
    // into the table, and a collection that finds both live re-lays the table with all of them.
    bool kept = true;
    for (size_t a = 1; a < 100000; a *= 3)
      for (size_t b = 1; b < 100000; b *= 3) {
        E e;
        std::vector<Tree> chain{e.leaf()};
        while (chain.size() <= a) chain.push_back(e.stem(chain.back()));
        chain.push_back(e.list(1, [&] { return e.leaf(); }, chain.back()));
        while (chain.size() <= a + 1 + b) chain.push_back(e.stem(chain.back()));
        e.roots().push_back(chain.back());
        e.collect();
        kept &= e.live() == chain.size() - 1 && rebuilt(e, chain);
      }
    check("chains on the node made last, and a collection that finds them live", kept);
  }
  return fail != 0;
}
