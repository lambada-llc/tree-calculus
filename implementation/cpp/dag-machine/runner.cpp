// runner.cpp — minimal fast tree-calculus program runner
//
// reduce_canonicalize.cpp in this directory is a pure DAG→DAG transform: a
// module in on stdin, the same module reduced and hash-consed out on stdout.
// This one instead *runs a program against data*: it marshals host strings
// into tree-calculus values, applies the program to them, and decodes the
// result back — holding one loaded module and answering many such queries,
// so reductions of shared sub-terms are amortised across them.
//
//   runner
//
// reads commands from stdin, writes responses to stdout, and exits on EOF.
// Commands are newline-terminated. Some commands carry a length-prefixed
// payload.
//
//   load <path>\n
//     -> ok\n                                (replaces env)
//   bind <name> <byte-len>\n<bytes>
//     -> ok\n                                (of_string(bytes) as <name>, for the
//                                             next reduce and no longer)
//   reduce <dag|string> <byte-len>\n<bytes>
//     -> data <len>\n<bytes>                 (value of <bytes>, a DAG read against
//                                             the loaded module and whatever was
//                                             bound, rendered as asked)
//   dump\n
//     -> data <len>\n<bytes>                 (the evaluated module; eager only)
//
// Two commands, because there are two questions: what to reduce, and how to
// render it. A name in the module is a payload of one word, so looking a symbol
// up needs no command of its own; applying the module's compiler to a source
// file is `bind` followed by a payload that mentions both. Earlier versions
// spelled those `eval`, `eval-dag` and `apply` — three verbs covering four of
// the six cells that <what> x <how> actually has, which is why asking for a
// string that was not already a symbol had no spelling at all.
//
// On any failure: err <message>\n. <bytes> in responses is exactly
// <len> raw bytes (no trailing newline, since the length is exact).
//
// An err is recoverable only where the framing was understood. A command
// carrying a payload whose length could not be read leaves that payload in the
// stream, where it would be read as commands; the server reports and exits
// rather than answer nonsense to everything after it.
//
// Reduction
//
// Either of two evaluators, both over the same 8-byte nil-packed nodes in
// an mmap'd arena, chosen when this file is compiled:
//
//   default        ../lazy-graph-nil-mmap-32.hpp — head normal form on
//                  demand, so a binding whose normal form does not exist
//                  costs nothing until something asks for it.
//   -DRUNNER_EAGER ../eager-graph-nil-mmap-32.hpp — every binding is
//                  normalized as the module is read, which is faster and
//                  leaner *if* every binding in the module has a normal form.
//
// That proviso is the whole story. Eager is the better evaluator and the
// worse default: one definition that only converges lazily hangs the build,
// and nothing in the module system checks for it. A repository that holds
// itself to eager termination should build with -DRUNNER_EAGER and say so;
// everyone else gets an evaluator that cannot be broken this way.
//
// The eager evaluators in ../ that are not *-graph-* are benchmark
// implementations — recursive apply(), an arena that never frees — and are
// not usable here: a module is thousands of bindings reduced back to back in
// one process, which is a stack overflow and an unbounded heap respectively.
// See eager-graph-nil-mmap-32.hpp.
//
// Memory management
//
// Neither evaluator frees as it reduces; what bounds them is a mark-and-sweep
// from inside the reduction loop, once the arena passes
// RUNNER_RSS_THRESHOLD_MB. A single request can allocate a thousand times
// what it keeps, so waiting until it has answered is not enough. Everything
// that has to survive is registered as a root: every binding of the loaded
// module, and whatever the current request has bound or read. Both
// collectors are non-moving, so a root registered once stays valid, and a
// Tree held anywhere the collector cannot see — a local here, a frame of the
// walk in Renderer — survives a collection unchanged.
//
// Build:
//   c++ -O3 -std=c++17 -pthread [-DRUNNER_EAGER] -o runner runner.cpp

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef RUNNER_EAGER
#include "../eager-graph-nil-mmap-32.hpp"
using Reducer = EagerGraphNilMmap32;
#else
#include "../lazy-graph-nil-mmap-32.hpp"
using Reducer = LazyGraphNilMmap32;
#endif

// The one evaluator this process reduces in. Trees are indices into its arena,
// so they are only meaningful until the next clear().
static Reducer g_e;

using Tree = Reducer::Tree;

