// reduce_canonicalize.cpp — reduce every binding of a module, hash-consed
//
//   reduce_canonicalize [--fuel=<steps>] [--stats-per-symbol] < module > reduced
//
// Reads a DAG module, reduces each application as it is read (memoized, so a
// shared sub-term is reduced once), and writes the module back with every
// binding in normal form: nodes as `:N` ids, hash-consed, unreachable ones
// dropped, and every naming (2-word) line and terminator kept, in order.
//
// The eager runner's `load` + `dump` (runner.cpp) also normalizes a whole
// module, and this is not that, for what its output says: a node built by
// applying a name another line defines refers to it *by that name*, so the
// reduced module still says which symbol each part was built through. A dump
// keys nodes by value and writes structural `~` ids, which is right for a cache
// and loses exactly that attribution. Also its own: `--fuel` caps reduction
// steps (exit 1 when spent), so a non-terminating binding fails a build rather
// than hanging it; `--stats-per-symbol` writes `Symbol,steps` CSV to stderr —
// the contractions each exported binding cost — ending in a `TOTAL` row.
//
// Representation (the algorithm and its traversal order are what fix the
// output and the step counts, so those are untouched; only the data structures
// behind them changed: 7.8x fewer instructions, and on forest's library bundle
// 2.8 s became 0.4 s with the memo in cache and 0.7–1.1 s with it in DRAM):
//
// - Two id spaces. A *node* id names a term being reduced: the N interned
//   names first (the input is tokenized up front, so N is known), temporaries
//   after. A *cid* names what gets written: a name (cid < N, written as that
//   name) or a constructed node (cid >= N, written as `:(cid - N)`). Keys of
//   the memo are pairs of cids, values are node ids.
// - One 16-byte record per node: children, representative (the node this one
//   reduced to, itself if none) and canonical cid (or -1). The constructor is
//   read off the children — a leaf has none, a stem a left one — not stored.
// - The memo is a flat open-addressed table of 12-byte slots (packed key,
//   value), linear probing, Fibonacci hashing, empty = key 0, which a zeroed
//   (lazily faulted) allocation gives for free. It is sized
//   from the input up front (128 slots a line: forest's library needs ~70
//   entries a line, and the table stays under half full). A pop does one
//   probe, and the slot a miss stops at is the slot the result goes in; a task
//   pushed back for other results places its key there with a placeholder, so
//   finishing it later is a store, not another probe. The stock
//   std::unordered_map (a malloc'd node per entry, bucket chains, two or three
//   lookups per pop) was where most of the time went.
// - There is no separate hash-consing table: a key reaches construction only
//   after missing in the memo, and both tables were inserted together, so the
//   consing lookup never hit. Every construction is new.
// - A child task's memo key is computed when it is pushed (its operands are
//   finished nodes, whose canonical ids never change) and its slot prefetched,
//   so the probe's miss overlaps the work before its pop. A task pushed back
//   gets its key when popped: it depends on the results it waited for.
// - stderr is buffered (the stats are ~50k rows; unbuffered std::cerr made each
//   one several syscalls), and flushed before the fuel message.
//
// Measured on forest's library bundle (4.5M pops, 3.8M memo entries), each
// against the version without it: huge pages 1.3x with warm blocks, 0.4x with
// cold ones on a VM whose balloon reports freed memory to its host (see
// big_alloc), so they are opt-in; growing the memo from small by doubling
// instead of sizing it up front -22%; 16-byte
// aligned slots instead of 12-byte packed -4%; the keys and values in two
// arrays (a probe reads only keys) -4%, as a hit or an insert then touches
// two lines; no prefetch of a pushed child's slot -4%. What remains is one
// dependent cache miss per pop: a task's key is known only once the tasks it
// waited for are done, so the misses of a chain of them cannot overlap.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#ifdef __linux__
#include <sys/mman.h>
#endif

