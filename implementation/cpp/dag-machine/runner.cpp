// runner.cpp — minimal fast tree-calculus program runner
//
// The other DAG machines in this directory are pure DAG→DAG transforms:
// reduce.cpp reduces a tree, canonicalize.cpp hash-conses it, a tree in
// on stdin and a tree out on stdout. This one instead *runs a program
// against data*: it marshals host strings/bytes into tree-calculus
// values, applies the program to them, and decodes the result back to a
// string — and it can hold one loaded program and answer many such
// queries. It's a fast, purpose-built subset of bin/main.js (which
// "applies tree calculus programs to arguments"), covering just the
// invocation patterns a LambAda build needs.
//
// One-shot mode:
//
//   runner <dag-file> <string>
//
// reads the DAG from <dag-file>, applies it to <string> (marshalled
// as a TC list-of-bytes), reduces, and prints the result as a string
// (with a trailing newline, matching `console.log`).
//
// Server mode — a single process loads the bundle once and answers many
// requests, so reductions of shared sub-terms are amortised across them:
//
//   runner -s
//
// reads commands from stdin, writes responses to stdout. Commands are
// newline-terminated. Some commands carry a length-prefixed payload.
//
//   load <path>\n
//     -> ok\n                                (replaces env)
//   eval <symbol>\n
//     -> data <len>\n<bytes>                 (to_string of env[symbol])
//   eval-dag <symbol>\n
//     -> data <len>\n<bytes>                 (reduced env[symbol] as hash-consed DAG text)
//   apply <symbol> <byte-len>\n<bytes>
//     -> data <len>\n<bytes>                 (to_string of apply(env[sym], of_string(bytes)))
//   reduce <byte-len>\n<bytes>
//     -> data <len>\n<bytes>                 (value of <bytes>, a DAG read against
//                                             the loaded module, as hash-consed DAG
//                                             text; defines nothing that outlives it)
//   reset\n
//     -> ok\n                                (drop arena, re-parse the loaded bundle)
//   quit\n
//     -> ok\n                                (and exits)
//
// On any failure: err <message>\n. <bytes> in responses is exactly
// <len> raw bytes (no trailing newline, since the length is exact).
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
// module, and the argument and application a one-off `apply` builds. Both
// collectors are non-moving, so a root registered once stays valid, and a
// Tree held anywhere the collector cannot see — a local here, a frame of the
// walk in to_dag — survives a collection unchanged.
//
// Build:
//   c++ -O3 -std=c++17 -pthread [-DRUNNER_EAGER] -o runner runner.cpp

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
static inline void hold(Tree t) { g_e.roots().push_back(t); }

static inline void drop_held() { g_e.roots().pop_back(); }

// Everything held while this is alive is released when it goes out of scope,
// however the scope is left. For a command that roots an unknown number of
// trees — reading a DAG roots one per line — and must leave none behind.
struct HeldScope {
  const size_t mark = g_e.roots().size();
  ~HeldScope() { g_e.roots().resize(mark); }
};

static inline void set_collection_budget(size_t nodes) { g_e.set_budget(nodes); }

// die() throws so server-mode commands can recover; main() catches and reports.
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
// nats this should be widened, but _to_string outputs char codes).
static uint64_t to_nat_u64(Tree t) {
  auto bits = to_list(t);
  uint64_t n = 0;
  for (size_t i = bits.size(); i > 0; --i) {
    n = (n << 1) | (to_bool(bits[i - 1]) ? 1u : 0u);
  }
  return n;
}