// Evaluation order is the only thing the two reducers disagree about. Both
// reclaim the same way — a non-moving mark-and-sweep from a root set the caller
// keeps filled — so everything below this point is written once, against
// whichever is in play.
// Rooting is push-only: a request roots an unknown number of trees — reading a
// DAG roots one per line — and releases them all at once by truncating back to
// where the module's own roots end. See `end_request` in run_server.
static inline void hold(Tree t) { g_e.roots().push_back(t); }

static inline void set_collection_budget(size_t nodes) { g_e.set_budget(nodes); }

// die() throws so a failing command can be answered with `err` and the server
// carry on; run_server catches.
[[noreturn]] static void die(const char* msg) {
  throw std::runtime_error(msg);
}

// ─── marshalling ──────────────────────────────────────────────────────────
//
// Everything below inspects trees through one helper: forcing a node to head
// normal form and reading off its arity and children is the only thing the
// evaluator exposes, and the only thing any of this needs.

struct Shape {
  uint32_t arity;
  Tree u, v;
};

static Shape shape(Tree t) {
  return g_e.triage(
      [] { return Shape{0, 0, 0}; },
      [](Tree u) { return Shape{1, u, 0}; },
      [](Tree u, Tree v) { return Shape{2, u, v}; },
      t);
}

static bool to_bool(Tree t) {
  switch (shape(t).arity) {
    case 0: return false;
    case 1: return true;
    default: die("tree is not a bool");
  }
}

// Walk the cons-list spine, accumulating heads.
static std::vector<Tree> to_list(Tree t) {
  std::vector<Tree> out;
  for (;;) {
    Shape s = shape(t);
    if (s.arity == 0) return out;
    if (s.arity == 1) die("tree is not a list");
    out.push_back(s.u); // head
    t = s.v;            // tail
  }
}

// to_nat as 64-bit (chars only need 8 bits; if any test ever needs >64-bit
// nats this should be widened, but _to_string outputs char codes). Read off
// the list in place: this runs once per character of every string answer.
static uint64_t to_nat_u64(Tree t) {
  uint64_t n = 0;
  unsigned i = 0; // bits are LSB first; past 64 they are dropped
  for (Shape s = shape(t); s.arity; s = shape(s.v), ++i) {
    if (s.arity == 1) die("tree is not a list");
    if (to_bool(s.u) && i < 64) n |= uint64_t(1) << i;
  }
  return n;
}

// Renders trees as hash-consed DAG lines, appended to `out`: a stem as
// `id △ u`, a fork as `id (stem) v` — the line for its stem `△ u`, applied to
// `v`. Each node is forced as it is reached and its children walked after, so
// what is rendered is the *normal form*. Lines are shared by structure, not by
// arena node: under the lazy evaluator two equal values can be two nodes.
//
// One renderer for both things written as DAGs: `reduce dag`'s answer, which
// mirrors formatter_dag.to in ../../../bin/main.js byte for byte (ids from 0,
// right child first) so it parses back via Dag.parse; and `dump`, whose ids
// carry a `~` prefix so they can never collide with a module's own names.
struct Renderer {
  static constexpr uint32_t LEAF = UINT32_MAX;
  const char* prefix;
  std::string out;
  std::unordered_map<Tree, uint32_t> id;           // arena node -> line
  std::unordered_map<uint64_t, uint32_t> line_of;  // (left, right) -> line
  uint32_t next = 0;

  void ref(uint32_t at, std::string& to) const {
    if (at == LEAF) { to += "\xe2\x96\xb3"; return; } // △
    to += prefix;
    to += std::to_string(at);
  }

  uint32_t line(uint32_t left, uint32_t right) {
    const auto [it, fresh] = line_of.try_emplace(uint64_t(left) << 32 | right, next);
    if (!fresh) return it->second;
    ref(next, out);
    out += ' ';
    ref(left, out);
    out += ' ';
    ref(right, out);
    out += '\n';
    return next++;
  }

  // The reference to `root`, every line it needs already in `out`.
  std::string operator()(Tree root) {
    std::vector<std::pair<Tree, bool>> stack{{root, false}}; // (node, exit phase)
    while (!stack.empty()) {
      const auto [node, exit] = stack.back();
      stack.pop_back();
      if (id.count(node)) continue;
      const Shape s = shape(node); // forces on entry; a lookup on exit
      if (!exit) {
        stack.push_back({node, true});
        // Right child on top, so it is rendered first — mirrors
        // `for (const c of children) todo.push(c)` in the JS implementation.
        if (s.arity >= 1) stack.push_back({s.u, false});
        if (s.arity == 2) stack.push_back({s.v, false});
        continue;
      }
      uint32_t at = LEAF;
      if (s.arity >= 1) at = line(LEAF, id.at(s.u));
      if (s.arity == 2) at = line(at, id.at(s.v));
      id.emplace(node, at);
    }
    std::string to;
    ref(id.at(root), to);
    return to;
  }
};