// ── Memory ──────────────────────────────────────────────────────────────────
// Zeroed, 2 MB aligned, and huge-page advised on RUNNER_HUGEPAGES (as the
// runner's evaluator is; see runner.md): the memo is tens of MB probed at
// random, so a TLB entry per 4 KB page misses on nearly every probe, and
// forest's library took 0.42 s on warm huge pages against 0.53 s on small
// ones — but 1.22 s on huge pages a VM's balloon had handed back to the host,
// the state a build's reducer usually starts in.

static bool huge_pages() {
  const char* env = std::getenv("RUNNER_HUGEPAGES");
  return env && *env && std::strcmp(env, "0") != 0;
}

static void* big_alloc(size_t bytes) {
  const size_t align = 2u << 20;
  bytes = (bytes + align - 1) / align * align;
#ifdef __linux__
  void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); std::exit(1); }
  if (huge_pages()) madvise(p, bytes, MADV_HUGEPAGE);
#else
  void* p = calloc(bytes, 1);
  if (!p) { perror("calloc"); std::exit(1); }
#endif
  return p;
}

static void big_free(void* p, size_t bytes) {
#ifdef __linux__
  const size_t align = 2u << 20;
  munmap(p, (bytes + align - 1) / align * align);
#else
  free(p);
#endif
}

// A growable array of trivially copyable T, allocated by big_alloc.
template <class T> struct Arr {
  T* p = nullptr;
  size_t n = 0, cap = 0;
  void reserve(size_t want) {
    if (want <= cap) return;
    size_t c = cap ? cap : 1024;
    while (c < want) c *= 2;
    T* q = (T*)big_alloc(c * sizeof(T));
    if (n) memcpy(q, p, n * sizeof(T));
    if (p) big_free(p, cap * sizeof(T));
    p = q; cap = c;
  }
  T& operator[](size_t i) { return p[i]; }
  void push(const T& v) { if (n == cap) reserve(n + 1); p[n++] = v; }
};

// ── Flat hash map, uint64 key (0 = empty) ───────────────────────────────────

template <class V> struct FlatMap {
  struct __attribute__((packed)) Slot { uint64_t key; V val; }; // 12 bytes: fewer lines per probe beat alignment
  Slot* slots = nullptr;
  size_t mask = 0, count = 0, limit = 0, bytes = 0;
  int shift = 64;

  void init(size_t min_slots) {
    size_t cap = 1024;
    while (cap < min_slots) cap *= 2;
    bytes = cap * sizeof(Slot);
    slots = (Slot*)big_alloc(bytes);
    mask = cap - 1;
    limit = cap / 2;
    shift = 64 - __builtin_ctzll(cap);
    count = 0;
  }
  size_t home(uint64_t key) const { return (key * 0x9e3779b97f4a7c15ULL) >> shift; }
  // The slot holding key, or the empty slot the probe stopped at.
  Slot* probe(uint64_t key) const {
    size_t i = home(key);
    while (slots[i].key && slots[i].key != key) i = (i + 1) & mask;
    return &slots[i];
  }
  // Fill an empty slot returned by probe(). Invalidates slot pointers if it grows.
  void insert_at(Slot* s, uint64_t key, V v) {
    s->key = key; s->val = v;
    if (++count > limit) grow();
  }
  // Where key now lives, after a grow.
  size_t find(uint64_t key) const {
    size_t i = home(key);
    while (slots[i].key != key) i = (i + 1) & mask;
    return i;
  }
  void grow();
};

// cids are non-negative, so bit 63 marks a packed pair as a non-empty key.
static inline uint64_t pair_key(int32_t cf, int32_t ca) {
  return ((uint64_t)(uint32_t)cf << 32 | (uint32_t)ca) | (1ULL << 63);
}

// ── Names ───────────────────────────────────────────────────────────────────
// Interned in input order; named ids are 0..N-1 in both id spaces. The strings
// stay in the input buffer.

