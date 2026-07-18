// Parallel bulk-synchronous frontier reducer. Reduces every ready node in the
// graph each round, in parallel, exploiting confluence.
//
// A round has two phases and needs no locks on the graph:
//   Phase A (read-only): scan the live application nodes, collect the ready ones
//     (function is a value; for rule 3, argument too).
//   Phase B (reduce): reduce each ready node. It only reads normal forms and
//     writes its own node plus freshly allocated ones, so distinct ready nodes
//     touch disjoint memory. The only shared state is the allocator (atomic
//     bump, per-thread chunks) and the frontier banks (atomic batch-append —
//     one fetch_add + memcpy per thread per round, no serial merge).
//
// Node = two 32-bit words (8 bytes): tag in the top 3 bits of the first word,
// index in the low 29 bits (up to 2^29 nodes). Reduction is bandwidth-bound, so
// the tight packing cuts memory traffic.
//
// Build:  clang++ parallel-frontier.cpp -O3 -std=c++23 -stdlib=libc++ -fopenmp -o parallel-frontier.exe
// Usage:  ternary on stdin/stdout; DBG=1 prints rounds/work; env OMP_NUM_THREADS.

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <sys/mman.h>
#include <omp.h>

enum : uint32_t { LEAF = 0, STEM = 1, FORK = 2, APP = 3, IND = 4 };
static constexpr uint32_t IDX = 0x1fffffffu;   // low 29 bits = index
static constexpr int TSH = 29;

struct Node { uint32_t hx, y; };               // hx = (tag<<29) | child1 ; y = child2
static constexpr size_t CAP = size_t(1) << 29; // 2^29 nodes (matches 29-bit index)
static constexpr uint32_t CHUNK = 1u << 12;
static Node *H;
static std::atomic<uint32_t> TOP{1};

static inline uint32_t TAG(uint32_t i) { return H[i].hx >> TSH; }
static inline uint32_t XC(uint32_t i)  { return H[i].hx & IDX; }
static inline uint32_t YC(uint32_t i)  { return H[i].y; }
static inline void SET(uint32_t i, uint32_t t, uint32_t x, uint32_t y) { H[i].hx = (t << TSH) | x; H[i].y = y; }

struct Ctx { uint32_t cur = 0, end = 0; };
static inline uint32_t mk(Ctx &c, uint32_t t, uint32_t x, uint32_t y) {
  if (c.cur >= c.end) { c.cur = TOP.fetch_add(CHUNK, std::memory_order_relaxed); c.end = c.cur + CHUNK; }
  uint32_t id = c.cur++;
  H[id].hx = (t << TSH) | x; H[id].y = y;
  return id;
}
static inline uint32_t resolve(uint32_t i) { while (TAG(i) == IND) i = XC(i); return i; }

// Read-only readiness test (Phase A).
static inline bool ready(uint32_t i) {
  uint32_t f = resolve(XC(i)), tf = TAG(f);
  if (tf == APP) return false;
  if (tf == LEAF || tf == STEM) return true;
  uint32_t u = resolve(XC(f)), tu = TAG(u);
  if (tu == APP) return false;
  if (tu == LEAF || tu == STEM) return true;   // rule 1 / 2
  return TAG(resolve(YC(i))) != APP;           // rule 3 needs the argument
}

// Reduce ready node i (Phase B). Pushes newly-live APP ids (new nodes, and i
// itself if it stays an APP) into `out`.
static inline void reduce(Ctx &c, uint32_t i, std::vector<uint32_t> &out) {
  uint32_t f = resolve(XC(i)), tf = TAG(f);
  if (tf == LEAF) { SET(i, STEM, YC(i), 0); return; }                  // 0a: stem(a)
  if (tf == STEM) { SET(i, FORK, XC(f), YC(i)); return; }              // 0b: fork(u,a)
  uint32_t u = resolve(XC(f)), w = YC(f), tu = TAG(u);
  if (tu == LEAF) { SET(i, IND, w, 0); return; }                       // rule 1: -> w
  if (tu == STEM) {                                                    // rule 2 (fork)
    uint32_t x = XC(u), a = YC(i);
    uint32_t L = mk(c, APP, x, a), R = mk(c, APP, w, a);
    SET(i, APP, L, R);
    out.push_back(L); out.push_back(R); out.push_back(i);
    return;
  }
  uint32_t a = resolve(YC(i)), p = XC(u), q = YC(u), ta = TAG(a);
  if (ta == LEAF) { SET(i, IND, p, 0); return; }                       // 3a -> p
  if (ta == STEM) { SET(i, APP, q, XC(a)); out.push_back(i); return; } // 3b -> App(q,d)
  uint32_t inner = mk(c, APP, w, XC(a));                                // 3c -> App(App(w,d), e)
  SET(i, APP, inner, YC(a));
  out.push_back(inner); out.push_back(i);
}