static std::string to_dag(Tree root) {
  Renderer render{""};
  const std::string value = render(root);
  return render.out + value;
}

// Encode one Unicode code point as UTF-8.
static void utf8_encode(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// JS treats string nats as UTF-16 code units (`String.fromCharCode`). For the
// range these builds actually use (ASCII + a few BMP symbols like △) we get the
// same result by treating each nat as a Unicode code point.
static std::string to_string_marshal(Tree t) {
  auto chars = to_list(t);
  std::string s;
  s.reserve(chars.size());
  for (Tree c : chars) utf8_encode(static_cast<uint32_t>(to_nat_u64(c)), s);
  return s;
}

static Tree of_bool(bool b) {
  return b ? g_e.stem(g_e.leaf()) : g_e.leaf();
}

static Tree of_nat(uint64_t n) {
  // Bits are stored LSB-first as a list of bools.
  std::vector<Tree> bits;
  while (n) {
    bits.push_back(of_bool(n & 1u));
    n >>= 1;
  }
  Tree f = g_e.leaf();
  for (size_t i = bits.size(); i > 0; --i) f = g_e.fork(bits[i - 1], f);
  return f;
}

// The code point of the UTF-8 sequence at s[i], and i moved past it. Inlined:
// it runs twice per character of a string, and called, took 40% more instructions.
[[gnu::always_inline]] inline uint32_t utf8_decode(std::string_view s, size_t& i) {
  uint8_t b = static_cast<uint8_t>(s[i]);
  uint32_t cp;
  int len;
  if (b < 0x80)            { cp = b;          len = 1; }
  else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; len = 2; }
  else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; len = 3; }
  else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; len = 4; }
  else die("invalid utf-8 in input");
  if (i + len > s.size()) die("truncated utf-8 in input");
  for (int k = 1; k < len; ++k) {
    uint8_t cb = static_cast<uint8_t>(s[i + k]);
    if ((cb & 0xC0) != 0x80) die("invalid utf-8 continuation");
    cp = (cp << 6) | (cb & 0x3F);
  }
  i += len;
  return cp;
}

// Decode UTF-8 input bytes into a list of Unicode code points, mirroring how
// the JS CLI effectively maps a JS string into a list of code-unit-valued nats.
// Inputs stay within the BMP, so a code point per nat round-trips byte-for-byte
// with the JS implementation.
//
// A source file is millions of characters but only a few dozen distinct ones,
// so each code point's nat is built once and every occurrence shares it: what
// is left per character is the one list cell that holds it. Twice over the
// text, then: once forwards, to check it, count the characters and build their
// nats — what list() is handed must build no node — and once backwards, as
// list() takes them from the list's end, straight from the text rather than
// from a copy of it.
static Tree of_string(std::string_view s) {
  std::vector<Tree> nat; // by code point; 0, which no tree is, until built
  size_t n = 0;
  for (size_t i = 0; i < s.size(); ++n) {
    const uint32_t cp = utf8_decode(s, i);
    if (cp >= nat.size()) nat.resize(cp + 1);
    if (!nat[cp]) nat[cp] = of_nat(cp);
  }
  size_t end = s.size(); // where the character list() takes next ends
  return g_e.list(n, [&] {
    size_t i = end;
    while ((static_cast<uint8_t>(s[--i]) & 0xC0) == 0x80) {} // back to where it starts
    end = i;
    return nat[utf8_decode(s, i)];
  }, g_e.leaf());
}

// ─── DAG parser ───────────────────────────────────────────────────────────
//
// Format (3-word | 2-word | 1-word lines):
//   "id left right"   →  env[id] = apply(env[left], env[right])
//   "symbol id"       →  env[symbol] = env[id]   (alias)
//   "id"              →  return env[id]          (terminator)
//
// "△" (UTF-8 E2 96 B3) is bound to the leaf in the initial environment.
//
// Every binding is an unreduced application: what a symbol denotes is not
// computed until something asks for it. See lazy-graph-nil-mmap-32.hpp.

using TreeEnv = std::unordered_map<std::string, Tree>;