// Render a tree as hash-consed DAG text, mirroring formatter_dag.to in
// ../../../bin/main.js. Forces full reduction along the way (each node is
// reduced before its children are walked), so the output is the *reduced* DAG —
// the same byte-for-byte representation the JS path produces, suitable for
// parsing back via Dag.parse.
static std::string to_dag(Tree root) {
  std::vector<std::pair<Tree, bool>> stack; // (node, exit_phase)
  std::unordered_map<Tree, std::string> keys;
  std::unordered_map<std::string, std::string> app_keys;
  std::vector<std::string> lines;
  size_t counter = 0;

  auto getOrAlloc = [&](const std::string& app_key) -> std::string {
    auto it = app_keys.find(app_key);
    if (it != app_keys.end()) return it->second;
    std::string id = std::to_string(counter++);
    app_keys.emplace(app_key, id);
    lines.push_back(id + " " + app_key);
    return id;
  };

  stack.push_back({root, false});
  while (!stack.empty()) {
    auto frame = stack.back(); stack.pop_back();
    Tree node = frame.first;
    if (keys.count(node)) continue;

    if (!frame.second) {
      Shape s = shape(node); // forces
      stack.push_back({node, true});
      // Right child on top so it is popped (and processed) first — mirrors
      // `for (const c of children) todo.push(c)` in the JS implementation.
      if (s.arity >= 1) stack.push_back({s.u, false});
      if (s.arity == 2) stack.push_back({s.v, false});
    } else {
      Shape s = shape(node); // already reduced: a lookup
      std::string current = "\xe2\x96\xb3"; // △
      if (s.arity >= 1) current = getOrAlloc(current + " " + keys[s.u]);
      if (s.arity == 2) current = getOrAlloc(current + " " + keys[s.v]);
      keys[node] = current;
    }
  }

  std::string result;
  for (const auto& line : lines) { result += line; result += '\n'; }
  result += keys[root];
  return result;
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

// Decode UTF-8 input bytes into Unicode code points, mirroring how the JS CLI
// effectively maps a JS string into a list of code-unit-valued nats. Inputs stay
// within the BMP, so a code point per nat round-trips byte-for-byte with the JS
// implementation.
static std::vector<uint32_t> utf8_decode(std::string_view s) {
  std::vector<uint32_t> out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
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
    out.push_back(cp);
    i += len;
  }
  return out;
}

