// Parallel fork-join reducer on the *champion* backend.
//
// Combines the three ingredients that individually won the single-thread board
// and the parallel prototype:
//   1. Representation: nil-mmap-32 nodes — 8 bytes {uint32 u, uint32 v} in one
//      mmap arena, arity discriminated by null children (u==0 leaf; v==0 stem;
//      else fork). Index 0 is the null sentinel; the shared leaf lives at 1.
//   2. Peeking super-rules: rule 2 (S) is expanded by peeking into the head so
//      that a provably dead apply(y,b) is never built (S + K elimination). Same
//      derivation as peek.hpp.
//   3. Fork-join parallelism: the two *independent* sub-applies produced by the
//      duplication rule run on different cores (tree calculus is confluent).
//      Independent spots parallelized here:
//        - generic rule-2 fallback:   apply(apply(x,b), apply(y,b))
//        - x = stem(stem x2):         apply(apply(x2,R), apply(b,R))   (after R)
//
// Granularity: a branch-depth counter (BCUT) plus a hard cap on total spawned
// tasks keeps tasks coarse; each task bump-allocates from its own arena chunk
// (one atomic fetch_add per chunk, none per node), and nodes are immutable once
// published so cross-thread reads need no synchronization.
//
// Build:  clang++ parallel-peek.cpp -O3 -std=c++23 -stdlib=libc++ -fopenmp -o parallel-peek
// Usage:  same stdin/stdout contract as cpp/main.exe (ternary lines, left-fold).
//         env BCUT (default 5), OMP_NUM_THREADS.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <omp.h>

struct Node { uint32_t u, v; };

static constexpr size_t CAP = size_t(1) << 31;   // 2^31 nodes (16 GB virtual)
static constexpr uint32_t CHUNK = 1u << 12;      // 4k nodes per chunk claim
static int BCUT = 5;                             // branch-depth fork cutoff (best for parallel-and)
static int MAXTASKS = 256;                       // hard cap on total spawned tasks

static Node *ARENA;
static std::atomic<uint32_t> HWM{2};             // 0 = null sentinel, 1 = leaf
static std::atomic<int> SPAWNED{0};

struct Ctx { uint32_t cur = 0, end = 0; };

static inline uint32_t alloc(Ctx &c, uint32_t u, uint32_t v) {
  if (c.cur >= c.end) {
    c.cur = HWM.fetch_add(CHUNK, std::memory_order_relaxed);
    c.end = c.cur + CHUNK;
  }
  uint32_t id = c.cur++;
  ARENA[id] = {u, v};
  return id;
}

static inline uint32_t leaf() { return 1; }
static inline uint32_t stem(Ctx &c, uint32_t u) { return alloc(c, u, 0); }
static inline uint32_t fork(Ctx &c, uint32_t u, uint32_t v) { return alloc(c, u, v); }

static uint32_t apply(Ctx &c, uint32_t a, uint32_t b, int bdepth);

// Compute apply(apply(P,Q), apply(S,T)) with the two inner applies on different
// cores. Only reached with spawn budget left (bdepth < BCUT); the common
// no-budget case is handled inline at the call sites so the hot path pays no
// call/atomic overhead.
static uint32_t par_apply2_spawn(Ctx &c, uint32_t P, uint32_t Q,
                                 uint32_t S, uint32_t T, int bdepth) {
  uint32_t L, R;
  if (SPAWNED.fetch_add(1, std::memory_order_relaxed) < MAXTASKS) {
    Ctx cl;
    #pragma omp task shared(L, cl) firstprivate(P, Q, bdepth) default(none)
    L = apply(cl, P, Q, bdepth + 1);
    R = apply(c, S, T, bdepth + 1);
    #pragma omp taskwait
  } else {
    L = apply(c, P, Q, BCUT);
    R = apply(c, S, T, BCUT);
  }
  return apply(c, L, R, bdepth);
}

// apply(apply(P,Q), apply(S,T)): spawn only while budget remains, else inline.
static inline uint32_t par_apply2(Ctx &c, uint32_t P, uint32_t Q,
                                  uint32_t S, uint32_t T, int bdepth) {
  if (bdepth < BCUT && SPAWNED.load(std::memory_order_relaxed) < MAXTASKS)
    return par_apply2_spawn(c, P, Q, S, T, bdepth);
  // Hot path: no spawning. Force bdepth to BCUT so descendants skip the check.
  uint32_t L = apply(c, P, Q, BCUT);
  uint32_t R = apply(c, S, T, BCUT);
  return apply(c, L, R, BCUT);
}