struct Word { const char* p; uint32_t len; };
static std::vector<Word> words;
static FlatMap<int32_t> word_ids; // key: hash of the bytes (collisions resolved below)
static std::vector<int32_t> word_chain; // id -> next id with the same key, or -1

static uint64_t str_hash(const char* p, size_t n) {
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 1099511628211ULL; }
  return h | 1; // non-zero
}

static int32_t intern(const char* p, size_t n) {
  uint64_t h = str_hash(p, n);
  FlatMap<int32_t>::Slot* s = word_ids.probe(h);
  if (s->key) {
    for (int32_t id = s->val; id >= 0; id = word_chain[id])
      if (words[id].len == n && memcmp(words[id].p, p, n) == 0) return id;
  }
  int32_t id = (int32_t)words.size();
  words.push_back({p, (uint32_t)n});
  word_chain.push_back(s->key ? s->val : -1);
  if (s->key) s->val = id; else word_ids.insert_at(s, h, id);
  return id;
}

// ── Core state ──────────────────────────────────────────────────────────────

struct Node {
  int32_t a, b;    // children: a < 0 leaf; b < 0 stem; else fork (a = left, b = right)
  int32_t rep;     // the node this one reduced to, itself if none (so no test on the way)
  int32_t canon;   // cid, or -1
};
static Arr<Node> nodes;
static int32_t N = 0;         // number of names; first temporary / canon id
static int32_t ncanon = 0;    // constructed nodes so far
struct Def { int32_t l, r; };
static Arr<Def> cdef;         // per constructed node: (left cid, right cid)
static FlatMap<int32_t> memo; // (cid, cid) -> node id

static int64_t fuel = INT64_MAX;
static int32_t current_lineno = 0;
static bool stats_per_symbol = false;
static int64_t stat_contractions = 0;

struct OutLine { int32_t w1, w2, w3; int32_t n; }; // n words; cids
static Arr<OutLine> out_lines;
static std::vector<int32_t> roots; // cids

static inline int32_t fresh_temp() {
  int32_t id = (int32_t)nodes.n;
  nodes.push({-1, -1, id, -1});
  return id;
}

// A node is only ever linked to a representative (a memo value, or a child
// resolved first), so one hop is the rule. A longer chain arises only when a
// name is defined twice (its link is reset, and nodes linked to it before
// then lead on through it): that case follows the chain and compresses it,
// which is observable — a reset of a name half-way along a chain cuts an
// uncompressed chain there — so it is done exactly when the stock did it.
static inline int32_t resolve(int32_t id) {
  int32_t r = nodes.p[id].rep;
  if (__builtin_expect(nodes.p[r].rep != r, 0)) {
    while (nodes.p[r].rep != r) r = nodes.p[r].rep;
    for (int32_t i = id; nodes.p[i].rep != i;) { int32_t next = nodes.p[i].rep; nodes.p[i].rep = r; i = next; }
  }
  return r;
}

static inline int32_t canonical(int32_t id) {
  int32_t r = resolve(id);
  int32_t c = nodes.p[r].canon;
  return c >= 0 ? c : r;
}

static inline int32_t new_canon(int32_t cf, int32_t ca) {
  int32_t cid = N + ncanon++;
  cdef.push({cf, ca});
  out_lines.push({cid, cf, ca, 3});
  return cid;
}

// Tasks, and the side stack of memo slots owed a value: a task pushed back
// for the result of others has its key placed in the slot its probe found,
// with a placeholder value, so finishing it is a store rather than a probe.
// A child's key is known when it is pushed (its operands are finished nodes,
// whose canonical ids never change), so it is computed then and its slot
// prefetched; a task pushed back has key 0 and computes it when popped.
struct Task { int32_t target, func, arg; uint32_t pend_begin; uint64_t key; };
static Arr<Task> stack;
static Arr<uint32_t> pend;
static const int32_t PENDING = -1;

