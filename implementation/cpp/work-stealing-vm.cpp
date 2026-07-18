// Work-stealing parallel VM for tree calculus.
//
// The frontier reducer proved equal's parallelism is real but paid ~5x by
// materializing an application node per reduction. This reducer keeps the
// *sequential champion's* representation and recursion — apply() is an ordinary
// recursive function on the native stack, so an un-stolen fork costs nothing
// beyond a deque push/pop and builds no heap application node. Parallelism comes
// only from rule 2:
//
//     apply(fork(stem u', y), b) = apply( apply(u',b), apply(y,b) )
//
// The right branch apply(y,b) is made stealable (a stack-allocated Task pushed to
// a Chase-Lev deque); this thread recurses into the left branch. On return it
// tries to pop the right branch back:
//   - not stolen  -> run it inline (the common, deep, fine-grained case: cheap)
//   - stolen      -> a thief is running it; join on an atomic result slot, and
//                    help by stealing other work while waiting (the later
//                    finisher continues with the combine apply(L,R)).
// Only *stolen* forks touch shared state, and steals are bounded by
// cores x span, so heap/atomic traffic is O(steals), not O(work).
//
// Nodes (champion's nil layout): {u,v}; u==0 leaf, v==0 stem, else fork. Index 0
// reserved, leaf at 1. Nodes are immutable once built, so cross-thread reads need
// no synchronization; the only shared mutable state is the deques, the per-fork
// atomic result slot, the arena bump pointer, and a done flag.
//
// OUTCOME (see benchmark/BREAKING-RECORDS.md): it works and materializes no
// application nodes, but it LOSES on `equal`. Single thread is ~2.5x slower than
// the champion (a deque push/pop with a fence at every rule-2, millions of them),
// and it scales *negatively* (P=2 slower than P=1). Cause is structural, not a
// tuning bug: equal's parallelism is fine-grained and *deep*, while work-stealing
// steals the *oldest/shallowest* tasks — which for equal are junk fixpoint-
// machinery forks. Idle workers hammer the one busy deque (the fold), steal tiny
// tasks, finish instantly, and re-poll, contending on its head/tail. Work-stealing
// wants coarse, top-concentrated parallelism; the bulk-synchronous frontier
// (parallel-frontier.cpp) is the right engine for this workload.
//
// Build:  clang++ work-stealing-vm.cpp -O3 -std=c++23 -stdlib=libc++ -pthread -o work-stealing-vm
// Usage:  stdin/stdout ternary like the others; env WSVM_THREADS (default: cores).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <sys/mman.h>
#include <pthread.h>

struct Node { uint32_t u, v; };
static constexpr size_t CAP = size_t(1) << 31;
static constexpr uint32_t CHUNK = 1u << 12;
static Node *A;
static std::atomic<uint32_t> ATOP{2};                 // 0 reserved, 1 = leaf
static std::atomic<bool> DONE{false};

static constexpr uint32_t PENDING = 0xffffffffu;
struct Task { uint32_t a, b; std::atomic<uint32_t> res; };

// Chase-Lev work-stealing deque of Task* (bounded ring buffer).
static constexpr int DQSZ = 1 << 20, DQMASK = DQSZ - 1;
struct Deque {
  std::atomic<int64_t> top{0}, bot{0};
  Task **buf;
  void init() { buf = new Task *[DQSZ]; }
  void push(Task *t) {                                 // owner
    int64_t b = bot.load(std::memory_order_relaxed);
    buf[b & DQMASK] = t;
    std::atomic_thread_fence(std::memory_order_release);
    bot.store(b + 1, std::memory_order_relaxed);
  }
  Task *pop() {                                        // owner
    int64_t b = bot.load(std::memory_order_relaxed) - 1;
    bot.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t t = top.load(std::memory_order_relaxed);
    if (t <= b) {
      Task *task = buf[b & DQMASK];
      if (t == b) {
        if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
          task = nullptr;                              // lost the last item to a thief
        bot.store(b + 1, std::memory_order_relaxed);
      }
      return task;
    }
    bot.store(b + 1, std::memory_order_relaxed);       // empty
    return nullptr;
  }
  Task *steal() {                                      // thief
    int64_t t = top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t b = bot.load(std::memory_order_acquire);
    if (t < b) {
      Task *task = buf[t & DQMASK];
      if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        return nullptr;                                // lost the race
      return task;
    }
    return nullptr;
  }
};

struct alignas(64) Worker {
  Deque dq;
  uint32_t cur = 0, end = 0;    // allocation chunk
  unsigned rng;                 // xorshift for victim selection
};
static Worker *WK;
static int P;

static inline uint32_t alloc(Worker &w, uint32_t u, uint32_t v) {
  if (w.cur >= w.end) { w.cur = ATOP.fetch_add(CHUNK, std::memory_order_relaxed); w.end = w.cur + CHUNK; }
  uint32_t id = w.cur++;
  A[id] = {u, v};
  return id;
}
static inline uint32_t leaf() { return 1; }
static inline uint32_t stem(Worker &w, uint32_t u) { return alloc(w, u, 0); }
static inline uint32_t fork_(Worker &w, uint32_t u, uint32_t v) { return alloc(w, u, v); }

