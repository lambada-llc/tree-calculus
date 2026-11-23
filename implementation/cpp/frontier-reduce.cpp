// Bulk-synchronous frontier reducer for tree calculus.
//
// Motivation: a recursive fork-join reducer can only cash in *coarse* parallelism
// (per-task overhead swamps fine-grained work). This reducer instead models the
// computation as a graph of application nodes and, each ROUND, reduces *every*
// redex that is currently ready — simultaneously. The number of rounds is then
// the parallel span (critical-path length) with infinite cores, and the total
// number of reductions is the work. Measuring both directly tests claims like
// "equal on two depth-d trees has span O(d)".
//
// This implementation runs the rounds sequentially but counts them, so it
// reports the *available* parallelism (work / span) independent of scheduler
// overhead. Per-round parallel execution is a straightforward next step.
//
// Node graph (indices into one arena), tags:
//   LEAF, STEM(x), FORK(x,y)  — values
//   APP(x=function, y=argument) — a redex candidate
//   IND(x)                      — forwarding pointer (created by rules 1 / 3a)
//
// Reduction of APP(f,a), with f resolved through indirections:
//   f = LEAF            -> STEM(a)                                   (0a)
//   f = STEM(u)         -> FORK(u,a)                                 (0b)
//   f = FORK(u,w):
//     u = LEAF          -> IND(w)                                    (rule 1)
//     u = STEM(x)       -> APP(APP(x,a), APP(w,a))                   (rule 2, the only forker)
//     u = FORK(p,q), need a resolved:
//       a = LEAF        -> IND(p)                                    (3a)
//       a = STEM(d)     -> APP(q,d)                                  (3b)
//       a = FORK(d,e)   -> APP(APP(w,d), e)                          (3c)
//
// Readiness / demand: an APP is ready when its function is a value (for rule 3,
// also its argument). Blocked APPs register on the node they wait for; when that
// node becomes a value or indirection, its waiters are re-checked. Every APP is
// eventually reduced (full normalization), so the whole graph reaches normal
// form — no laziness, so discarded subterms are reduced too (fine for the
// terminating programs here).

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>

enum : uint8_t { LEAF = 0, STEM = 1, FORK = 2, APP = 3, IND = 4 };

struct Node { uint8_t tag; int32_t x, y; int32_t whead, wnext; };
static std::vector<Node> H;
static std::vector<int32_t> FRONTIER;   // ready APPs for the *next* round
static uint64_t ROUNDS = 0, REDUCTIONS = 0;

static inline int32_t mk(uint8_t t, int32_t x, int32_t y) {
  H.push_back({t, x, y, -1, -1});
  return (int32_t)H.size() - 1;
}
static inline int32_t resolve(int32_t i) { while (H[i].tag == IND) i = H[i].x; return i; }

static void schedule(int32_t i);                    // fwd
static inline void addwait(int32_t dep, int32_t i) { H[i].wnext = H[dep].whead; H[dep].whead = i; }
static inline void wake(int32_t j) {                // j became value/IND: re-check its waiters
  int32_t w = H[j].whead; H[j].whead = -1;
  while (w != -1) { int32_t nx = H[w].wnext; schedule(w); w = nx; }
}

// Decide if APP i can fire now; if not, park it on the node it needs.
static void schedule(int32_t i) {
  int32_t f = resolve(H[i].x);
  uint8_t tf = H[f].tag;
  if (tf == APP) { addwait(f, i); return; }
  if (tf == LEAF || tf == STEM) { FRONTIER.push_back(i); return; }
  // f == FORK
  int32_t u = resolve(H[f].x);
  uint8_t tu = H[u].tag;
  if (tu == APP) { addwait(u, i); return; }
  if (tu == LEAF || tu == STEM) { FRONTIER.push_back(i); return; }  // rule 1 or 2
  // u == FORK -> rule 3, need the argument resolved
  int32_t a = resolve(H[i].y);
  if (H[a].tag == APP) { addwait(a, i); return; }
  FRONTIER.push_back(i);
}