template <class V> void FlatMap<V>::grow() {
  Slot* old = slots; size_t old_cap = mask + 1, old_bytes = bytes;
  init(old_cap * 2);
  for (size_t i = 0; i < old_cap; ++i) {
    if (!old[i].key) continue;
    size_t j = home(old[i].key);
    while (slots[j].key) j = (j + 1) & mask;
    slots[j] = old[i];
    ++count;
  }
  if ((void*)this == (void*)&memo) // the owed slots moved with their keys
    for (size_t i = 0; i < pend.n; ++i) pend.p[i] = (uint32_t)find(old[pend.p[i]].key);
  big_free(old, old_bytes);
}

// Place key in the (empty, or already its own) slot its probe found, owed a value.
static inline void owe(FlatMap<int32_t>::Slot* s, uint64_t key) {
  pend.push((uint32_t)(s - memo.slots));
  if (!s->key) memo.insert_at(s, key, PENDING);
}

static inline uint64_t key_of(int32_t func, int32_t arg) {
  uint64_t key = pair_key(canonical(func), canonical(arg));
  __builtin_prefetch(&memo.slots[memo.home(key)]);
  return key;
}

static std::string err_buf; // stderr, written out at exit

static void append_num(std::string& s, int64_t v) {
  char buf[24]; char* e = buf + sizeof buf; char* q = e;
  uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
  do { *--q = '0' + u % 10; u /= 10; } while (u);
  if (v < 0) *--q = '-';
  s.append(q, e - q);
}

[[noreturn]] static void fuel_exhausted() {
  fwrite(err_buf.data(), 1, err_buf.size(), stderr);
  err_buf.clear();
  std::cerr << "fuel exhausted";
  if (current_lineno > 0) std::cerr << " at line " << current_lineno;
  std::cerr << std::endl;
  std::exit(1);
}

// Record the task's result for its own key (in the slot its probe found) and
// in every slot it owes.
static inline void finish(FlatMap<int32_t>::Slot* s, uint64_t key, uint32_t pend_begin, int32_t result) {
  if (s) { if (s->key) s->val = result; else memo.insert_at(s, key, result); }
  for (uint32_t i = pend_begin; i < pend.n; ++i) memo.slots[pend.p[i]].val = result;
  pend.n = pend_begin;
}

static void process_apply(int32_t target, int32_t func, int32_t arg) {
  stack.n = 0;
  stack.push({target, func, arg, (uint32_t)pend.n, 0});
  while (stack.n) {
    if (fuel == 0) fuel_exhausted();
    --fuel;
    Task t = stack.p[--stack.n];
    stack.reserve(stack.n + 3);

    uint64_t key = t.key ? t.key : pair_key(canonical(t.func), canonical(t.arg));
    FlatMap<int32_t>::Slot* s = memo.probe(key);
    if (s->key && s->val != PENDING) {
      int32_t prev = s->val;
      Node& tn = nodes.p[t.target];
      tn.a = nodes.p[prev].a; tn.b = nodes.p[prev].b;
      tn.rep = prev;
      finish(nullptr, 0, t.pend_begin, prev);
      continue;
    }

    nodes.p[t.target].rep = t.target;
    Node f = nodes.p[t.func];
    if (f.b < 0) {                                        // leaf: build a stem; stem: build a fork
      // The node's children are canonicalized after the reset above, not
      // before as for the key: for a redefined name that is its own operand
      // the two differ, and the stock wrote the node from the later reading.
      int32_t cf = canonical(t.func), ca = canonical(t.arg);
      Node& tn = nodes.p[t.target];
      if (f.a < 0) { tn.a = t.arg; tn.b = -1; } else { tn.a = f.a; tn.b = t.arg; }
      tn.canon = new_canon(cf, ca);
      finish(s, key, t.pend_begin, t.target);
    } else {                                              // fork: contract
      ++stat_contractions;
      Node u = nodes.p[f.a];
      if (u.a < 0) {                                      // △ (△ b) a = b
        int32_t rb = resolve(f.b);
        Node& tn = nodes.p[t.target];
        tn.a = nodes.p[rb].a; tn.b = nodes.p[rb].b; tn.rep = rb;
        finish(s, key, t.pend_begin, rb);
      } else if (u.b < 0) {                               // △ (△ (△ x) y) z = (x z) (y z)
        int32_t t1 = fresh_temp();
        int32_t t2 = fresh_temp();
        owe(s, key);
        stack.p[stack.n++] = {t.target, t1, t2, t.pend_begin, 0};
        stack.p[stack.n++] = {t2, f.b, t.arg, (uint32_t)pend.n, key_of(f.b, t.arg)};
        stack.p[stack.n++] = {t1, u.a, t.arg, (uint32_t)pend.n, key_of(u.a, t.arg)};
      } else {
        Node c = nodes.p[t.arg];
        if (c.a < 0) {                                    // △ (△ (△ w x) y) △ = w
          int32_t ra = resolve(u.a);
          Node& tn = nodes.p[t.target];
          tn.a = nodes.p[ra].a; tn.b = nodes.p[ra].b; tn.rep = ra;
          finish(s, key, t.pend_begin, ra);
        } else if (c.b < 0) {                             // △ (△ (△ w x) y) (△ u) = x u
          owe(s, key);
          stack.p[stack.n++] = {t.target, u.b, c.a, t.pend_begin, key_of(u.b, c.a)};
        } else {                                          // △ (△ (△ w x) y) (△ u v) = y u v
          int32_t tt = fresh_temp();
          owe(s, key);
          stack.p[stack.n++] = {t.target, tt, c.b, t.pend_begin, 0};
          stack.p[stack.n++] = {tt, f.b, c.a, (uint32_t)pend.n, key_of(f.b, c.a)};
        }
      }
    }
  }
}