// The bindings of the loaded module in definition order — what `dump` walks.
// The map alone cannot say what order the module said things in, and a dump
// that scrambled definition order would not be the same module.
static std::vector<std::pair<std::string, Tree>> g_env_order;

// Returns the value of any 1-word (terminator) line if present, else 0: a
// module has none, a `reduce` payload must.
//
// `outer` is an enclosing scope to resolve names `env` does not define. That is
// what makes it possible to read an expression *against* a loaded module rather
// than into it: the expression's own bindings go in a scope of their own, where
// they shadow nothing that outlives them and can be dropped whole afterwards.
static Tree parse_dag_into(std::string_view text, TreeEnv& env,
                           const TreeEnv* outer = nullptr,
                           std::vector<std::pair<std::string, Tree>>* order = nullptr) {
  // The leaf is in every scope. Unconditionally, not just an empty one: a
  // scratch scope that `bind` has already put something in is still a scope a
  // DAG may spell △ in.
  env.try_emplace("\xe2\x96\xb3", g_e.leaf()); // △

  auto get = [&](std::string_view name) -> Tree {
    auto it = env.find(std::string(name));
    if (it != env.end()) return it->second;
    if (outer) {
      auto o = outer->find(std::string(name));
      if (o != outer->end()) return o->second;
    }
    std::string msg = "unbound variable: ";
    msg.append(name);
    die(msg.c_str());
  };

  size_t i = 0, n = text.size();
  while (i < n) {
    size_t lineEnd = i;
    while (lineEnd < n && text[lineEnd] != '\n') ++lineEnd;
    size_t end = lineEnd;
    if (end > i && text[end - 1] == '\r') --end;

    std::string_view tok[3];
    int nt = 0;
    size_t j = i;
    while (j < end && nt < 3) {
      while (j < end && text[j] == ' ') ++j;
      if (j >= end) break;
      size_t k = j;
      while (k < end && text[k] != ' ') ++k;
      tok[nt++] = std::string_view(text.data() + j, k - j);
      j = k;
    }

    if (nt == 3) {
      const Tree value = g_e.apply(get(tok[1]), get(tok[2]));
      env[std::string(tok[0])] = value;
      // A binding is a root for as long as the module is loaded. Collection does
      // not move anything, so registering it once is all it ever needs.
      hold(value);
      // `~` names are a dump's structural scaffolding (see dump_module): part
      // of the module's environment so aliases resolve, but not part of what
      // it says — a re-dump builds scaffolding of its own.
      if (order && tok[0][0] != '~') order->emplace_back(std::string(tok[0]), value);
    } else if (nt == 2) {
      const Tree value = get(tok[1]);
      env[std::string(tok[0])] = value;
      if (order && tok[0][0] != '~') order->emplace_back(std::string(tok[0]), value);
    } else if (nt == 1) {
      return get(tok[0]);
    }

    i = lineEnd + 1;
  }
  return 0;
}

static std::string read_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::string msg = "could not open ";
    msg += path;
    die(msg.c_str());
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  return buf.str();
}

// ─── server ─────────────────────────────────────────────────────────────────

static void write_data(const std::string& s) {
  std::fprintf(stdout, "data %zu\n", s.size());
  std::fwrite(s.data(), 1, s.size(), stdout);
  std::fflush(stdout);
}

static void write_err(const std::string& msg) {
  std::fprintf(stdout, "err %s\n", msg.c_str());
  std::fflush(stdout);
}

// The decimal byte count a payload-carrying command ends in, or -1 if that is
// not what `s` is.
static long long payload_length(std::string_view s) {
  if (s.empty()) return -1;
  long long n = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '9') return -1;
    n = n * 10 + (ch - '0');
  }
  return n;
}

// Read exactly n bytes from stdin into out (resized).
static bool read_exact(std::string& out, size_t n) {
  out.resize(n);
  size_t got = 0;
  while (got < n) {
    size_t r = std::fread(out.data() + got, 1, n - got, stdin);
    if (r == 0) return false;
    got += r;
  }
  return true;
}

// Drop the arena and rebuild env from the bundle on disk. Every Tree handed out
// so far becomes invalid, which is why this only ever runs between commands.
//
// One rule if it fails, whether the file could not be read or the DAG did not
// resolve: the server has no module. Half of one would answer `unbound` to
// every symbol it did not reach, which reads as a broken bundle rather than a
// failed load.
static void load_bundle(TreeEnv& env, const std::string& bundle_path) {
  env.clear();
  g_env_order.clear();
  g_e.clear();
  parse_dag_into(read_file(bundle_path.c_str()), env, nullptr, &g_env_order);
}

