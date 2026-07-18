// Parallel bulk-synchronous frontier reducer.
//
// The measuring version (frontier-reduce.cpp) showed that a structural `equal`
// has span O(depth) and work O(2^depth) — thousands-fold available parallelism.
// This version actually executes each round in parallel to turn that into
// wall-clock speedup.
//
// Why it needs no locks on the graph — the two-phase round:
//   Phase A (read-only): scan the live application nodes, collect the ones that
//     are READY (function is a value; for rule 3, argument too). Pure reads.
//   Phase B (reduce): reduce every ready node. A ready node only *reads* values
//     (its function/argument are already normal forms, so no other thread is
//     mutating them) and only *writes* its own node plus freshly allocated
//     nodes. Distinct ready nodes touch disjoint memory. So the sole shared
//     state is the allocator (atomic bump, per-thread chunks) and the frontier
//     buffers (per-thread, concatenated between rounds). No graph locks, no CAS
//     on nodes.
//
// Node graph and reduction rules are identical to frontier-reduce.cpp.
//
// Build:  clang++ parallel-frontier.cpp -O3 -std=c++23 -stdlib=libc++ -fopenmp -o parallel-frontier
// Usage:  stdin/stdout ternary like the others; DBG=1 prints rounds/work; env
//         OMP_NUM_THREADS. Sets OMP_STACKSIZE so deep to_ternary output is safe.

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

enum : uint8_t { LEAF = 0, STEM = 1, FORK = 2, APP = 3, IND = 4 };

struct Node { uint8_t tag; uint32_t x, y; };
static constexpr size_t CAP = size_t(1) << 30;   // 1G nodes (12 GB virtual, lazily paged)
static constexpr uint32_t CHUNK = 1u << 12;
static Node *H;
static std::atomic<uint32_t> TOP{1};             // node 0 unused (0 = "no index" sentinel-ish)

struct Ctx { uint32_t cur = 0, end = 0; };
static inline uint32_t mk(Ctx &c, uint8_t t, uint32_t x, uint32_t y) {
  if (c.cur >= c.end) { c.cur = TOP.fetch_add(CHUNK, std::memory_order_relaxed); c.end = c.cur + CHUNK; }
  uint32_t id = c.cur++;
  H[id] = {t, x, y};
  return id;
}
static inline uint32_t resolve(uint32_t i) { while (H[i].tag == IND) i = H[i].x; return i; }

// Read-only readiness test (Phase A).
static inline bool ready(uint32_t i) {
  uint32_t f = resolve(H[i].x);
  uint8_t tf = H[f].tag;
  if (tf == APP) return false;
  if (tf == LEAF || tf == STEM) return true;
  uint32_t u = resolve(H[f].x);
  uint8_t tu = H[u].tag;
  if (tu == APP) return false;
  if (tu == LEAF || tu == STEM) return true;      // rule 1 / 2
  return H[resolve(H[i].y)].tag != APP;           // rule 3 needs the argument
}

// Reduce ready node i (Phase B). Pushes newly-live APP node ids (new nodes, and
// i itself if it remains an APP) into `out`.
static inline void reduce(Ctx &c, uint32_t i, std::vector<uint32_t> &out) {
  uint32_t f = resolve(H[i].x);
  uint8_t tf = H[f].tag;
  if (tf == LEAF) { H[i].tag = STEM; H[i].x = H[i].y; return; }                 // 0a
  if (tf == STEM) { uint32_t a = H[i].y; H[i].tag = FORK; H[i].x = H[f].x; H[i].y = a; return; } // 0b
  uint32_t u = resolve(H[f].x), w = H[f].y;
  uint8_t tu = H[u].tag;
  if (tu == LEAF) { H[i].tag = IND; H[i].x = w; return; }                       // rule 1
  if (tu == STEM) {                                                             // rule 2 (fork)
    uint32_t x = H[u].x, a = H[i].y;
    uint32_t L = mk(c, APP, x, a), R = mk(c, APP, w, a);
    H[i].tag = APP; H[i].x = L; H[i].y = R;
    out.push_back(L); out.push_back(R); out.push_back(i);
    return;
  }
  uint32_t a = resolve(H[i].y), p = H[u].x, q = H[u].y;
  uint8_t ta = H[a].tag;
  if (ta == LEAF) { H[i].tag = IND; H[i].x = p; return; }                       // 3a
  if (ta == STEM) { H[i].tag = APP; H[i].x = q; H[i].y = H[a].x; out.push_back(i); return; } // 3b
  uint32_t inner = mk(c, APP, w, H[a].x);                                        // 3c
  H[i].tag = APP; H[i].x = inner; H[i].y = H[a].y;
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
  switch (H[i].tag) {
    case LEAF: out.push_back('0'); break;
    case STEM: out.push_back('1'); to_ternary(H[i].x, out); break;
    case FORK: out.push_back('2'); to_ternary(H[i].x, out); to_ternary(H[i].y, out); break;
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

  // Two flat banks of live-app indices (mmap, lazily paged), toggled each round.
  // The next bank is filled by parallel atomic batch-append — no serial merge.
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

  uint64_t rounds = 0, work = 0;
  std::atomic<uint32_t> nnext{0};
  std::atomic<uint64_t> work_a{0};

  #pragma omp parallel
  {
    Ctx c;
    std::vector<uint32_t> rdy, keepout;
    for (;;) {
      rdy.clear(); keepout.clear();
      uint32_t *C = bank[cur], *N = bank[cur ^ 1];
      // --- Phase A (reads only): partition into ready (this thread reduces) vs blocked (carry over). ---
      #pragma omp for schedule(static) nowait
      for (uint32_t k = 0; k < ncur; ++k) {
        uint32_t i = C[k];
        if (ready(i)) rdy.push_back(i);
        else keepout.push_back(i);
      }
      #pragma omp barrier
      // --- Phase B (writes): reduce ready nodes; new/still-live apps go to keepout. ---
      for (uint32_t i : rdy) reduce(c, i, keepout);
      work_a.fetch_add(rdy.size(), std::memory_order_relaxed);
      // --- Phase C: parallel batch-append into the next bank (one atomic per thread). ---
      uint32_t base = nnext.fetch_add((uint32_t)keepout.size(), std::memory_order_relaxed);
      if (!keepout.empty()) memcpy(N + base, keepout.data(), keepout.size() * sizeof(uint32_t));
      #pragma omp barrier
      #pragma omp single
      { cur ^= 1; ncur = nnext.load(); nnext.store(0); rounds++; }
      #pragma omp barrier
      if (ncur == 0) break;
    }
  }
  work = work_a.load();
  munmap(bank[0], CAP * sizeof(uint32_t)); munmap(bank[1], CAP * sizeof(uint32_t));

  if (getenv("DBG"))
    fprintf(stderr, "rounds=%llu work~=%llu nodes=%u threads=%d\n",
            (unsigned long long)rounds, (unsigned long long)work, TOP.load(), omp_get_max_threads());

  std::string out_s; to_ternary(root, out_s);
  std::cout << out_s << "\n";
  return 0;
}