static void append_name(std::string& s, int32_t cid) {
  if (cid < N) s.append(words[cid].p, words[cid].len);
  else { s += ':'; append_num(s, cid - N); }
}

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--stats-per-symbol") stats_per_symbol = true;
    else if (arg.rfind("--fuel=", 0) == 0) {
      int64_t f = std::stoll(arg.substr(7));
      fuel = f < 0 ? INT64_MAX : f;
    } else {
      std::cerr << "reduce_canonicalize: unrecognized argument " << arg << "\n"
                << "usage: reduce_canonicalize [--fuel=<steps>] [--stats-per-symbol] < module\n";
      return 2;
    }
  }

  // Read all input at once; the names are served from this buffer.
  std::string input;
  for (size_t got = 0;;) {
    input.resize(got + (1 << 20));
    size_t n = fread(&input[got], 1, 1 << 20, stdin);
    got += n;
    if (n == 0) { input.resize(got); break; }
  }
  const char* p = input.data();
  const char* end = p + input.size();

  // Tokenize every line up front (so the number of names is known before any
  // temporary is made): up to 4 words a line, more are dropped.
  struct Line { int32_t w[3]; int32_t n; };
  std::vector<Line> lines;
  lines.reserve(input.size() / 16 + 16);
  word_ids.init(1 << 16);
  words.reserve(1 << 16);
  word_chain.reserve(1 << 16);
  int32_t leaf_id = intern("\xe2\x96\xb3", 3); // △
  (void)leaf_id;
  while (p < end) {
    while (p < end && *p == ' ') ++p;
    if (p >= end) break;
    if (*p == '\n') { ++p; continue; }
    Line l = {{-1, -1, -1}, 0};
    while (l.n < 4 && p < end && *p != '\n') {
      const char* ws = p;
      while (p < end && *p != ' ' && *p != '\n') ++p;
      if (l.n < 3) l.w[l.n] = intern(ws, p - ws);
      ++l.n;
      while (p < end && *p == ' ') ++p;
    }
    while (p < end && *p != '\n') ++p;
    if (p < end) ++p;
    lines.push_back(l);
  }
  N = (int32_t)words.size();

  nodes.reserve((size_t)N + 64 * lines.size() + 1024);
  for (int32_t i = 0; i < N; ++i) nodes.p[i] = {-1, -1, i, -1};
  nodes.n = N;
  // forest's library: ~70 memo entries a line; a table at most half full.
  memo.init(128 * lines.size() + 1024);
  cdef.reserve(64 * lines.size() + 1024);
  out_lines.reserve(32 * lines.size() + 1024);
  stack.reserve(1 << 12);
  pend.reserve(1 << 12);

  std::vector<int64_t> sym_contractions(N, 0);
  FlatMap<int64_t> delta_cache; // (cid, cid) at the line -> its contraction delta
  if (stats_per_symbol) { delta_cache.init(4 * lines.size() + 1024); err_buf += "Symbol,steps\n"; }

  for (const Line& l : lines) {
    ++current_lineno;
    int32_t a = l.w[0];
    if (l.n == 1) {                                                   // terminal
      int32_t ca = canonical(a);
      roots.push_back(ca);
      out_lines.push({ca, 0, 0, 1});
      if (stats_per_symbol) {
        append_name(err_buf, a); err_buf += ','; append_num(err_buf, sym_contractions[a]); err_buf += '\n';
        sym_contractions[a] = 0;
      }
      continue;
    }
    int32_t b = l.w[1];
    if (l.n == 2) {                                                   // alias/export
      Node& na = nodes.p[a];
      na.a = nodes.p[b].a; na.b = nodes.p[b].b;
      na.rep = a;
      na.canon = a;
      int32_t cb = canonical(b);
      roots.push_back(cb);
      out_lines.push({a, cb, 0, 2});
      if (stats_per_symbol) {
        append_name(err_buf, a); err_buf += ','; append_num(err_buf, sym_contractions[b]); err_buf += '\n';
        sym_contractions[a] = 0;
      }
      continue;
    }
    if (l.n > 3) continue;                                            // 4+ words: drop
    int32_t c = l.w[2];
    if (stats_per_symbol) {
      int64_t before = stat_contractions;
      uint64_t key = pair_key(canonical(b), canonical(c));
      process_apply(a, b, c);                                         // application
      FlatMap<int64_t>::Slot* s = delta_cache.probe(key);
      if (!s->key) { delta_cache.insert_at(s, key, stat_contractions - before); s = delta_cache.probe(key); }
      sym_contractions[a] = sym_contractions[b] + sym_contractions[c] + s->val;
    } else {
      process_apply(a, b, c);
    }
  }

  // Reachability over constructed nodes, from every root.
  std::vector<uint8_t> reachable(ncanon, 0);
  {
    std::vector<int32_t> worklist;
    for (int32_t r : roots) if (r >= N) worklist.push_back(r - N);
    while (!worklist.empty()) {
      int32_t k = worklist.back();
      worklist.pop_back();
      if (reachable[k]) continue;
      reachable[k] = 1;
      int32_t l = cdef.p[k].l, r = cdef.p[k].r;
      if (l >= N && !reachable[l - N]) worklist.push_back(l - N);
      if (r >= N && !reachable[r - N]) worklist.push_back(r - N);
    }
  }

  std::string out;
  out.reserve(out_lines.n * 24 + 1024);
  for (size_t i = 0; i < out_lines.n; ++i) {
    const OutLine& e = out_lines.p[i];
    if (e.n == 3) {
      if (!reachable[e.w1 - N]) continue;
      append_name(out, e.w1); out += ' ';
      append_name(out, e.w2); out += ' ';
      append_name(out, e.w3);
    } else if (e.n == 2) {
      append_name(out, e.w1); out += ' ';
      append_name(out, e.w2);
    } else {
      append_name(out, e.w1);
    }
    out += '\n';
  }
  fwrite(out.data(), 1, out.size(), stdout);

  if (stats_per_symbol) { err_buf += "TOTAL,"; append_num(err_buf, stat_contractions); err_buf += '\n'; }
  fwrite(err_buf.data(), 1, err_buf.size(), stderr);
}