#ifdef RUNNER_EAGER
// The loaded module with every binding in the state eager loading left it:
// fully reduced. Rendered as one hash-consed DAG whose every application is a
// value being *built* (△ applied to a child is a stem, a stem applied to a
// child is a fork), re-loading it costs parsing and interning but no
// reduction — which is what makes the dump worth caching across processes.
//
// Structural lines use a `~` id space of their own so they can never collide
// with the module's names, and each binding is then an alias into it, in
// definition order, so the reload defines exactly what the original did.
//
// Only the eager runner offers this: under the lazy one a binding may have no
// normal form, and rendering it here would be the divergence that evaluator
// exists to avoid.
static std::string dump_module() {
  Renderer render{"~"};
  render.out.reserve(64 << 20);
  for (const auto& [name, value] : g_env_order) {
    const std::string value_ref = render(value);
    render.out += name;
    render.out += ' ';
    render.out += value_ref;
    render.out += '\n';
  }
  return std::move(render.out);
}
#endif

// Collection budget, in nodes. 0 lets the arena grow unchecked. The default keeps
// peak memory below the hard limits typical hosted CI builders impose (Cloudflare
// Pages, etc.); bump it on a roomy local machine (e.g.
// RUNNER_RSS_THRESHOLD_MB=4096) to collect less often.
static size_t collection_budget_nodes() {
  const char* env = std::getenv("RUNNER_RSS_THRESHOLD_MB");
  size_t mb = env ? std::strtoull(env, nullptr, 10) : 512;
  return mb * 1024 * 1024 / sizeof(uint64_t);
}

