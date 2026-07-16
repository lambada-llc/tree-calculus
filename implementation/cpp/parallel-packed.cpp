// Parallel fork-join tree-calculus reducer.
//
// Tree calculus is confluent: independent redexes may be reduced in any order,
// or simultaneously, without changing the result. The duplicating rule
//
//     apply(fork(stem u', y), b) = apply( apply(u', b), apply(y, b) )
//
// has two *independent* sub-applies, apply(u',b) and apply(y,b). We evaluate
// them on different cores. This is a classic fork-join: spawn one branch as a
// task, run the other inline, join, then perform the (dependent) outer apply.
//
// Granularity: we only fork while a *branch-depth* counter is below BCUT, so at
// most ~2^BCUT coarse tasks are created near the top of the computation and
// each runs fully sequentially. That keeps task-creation overhead negligible
// while giving the scheduler enough independent work to fill every core.
//
// Memory: one shared mmap arena. Each worker thread bump-allocates from a
// private chunk it claimed with a single atomic fetch_add, so the allocation
// hot path takes no locks and no per-node atomics. Nodes are immutable once
// their id is returned, so cross-thread reads need no synchronization.
//
// Node layout (identical to eager-packed.hpp): one 64-bit word,
//   [0:2] tag (0 leaf / 1 stem / 2 fork), [2:33] u (31b), [33:64] v (31b).
//
// Build:  clang++ parallel-packed.cpp -O3 -std=c++23 -stdlib=libc++ -fopenmp -o parallel-packed
// Usage:  same stdin/stdout contract as cpp/main.exe (ternary lines, left-fold).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <omp.h>

static constexpr size_t CAP = size_t(1) << 31;     // 2^31 nodes (16 GB virtual)
static constexpr uint32_t CHUNK = 1u << 12;        // 4k nodes per chunk claim
static int BCUT = 6;                               // branch-depth fork cutoff
static int MAXTASKS = 256;                          // hard cap on total spawned tasks

static uint64_t *ARENA;
static std::atomic<uint32_t> HWM{0};               // global allocation high-water mark
static std::atomic<int> SPAWNED{0};                // total tasks spawned (granularity cap)

// Allocator context: a private bump chunk. Passed by reference through the
// recursion (lives in a register), so no thread-local storage on the hot path.
// A spawned task gets a fresh Ctx and claims its own chunks, so two threads
// never bump the same region.
struct Ctx { uint32_t cur = 0, end = 0; };

static inline uint32_t alloc(Ctx &c, uint64_t w) {
  if (c.cur >= c.end) {
    c.cur = HWM.fetch_add(CHUNK, std::memory_order_relaxed);
    c.end = c.cur + CHUNK;
  }
  uint32_t id = c.cur++;
  ARENA[id] = w;
  return id;
}

static inline uint32_t tag(uint32_t x) { return ARENA[x] & 3u; }
static inline uint32_t uc(uint32_t x)  { return (ARENA[x] >> 2)  & 0x7fffffffu; }
static inline uint32_t vc(uint32_t x)  { return (ARENA[x] >> 33) & 0x7fffffffu; }

static inline uint32_t leaf() { return 0; }
static inline uint32_t stem(Ctx &c, uint32_t u) { return alloc(c, 1 | (uint64_t(u) << 2)); }
static inline uint32_t fork(Ctx &c, uint32_t u, uint32_t v) {
  return alloc(c, 2 | (uint64_t(u) << 2) | (uint64_t(v) << 33));
}

static uint32_t apply(Ctx &c, uint32_t a, uint32_t b, int bdepth) {
  for (;;) {
    switch (tag(a)) {
      case 0: return stem(c, b);          // apply(leaf, b)   = stem(b)
      case 1: return fork(c, uc(a), b);   // apply(stem u, b) = fork(u, b)
      default: {
        uint32_t u = uc(a), y = vc(a);
        switch (tag(u)) {
          case 0: return y;               // apply(fork(leaf,y), b) = y
          case 1: {                       // apply(fork(stem u',y), b) — the fork-join
            uint32_t u1 = uc(u);
            uint32_t l, r;
            if (bdepth < BCUT &&
                SPAWNED.load(std::memory_order_relaxed) < MAXTASKS &&
                SPAWNED.fetch_add(1, std::memory_order_relaxed) < MAXTASKS) {
              Ctx cl; // task's own allocator
              #pragma omp task shared(l, cl) firstprivate(u1, b, bdepth) default(none)
              l = apply(cl, u1, b, bdepth + 1);
              r = apply(c, y, b, bdepth + 1);
              #pragma omp taskwait
            } else {
              // Sequential fallthrough: force bdepth to BCUT so descendants
              // skip the spawn check entirely (no atomics on the hot path).
              l = apply(c, u1, b, BCUT);
              r = apply(c, y, b, BCUT);
            }
            // Tail-position dependent apply: loop instead of recursing.
            a = l; b = r;
            continue;
          }
          default:                        // apply(fork(fork(w,x),y), b) — triage b
            switch (tag(b)) {
              case 0: return uc(u);                            // b = leaf  -> w
              case 1: { a = vc(u); b = uc(b); continue; }      // b = stem  -> apply(x,d)
              default: {                                       // b = fork  -> apply(apply(y,d),e)
                uint32_t d = uc(b), e = vc(b);
                a = apply(c, y, d, bdepth); b = e; continue;
              }
            }
        }
      }
    }
  }
}

// ---- ternary I/O (matches evaluator.hpp) ----
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
  switch (tag(x)) {
    case 0: out.push_back('0'); break;
    case 1: out.push_back('1'); to_ternary(uc(x), out); break;
    default: out.push_back('2'); to_ternary(uc(x), out); to_ternary(vc(x), out); break;
  }
}

int main() {
  if (const char *e = getenv("BCUT")) BCUT = atoi(e);
  void *p = mmap(nullptr, CAP * sizeof(uint64_t), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); return 1; }
  ARENA = static_cast<uint64_t *>(p);
  Ctx c0;
  alloc(c0, 0); // leaf at id 0

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