static Tree of_string(std::string_view s) {
  auto cps = utf8_decode(s);
  Tree f = g_e.leaf();
  for (size_t i = cps.size(); i > 0; --i) f = g_e.fork(of_nat(cps[i - 1]), f);
  return f;
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

// Returns the value of any 1-word (terminator) line if present, else 0.
// Bundles used in server mode typically have no terminator; one-shot mode
// expects one.
//
// `outer` is an enclosing scope to resolve names `env` does not define. That is
// what makes it possible to read an expression *against* a loaded module rather
// than into it: the expression's own bindings go in a scope of their own, where
// they shadow nothing that outlives them and can be dropped whole afterwards.
static Tree parse_dag_into(std::string_view text, TreeEnv& env,
                           const TreeEnv* outer = nullptr) {
  if (env.empty()) env.emplace("\xe2\x96\xb3", g_e.leaf()); // △

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
    } else if (nt == 2) {
      env[std::string(tok[0])] = get(tok[1]);
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

// ─── server mode ──────────────────────────────────────────────────────────

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
static void load_bundle(TreeEnv& env, const std::string& bundle_path) {
  env.clear();
  g_e.clear();
  parse_dag_into(read_file(bundle_path.c_str()), env);
}

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
  std::string bundle_path; // remembered for `reset`
  bool loaded = false;

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

    if (verb == "quit") {
      std::fputs("ok\n", stdout);
      std::fflush(stdout);
      return 0;
    }

    try {
      if (verb == "load" || verb == "reset") {
        if (verb == "load") bundle_path = std::string(rest);
        if (bundle_path.empty()) { write_err("no bundle loaded"); continue; }
        load_bundle(env, bundle_path);
        loaded = true;
        std::fputs("ok\n", stdout);
        std::fflush(stdout);
        continue;
      }

      if (!loaded) { write_err("no bundle loaded"); continue; }

      if (verb == "eval" || verb == "eval-dag") {
        std::string sym(rest);
        auto it = env.find(sym);
        if (it == env.end()) { write_err("unbound: " + sym); continue; }
        write_data(verb == "eval" ? to_string_marshal(it->second) : to_dag(it->second));
        continue;
      }

      if (verb == "apply") {
        size_t sep = rest.rfind(' ');
        if (sep == std::string_view::npos) { write_err("apply: expected <symbol> <len>"); continue; }
        std::string sym(rest.substr(0, sep));
        const long long len = payload_length(rest.substr(sep + 1));
        if (len < 0) { write_err("apply: bad length"); continue; }

        std::string payload;
        if (!read_exact(payload, len)) { write_err("apply: short read"); return 1; }

        auto it = env.find(sym);
        if (it == env.end()) { write_err("unbound: " + sym); continue; }
        // The argument and the application are the caller's, not the bundle's, so
        // they need rooting for as long as the answer is being computed.
        const Tree result = g_e.apply(it->second, of_string(payload));
        hold(result);
        std::string out = to_string_marshal(result);
        drop_held();
        write_data(out);
        continue;
      }

      if (verb == "reduce") {
        const long long len = payload_length(rest);
        if (len < 0) { write_err("reduce: bad length"); continue; }

        std::string payload;
        if (!read_exact(payload, len)) { write_err("reduce: short read"); return 1; }

        // Read against the module rather than into it: `local` shadows nothing
        // and is gone by the next command, and so are the roots the reading
        // needed. A caller can ask a thousand questions in a row without the
        // module growing by one binding or the collector losing a thing to
        // reclaim.
        TreeEnv local;
        HeldScope held;
        const Tree value = parse_dag_into(payload, local, &env);
        if (!value) { write_err("reduce: not terminated by a value"); continue; }
        write_data(to_dag(value));
        continue;
      }

      write_err("unknown command: " + std::string(verb));
    } catch (const std::exception& e) {
      write_err(e.what());
    }
  }
}

// ─── main ─────────────────────────────────────────────────────────────────

// Real entry point — runs on a worker thread that has a large stack.
// Forcing a term is recursive and can chain tens of thousands of frames deep on
// number-crunching benchmark suites; the main thread's 8 MiB stack isn't enough.
struct WorkerArgs { int argc; char** argv; int result; };

static void* worker_main(void* p) {
  auto* w = static_cast<WorkerArgs*>(p);
  int argc = w->argc;
  char** argv = w->argv;

  set_collection_budget(collection_budget_nodes());

  if (argc == 2 && (std::strcmp(argv[1], "-s") == 0 ||
                    std::strcmp(argv[1], "--server") == 0)) {
    w->result = run_server();
    return nullptr;
  }

  if (argc != 3) {
    std::fprintf(stderr,
                 "Usage:\n"
                 "  %s <dag-file> <string>     one-shot apply\n"
                 "  %s -s | --server           stdin/stdout server mode\n",
                 argv[0], argv[0]);
    w->result = 1;
    return nullptr;
  }

  try {
    TreeEnv env;
    Tree dag = parse_dag_into(read_file(argv[1]), env);
    if (!dag) die("dag representation was not terminated by a value");
    const Tree result = g_e.apply(dag, of_string(argv[2]));
    hold(result);
    std::string out = to_string_marshal(result);
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fputc('\n', stdout);
    w->result = 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    w->result = 1;
  }
  return nullptr;
}

int main(int argc, char** argv) {
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

  WorkerArgs args{argc, argv, 1};
  pthread_t tid;
  if (int rc = pthread_create(&tid, &attr, worker_main, &args)) {
    std::fprintf(stderr, "runner: pthread_create(stack=%zu MiB): %s\n",
                 stack_mb, std::strerror(rc));
    return 1;
  }
  pthread_attr_destroy(&attr);
  pthread_join(tid, nullptr);
  return args.result;
}