// Peeking apply (see peek.hpp for the derivation), over nil-mmap-32 nodes.
static uint32_t apply(Ctx &c, uint32_t a, uint32_t b, int bdepth) {
  Node na = ARENA[a];
  if (na.u == 0) return stem(c, b);         // a = leaf:    apply(△, b) = △b
  if (na.v == 0) return fork(c, na.u, b);   // a = stem(u): apply(△u, b) = △ u b

  uint32_t u = na.u, y = na.v;              // a = fork(u, y)
  Node nu = ARENA[u];
  if (nu.u == 0) return y;                  // u = leaf (rule 1): y

  if (nu.v == 0) {                          // u = stem(x): rule 2 (S), peek x
    uint32_t x = nu.u;
    Node nx = ARENA[x];
    if (nx.u == 0)                          // x = leaf: fork(b, apply(y, b))
      return fork(c, b, apply(c, y, b, bdepth));
    if (nx.v == 0) {                        // x = stem(x1)
      uint32_t x1 = nx.u;
      Node nx1 = ARENA[x1];
      if (nx1.u == 0) return b;             // x = stem(leaf): apply(y,b) is dead
      if (nx1.v == 0) {                     // x = stem(stem x2)
        uint32_t x2 = nx1.u;
        uint32_t R = apply(c, y, b, bdepth);
        return par_apply2(c, x2, R, b, R, bdepth);  // apply(apply(x2,R),apply(b,R))
      }
      // x = stem(fork(w, x2))
      uint32_t w = nx1.u, x2 = nx1.v;
      uint32_t R = apply(c, y, b, bdepth);
      Node nR = ARENA[R];
      if (nR.u == 0) return w;              // R = leaf   -> w
      if (nR.v == 0) return apply(c, x2, nR.u, bdepth);           // R = stem d
      return apply(c, apply(c, b, nR.u, bdepth), nR.v, bdepth);   // R = fork d e
    }
    // x = fork(xw, x2)
    uint32_t x2 = nx.v;
    if (ARENA[nx.u].u == 0)                 // x = fork(leaf, x2): apply(x2, apply(y,b))
      return apply(c, x2, apply(c, y, b, bdepth), bdepth);
    return par_apply2(c, x, b, y, b, bdepth);  // generic: apply(apply(x,b),apply(y,b))
  }

  // u = fork(w, x): rule 3, triage b
  uint32_t w = nu.u, x = nu.v;
  Node nb = ARENA[b];
  if (nb.u == 0) return w;                              // b = leaf    -> w
  if (nb.v == 0) return apply(c, x, nb.u, bdepth);      // b = stem d   -> apply(x,d)
  return apply(c, apply(c, y, nb.u, bdepth), nb.v, bdepth); // b = fork d e -> apply(apply(y,d),e)
}

// ---- ternary I/O ----
static uint32_t of_ternary(Ctx &c, const std::string &s) {
  std::vector<uint32_t> st;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    char ch = *it;
    if (ch == '0') st.push_back(leaf());
    else if (ch == '1') { uint32_t x = st.back(); st.pop_back(); st.push_back(stem(c, x)); }
    else if (ch == '2') { uint32_t x = st.back(); st.pop_back(); uint32_t z = st.back(); st.pop_back(); st.push_back(fork(c, x, z)); }
  }
  return st.back();
}
static void to_ternary(uint32_t x, std::string &out) {
  Node n = ARENA[x];
  if (n.u == 0) { out.push_back('0'); return; }
  if (n.v == 0) { out.push_back('1'); to_ternary(n.u, out); return; }
  out.push_back('2'); to_ternary(n.u, out); to_ternary(n.v, out);
}

int main() {
  if (const char *e = getenv("BCUT")) BCUT = atoi(e);
  void *p = mmap(nullptr, CAP * sizeof(Node), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); return 1; }
  ARENA = static_cast<Node *>(p);
  ARENA[0] = {0, 0}; // null sentinel
  ARENA[1] = {0, 0}; // shared leaf

  Ctx c0;
  std::vector<uint32_t> inputs;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    inputs.push_back(of_ternary(c0, line));
  }

  uint32_t result = of_ternary(c0, "21100"); // identity
  #pragma omp parallel
  #pragma omp single
  {
    Ctx c = c0;
    for (uint32_t t : inputs) result = apply(c, result, t, 0);
  }

  std::string out;
  to_ternary(result, out);
  std::cout << out << "\n";
  return 0;
}