// Catch errors raised by die() during a command without tearing down the
// process.
static int run_server() {
  TreeEnv env;
  bool loaded = false;

  // The scope a request builds for itself. `bind` puts marshalled arguments in
  // it, `reduce` reads its payload into it, and the answer ends it — so what a
  // request defines shadows nothing that outlives it, and a caller can ask a
  // thousand questions without the module growing by one binding.
  TreeEnv scratch;
  // Everything the module rooted sits below this; everything the request rooted
  // sits above it, and is released with the scratch scope.
  size_t scratch_floor = 0;
  const auto end_request = [&] {
    scratch.clear();
    g_e.roots().resize(scratch_floor);
  };

  // The one thing this server does: reduce a DAG read against the module, and
  // render the value it ends on. Every command below is a way of spelling a
  // payload for it.
  const auto answer = [&](std::string_view format, std::string_view payload) {
    const Tree value = parse_dag_into(payload, scratch, &env);
    if (!value) die("not terminated by a value");
    if (format == "dag") return to_dag(value);
    if (format == "string") return to_string_marshal(value);
    die("unrecognized format (expected dag or string)");
  };

  // RUNNER_STATS=1: per-command wall time and evaluator counters on stderr.
  const bool stats = [] {
    const char* s = std::getenv("RUNNER_STATS");
    return s && *s && std::strcmp(s, "0") != 0;
  }();
  struct CommandStats {
#ifdef RUNNER_EAGER
    Reducer::Stats before;
#endif
    std::chrono::steady_clock::time_point start;
    std::string what;
    bool armed = false;
    void begin(const std::string& line) {
      armed = true;
      what = line.substr(0, 48);
#ifdef RUNNER_EAGER
      before = g_e.stats_counters;
#endif
      start = std::chrono::steady_clock::now();
    }
    ~CommandStats() { flush(); }
    void flush() {
      if (!armed) return;
      armed = false;
      const double ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start).count();
#ifdef RUNNER_EAGER
      const auto& now = g_e.stats_counters;
      std::fprintf(stderr,
          "runner-stats: %8.1fms steps=%llu hits=%llu puts=%llu gcs=%llu marked=%llu "
          "arena=%zu | %s\n",
          ms,
          (unsigned long long)(now.steps - before.steps),
          (unsigned long long)(now.memo_hits - before.memo_hits),
          (unsigned long long)(now.memo_puts - before.memo_puts),
          (unsigned long long)(now.gcs - before.gcs),
          (unsigned long long)(now.gc_marked - before.gc_marked),
          g_e.allocated(), what.c_str());
#else
      std::fprintf(stderr, "runner-stats: %8.1fms arena=%zu | %s\n",
                   ms, g_e.allocated(), what.c_str());
#endif
    }
  };

  std::string line;
  while (true) {
    // Read one command line.
    line.clear();
    int c;
    while ((c = std::fgetc(stdin)) != EOF && c != '\n') line.push_back((char)c);
    if (line.empty() && c == EOF) return 0;

    // Tokenize: first word = verb, remainder = args.
    size_t sp = line.find(' ');
    std::string_view verb(line.data(), sp == std::string::npos ? line.size() : sp);
    std::string_view rest = sp == std::string::npos
        ? std::string_view{}
        : std::string_view(line.data() + sp + 1, line.size() - sp - 1);

    try {
      CommandStats cs;
      if (stats) cs.begin(line);
      if (verb == "load") {
        loaded = false; // until it is; see load_bundle
        load_bundle(env, std::string(rest));
        loaded = true;
        // The module's own roots are what a request is now measured against.
        scratch.clear();
        scratch_floor = g_e.roots().size();
        std::fputs("ok\n", stdout);
        std::fflush(stdout);
        continue;
      }

      if (!loaded) { write_err("no bundle loaded"); continue; }

      if (verb == "dump") {
#ifdef RUNNER_EAGER
        write_data(dump_module());
#else
        write_err("dump: only the eager runner holds a module in dumpable form");
#endif
        continue;
      }

      if (verb == "bind") {
        // A length we cannot read is as unrecoverable as a short read: the
        // payload is already in the stream and would be parsed as commands.
        const size_t sep = rest.rfind(' ');
        if (sep == std::string_view::npos) { write_err("bind: expected <name> <len>"); return 1; }
        const long long len = payload_length(rest.substr(sep + 1));
        if (len < 0) { write_err("bind: bad length"); return 1; }
        std::string payload;
        if (!read_exact(payload, len)) { write_err("bind: short read"); return 1; }

        const Tree value = of_string(payload);
        hold(value); // the request's, until the request ends
        scratch[std::string(rest.substr(0, sep))] = value;
        std::fputs("ok\n", stdout);
        std::fflush(stdout);
        continue;
      }

      if (verb == "reduce") {
        const size_t sep = rest.find(' ');
        if (sep == std::string_view::npos) { write_err("reduce: expected <format> <len>"); return 1; }
        const long long len = payload_length(rest.substr(sep + 1));
        if (len < 0) { write_err("reduce: bad length"); return 1; }
        std::string payload;
        if (!read_exact(payload, len)) { write_err("reduce: short read"); return 1; }

        try {
          write_data(answer(rest.substr(0, sep), payload));
        } catch (...) {
          end_request(); // an answer that failed still ends its request
          throw;
        }
        end_request();
        continue;
      }

      write_err("unknown command: " + std::string(verb));
    } catch (const std::exception& e) {
      write_err(e.what());
    }
  }
}

// ─── main ─────────────────────────────────────────────────────────────────

// The server — run on a worker thread that has a large stack.
// Forcing a term is recursive and can chain tens of thousands of frames deep on
// number-crunching benchmark suites; the main thread's 8 MiB stack isn't enough.
static void* worker_main(void* p) {
  set_collection_budget(collection_budget_nodes());
  *static_cast<int*>(p) = run_server();
  return nullptr;
}

int main() {
  // Worker stack: 64 MiB by default — enough for the deepest reduction
  // chains we've observed in Forest (Poly.Bench, Nat.Bench) while staying
  // friendly to constrained hosted-CI builders. Override with
  // RUNNER_WORKER_STACK_MB if you hit a stack overflow.
  size_t stack_mb = 64;
  if (const char* s = std::getenv("RUNNER_WORKER_STACK_MB")) {
    size_t v = std::strtoull(s, nullptr, 10);
    if (v > 0) stack_mb = v;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (int rc = pthread_attr_setstacksize(&attr, stack_mb * 1024 * 1024)) {
    std::fprintf(stderr, "runner: pthread_attr_setstacksize(%zu MiB): %s\n",
                 stack_mb, std::strerror(rc));
    return 1;
  }

  int result = 1;
  pthread_t tid;
  if (int rc = pthread_create(&tid, &attr, worker_main, &result)) {
    std::fprintf(stderr, "runner: pthread_create(stack=%zu MiB): %s\n",
                 stack_mb, std::strerror(rc));
    return 1;
  }
  pthread_attr_destroy(&attr);
  pthread_join(tid, nullptr);
  return result;
}