static Ctx g0;
static uint32_t of_ternary(const std::string &s) {
  std::vector<uint32_t> st;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    char c = *it;
    if (c == '0') st.push_back(mk(g0, LEAF, 0, 0));
    else if (c == '1') { uint32_t u = st.back(); st.pop_back(); st.push_back(mk(g0, STEM, u, 0)); }
    else if (c == '2') { uint32_t u = st.back(); st.pop_back(); uint32_t v = st.back(); st.pop_back(); st.push_back(mk(g0, FORK, u, v)); }
  }
  return st.back();
}
static void to_ternary(uint32_t i, std::string &out) {
  i = resolve(i);
  switch (TAG(i)) {
    case LEAF: out.push_back('0'); break;
    case STEM: out.push_back('1'); to_ternary(XC(i), out); break;
    case FORK: out.push_back('2'); to_ternary(XC(i), out); to_ternary(YC(i), out); break;
    default: out.push_back('?'); break;
  }
}

int main() {
  setenv("OMP_STACKSIZE", "1G", 0);
  void *p = mmap(nullptr, CAP * sizeof(Node), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); return 1; }
  H = static_cast<Node *>(p);

  std::vector<uint32_t> inputs;
  std::string line;
  while (std::getline(std::cin, line)) if (!line.empty()) inputs.push_back(of_ternary(line));

  uint32_t root = of_ternary("21100");

  uint32_t *bank[2];
  for (int b = 0; b < 2; ++b) {
    void *q = mmap(nullptr, CAP * sizeof(uint32_t), PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (q == MAP_FAILED) { perror("mmap"); return 1; }
    bank[b] = static_cast<uint32_t *>(q);
  }
  int cur = 0;
  uint32_t ncur = 0;
  for (uint32_t t : inputs) { root = mk(g0, APP, root, t); bank[cur][ncur++] = root; }

  uint64_t rounds = 0;
  std::atomic<uint32_t> nnext{0};
  std::atomic<uint64_t> work_a{0};

  #pragma omp parallel
  {
    Ctx c;
    std::vector<uint32_t> rdy, keepout;
    for (;;) {
      rdy.clear(); keepout.clear();
      uint32_t *C = bank[cur], *N = bank[cur ^ 1];
      // --- Phase A (reads only): partition into ready vs blocked. ---
      #pragma omp for schedule(static) nowait
      for (uint32_t k = 0; k < ncur; ++k) {
        uint32_t i = C[k];
        if (ready(i)) rdy.push_back(i);
        else keepout.push_back(i);
      }
      #pragma omp barrier
      // --- Phase B (writes): reduce ready nodes; new/still-live apps -> keepout. ---
      for (uint32_t i : rdy) reduce(c, i, keepout);
      work_a.fetch_add(rdy.size(), std::memory_order_relaxed);
      // --- Phase C: parallel batch-append into the next bank. ---
      uint32_t base = nnext.fetch_add((uint32_t)keepout.size(), std::memory_order_relaxed);
      if (!keepout.empty()) memcpy(N + base, keepout.data(), keepout.size() * sizeof(uint32_t));
      #pragma omp barrier
      #pragma omp single
      { cur ^= 1; ncur = nnext.load(); nnext.store(0); rounds++; }
      #pragma omp barrier
      if (ncur == 0) break;
    }
  }
  munmap(bank[0], CAP * sizeof(uint32_t)); munmap(bank[1], CAP * sizeof(uint32_t));

  if (getenv("DBG"))
    fprintf(stderr, "rounds=%llu work~=%llu nodes=%u threads=%d\n",
            (unsigned long long)rounds, (unsigned long long)work_a.load(), TOP.load(), omp_get_max_threads());

  std::string out_s; to_ternary(root, out_s);
  std::cout << out_s << "\n";
  return 0;
}