static void reduce(int32_t i) {
  REDUCTIONS++;
  int32_t f = resolve(H[i].x);
  uint8_t tf = H[f].tag;
  if (tf == LEAF) {                    // 0a: stem(a)
    H[i].tag = STEM; H[i].x = H[i].y; wake(i); return;
  }
  if (tf == STEM) {                    // 0b: fork(u, a)
    int32_t a = H[i].y; H[i].tag = FORK; H[i].x = H[f].x; H[i].y = a; wake(i); return;
  }
  // tf == FORK
  int32_t u = resolve(H[f].x), w = H[f].y;
  uint8_t tu = H[u].tag;
  if (tu == LEAF) {                    // rule 1: -> w
    H[i].tag = IND; H[i].x = w; wake(i); return;
  }
  if (tu == STEM) {                    // rule 2: App(App(x,a), App(w,a))  -- the fork
    int32_t x = H[u].x, a = H[i].y;
    int32_t L = mk(APP, x, a), R = mk(APP, w, a);
    H[i].tag = APP; H[i].x = L; H[i].y = R;
    schedule(L); schedule(R); schedule(i);      // i now waits on L
    return;
  }
  // tu == FORK -> rule 3
  int32_t a = resolve(H[i].y), p = H[u].x, q = H[u].y;
  uint8_t ta = H[a].tag;
  if (ta == LEAF) { H[i].tag = IND; H[i].x = p; wake(i); return; }         // 3a -> p
  if (ta == STEM) { H[i].tag = APP; H[i].x = q; H[i].y = H[a].x; schedule(i); return; }  // 3b -> App(q,d)
  int32_t inner = mk(APP, w, H[a].x);                                       // 3c -> App(App(w,d), e)
  H[i].tag = APP; H[i].x = inner; H[i].y = H[a].y;
  schedule(inner); schedule(i);
}

// ---- ternary I/O ----
static int32_t of_ternary(const std::string &s) {
  std::vector<int32_t> st;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    char c = *it;
    if (c == '0') st.push_back(mk(LEAF, 0, 0));
    else if (c == '1') { int32_t u = st.back(); st.pop_back(); st.push_back(mk(STEM, u, 0)); }
    else if (c == '2') { int32_t u = st.back(); st.pop_back(); int32_t v = st.back(); st.pop_back(); st.push_back(mk(FORK, u, v)); }
  }
  return st.back();
}
static void to_ternary(int32_t i, std::string &out) {
  i = resolve(i);
  switch (H[i].tag) {
    case LEAF: out.push_back('0'); break;
    case STEM: out.push_back('1'); to_ternary(H[i].x, out); break;
    case FORK: out.push_back('2'); to_ternary(H[i].x, out); to_ternary(H[i].y, out); break;
    default: out.push_back('?'); break;   // unreduced APP left in the tree (shouldn't happen)
  }
}

int main() {
  H.reserve(1u << 26);
  std::vector<int32_t> inputs;
  std::string line;
  while (std::getline(std::cin, line)) { if (!line.empty()) inputs.push_back(of_ternary(line)); }

  int32_t root = of_ternary("21100");            // identity
  std::vector<int32_t> spine;
  for (int32_t t : inputs) { root = mk(APP, root, t); spine.push_back(root); }
  for (int32_t i : spine) schedule(i);           // seed the frontier

  while (!FRONTIER.empty()) {
    std::vector<int32_t> cur;
    cur.swap(FRONTIER);                          // this round's ready set; reduce() fills the next
    for (int32_t i : cur) if (H[i].tag == APP) reduce(i);
    ROUNDS++;
  }

  if (getenv("DBG"))
    fprintf(stderr, "rounds(span)=%llu reductions(work)=%llu parallelism=%.1f nodes=%zu\n",
            (unsigned long long)ROUNDS, (unsigned long long)REDUCTIONS,
            ROUNDS ? REDUCTIONS / (double)ROUNDS : 0.0, H.size());

  std::string out; to_ternary(root, out);
  std::cout << out << "\n";
  return 0;
}
