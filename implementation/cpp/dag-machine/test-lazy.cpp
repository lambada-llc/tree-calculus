// The lazy evaluator (../lazy-graph-nil-mmap-32.hpp) at the end of its arena, sized down so a test
// reaches it: a budget past the arena collects at its end, a live set that does not fit is an error
// rather than a write past it, and after one the evaluator reduces as before. Built and run by
// test.sh.

#include <cstdio>
#include <stdexcept>
#include <string>

#include "../lazy-graph-nil-mmap-32.hpp"

constexpr unsigned BITS = 12;
using Lazy = BasicLazyGraphNilMmap32<BITS>;
using Tree = Lazy::Tree;

static int fail = 0;
static void check(const char *what, bool ok) {
  std::printf("%s lazy %s\n", ok ? "PASS" : "FAIL", what);
  fail += !ok;
}

// What f answers, or what it threw.
template <class F> static std::string answer(F f) {
  try {
    return f();
  } catch (const std::exception &err) {
    return err.what();
  }
}

int main() {
  Lazy e;
  const Tree L = e.leaf(), k = e.stem(L);
  const auto S = [&](Tree f, Tree g) { return e.fork(e.stem(f), g); }; // S f g x = f x (g x)
  const auto K = [&](Tree x) { return e.fork(L, x); };                 // K x y = x
  const Tree I = S(k, k), W = S(I, I); // W x = x x
  // V V △ for V = \v \acc v v (△ acc acc) never stops, and keeps all it builds.
  const Tree V = S(S(K(L), S(K(L), S(K(k), W))), K(S(L, I)));
  for (const Tree t : {W, V}) e.roots().push_back(t);

  const auto whnf = [&](Tree x) -> std::string {
    return e.triage([] { return "leaf"; }, [](Tree) { return "stem"; }, [](Tree, Tree) { return "fork"; }, x);
  };
  // W △ = △ △ 30,000 times over: five nodes each, garbage once answered, 36 times the arena.
  const auto churn = [&] {
    std::string r;
    for (int i = 0; i < 30000; ++i) r = whnf(e.apply(W, L));
    return r;
  };
  const auto within = [&] { return e.allocated() < (size_t(1) << BITS); };

  e.set_budget(size_t(1) << 40);
  check("a budget past the arena collects at its end", answer(churn) == "stem" && within());
  e.set_budget(64);
  check("a live set that does not fit is an error",
        answer([&] { return whnf(e.apply(e.apply(V, V), L)); }) == "arena exhausted: the live set does not fit" &&
            within());
  check("after which it reduces as before", answer(churn) == "stem" && within());
  return fail != 0;
}