static uint32_t apply(Worker &w, uint32_t a, uint32_t b);

static inline void execute(Worker &w, Task *t) {
  t->res.store(apply(w, t->a, t->b), std::memory_order_release);
}
static inline Task *steal_random(Worker &w) {
  for (int tries = 0; tries < P; ++tries) {
    w.rng ^= w.rng << 13; w.rng ^= w.rng >> 17; w.rng ^= w.rng << 5;
    int v = w.rng % P;
    if (&WK[v] != &w) { Task *t = WK[v].dq.steal(); if (t) return t; }
  }
  return nullptr;
}
// Wait for a stolen task's result, helping with other work meanwhile
// (own pending work first, then stealing, with exponential backoff when idle).
static uint32_t join(Worker &w, Task *t) {
  int backoff = 1;
  for (;;) {
    uint32_t r = t->res.load(std::memory_order_acquire);
    if (r != PENDING) return r;
    Task *h = w.dq.pop();
    if (!h) h = steal_random(w);
    if (h) { execute(w, h); backoff = 1; }
    else { for (int k = 0; k < backoff; ++k) __builtin_ia32_pause(); if (backoff < 1024) backoff <<= 1; }
  }
}

static uint32_t apply(Worker &w, uint32_t a, uint32_t b) {
  Node na = A[a];
  if (na.u == 0) return stem(w, b);            // apply(leaf, b)   = stem(b)
  if (na.v == 0) return fork_(w, na.u, b);     // apply(stem u, b) = fork(u, b)
  uint32_t u = na.u, y = na.v;
  Node nu = A[u];
  if (nu.u == 0) return y;                      // rule 1
  if (nu.v == 0) {                              // rule 2 — the fork point
    uint32_t u1 = nu.u;
    Task tR{y, b, {PENDING}};
    w.dq.push(&tR);
    uint32_t L = apply(w, u1, b);
    uint32_t R;
    Task *got = w.dq.pop();
    if (got == &tR) R = apply(w, y, b);         // reclaimed: run inline (cheap, common)
    else R = join(w, &tR);                      // stolen: join + help
    return apply(w, L, R);
  }
  // rule 3: u = fork(w', x'), triage on b
  uint32_t wc = nu.u, xc = nu.v;
  Node nb = A[b];
  if (nb.u == 0) return wc;                      // b = leaf
  if (nb.v == 0) return apply(w, xc, nb.u);      // b = stem d
  return apply(w, apply(w, y, nb.u), nb.v);      // b = fork d e
}

// ---- ternary I/O (single-threaded, worker 0) ----
static uint32_t of_ternary(Worker &w, const std::string &s) {
  std::vector<uint32_t> st;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    char c = *it;
    if (c == '0') st.push_back(leaf());
    else if (c == '1') { uint32_t x = st.back(); st.pop_back(); st.push_back(stem(w, x)); }
    else if (c == '2') { uint32_t x = st.back(); st.pop_back(); uint32_t z = st.back(); st.pop_back(); st.push_back(fork_(w, x, z)); }
  }
  return st.back();
}
static void to_ternary(uint32_t x, std::string &out) {
  Node n = A[x];
  if (n.u == 0) { out.push_back('0'); return; }
  if (n.v == 0) { out.push_back('1'); to_ternary(n.u, out); return; }
  out.push_back('2'); to_ternary(n.u, out); to_ternary(n.v, out);
}

static void worker_main(int id) {
  Worker &w = WK[id];
  int backoff = 1;
  while (!DONE.load(std::memory_order_acquire)) {
    Task *t = steal_random(w);
    if (t) { execute(w, t); backoff = 1; }
    else { for (int k = 0; k < backoff; ++k) __builtin_ia32_pause(); if (backoff < 4096) backoff <<= 1; }
  }
}

int main() {
  P = 1;
  if (const char *e = getenv("WSVM_THREADS")) P = atoi(e);
  if (P < 1) P = 1;

  void *p = mmap(nullptr, CAP * sizeof(Node), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); return 1; }
  A = static_cast<Node *>(p);
  A[1] = {0, 0};

  WK = new Worker[P];
  for (int i = 0; i < P; ++i) { WK[i].dq.init(); WK[i].rng = 0x9e3779b9u + i * 2654435761u; }

  Worker &w0 = WK[0];
  std::vector<uint32_t> inputs;
  std::string line;
  while (std::getline(std::cin, line)) if (!line.empty()) inputs.push_back(of_ternary(w0, line));

  // Launch stealers with a large stack (deep recursion + help nesting).
  std::vector<pthread_t> th(P);
  pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, size_t(1) << 30);
  struct Arg { int id; };
  std::vector<Arg> args(P);
  auto trampoline = [](void *a) -> void * { worker_main(((Arg *)a)->id); return nullptr; };
  for (int i = 1; i < P; ++i) { args[i].id = i; pthread_create(&th[i], &attr, trampoline, &args[i]); }

  uint32_t result = of_ternary(w0, "21100");
  for (uint32_t t : inputs) result = apply(w0, result, t);

  DONE.store(true, std::memory_order_release);
  for (int i = 1; i < P; ++i) pthread_join(th[i], nullptr);

  std::string out; to_ternary(result, out);
  std::cout << out << "\n";
  return 0;
}
