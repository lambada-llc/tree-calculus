// Standalone driver for (a copy of) eager-graph-nil-mmap-32.hpp.
// Build: c++ -O3 -std=c++17 [-DINSTR] drv.cpp -o drv[-instr]
// Script commands (one per line, '#' comments):
//   budget MB | jets on|off | memo on|off | reset_memo
//   load PATH                 parse a DAG file (3-word lines are applied)
//   def NAME F X...           NAME = F X1 X2 ... (left-assoc apply)
//   measure LABEL F X...      same, print counters for it
//   trace F X...              print every reduce step (INSTR)
//   profile on|off|dump N     histogram of reduce-step function shapes (INSTR)
//   ternary PATH              benchmark: fold-apply ternary lines from identity
//   show NAME
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <random>
#include <unordered_set>

#ifdef INSTR
#include "eager-instr.hpp"
#elif defined(FIXJET)
#include "eager-jet.hpp"
#else
#include "eager-pristine.hpp"
#endif

using Reducer = EagerGraphNilMmap32Jets;
static Reducer E;
using Tree = uint32_t;
static std::unordered_map<std::string, Tree> env;
static std::unordered_map<Tree, std::string> name_of;
static bool pprof_live = false; // register symbols as their alias lines are read
static void pprof_register(const std::string &nm, uint32_t t);

static uint32_t NU(uint32_t t) { return E.triage([] { return 0u; }, [](uint32_t u) { return u; }, [](uint32_t u, uint32_t) { return u; }, t); }
static uint32_t NV(uint32_t t) { return E.triage([] { return 0u; }, [](uint32_t) { return 0u; }, [](uint32_t, uint32_t v) { return v; }, t); }
static std::string read_file(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { std::fprintf(stderr, "cannot open %s\n", p.c_str()); std::exit(1); }
  std::ostringstream b; b << f.rdbuf(); return b.str();
}
static Tree get(const std::string &n) {
  if (n == "\xe2\x96\xb3") return E.leaf();
  auto it = env.find(n);
  if (it == env.end()) { std::fprintf(stderr, "unbound %s\n", n.c_str()); std::exit(1); }
  return it->second;
}
static bool good_name(const std::string &n) {
  if (n.empty() || std::isdigit((unsigned char)n[0])) return false;
  if (n.rfind(":test", 0) == 0 || n.rfind(":source", 0) == 0 || n[0] == '~') return false;
  return true;
}
static std::unordered_map<std::string, std::pair<uint64_t, double>> line_cost; // id -> (steps, ms)
static std::vector<std::pair<std::string, std::string>> aliases;
static bool record_lines = false;
static uint64_t line_start_steps = 0;
static void parse_dag(const std::string &text) {
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    std::istringstream ws(line);
    std::string w[3]; int n = 0;
    while (n < 3 && (ws >> w[n])) ++n;
    if (n == 3) {
      uint64_t s0 = E.stats_counters.steps;
      auto t0 = std::chrono::steady_clock::now();
      Tree v = E.apply(get(w[1]), get(w[2]));
      if (record_lines) line_cost[w[0]] = {E.stats_counters.steps - s0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()};
      env[w[0]] = v; E.roots().push_back(v);
    } else if (n == 2) {
      if (record_lines) {
        static uint64_t last_steps = 0; static auto last_t = std::chrono::steady_clock::now();
        if (aliases.empty()) { last_steps = 0; }
        uint64_t now = E.stats_counters.steps; auto tn = std::chrono::steady_clock::now();
        line_cost["@" + w[0]] = {now - (aliases.empty() ? line_start_steps : last_steps), std::chrono::duration<double, std::milli>(tn - last_t).count()};
        last_steps = now; last_t = tn;
        aliases.emplace_back(w[0], "@" + w[0]);
      }
      Tree v = get(w[1]); env[w[0]] = v;
#ifdef INSTR
      if (pprof_live && good_name(w[0]) && v > 1) pprof_register(w[0], v);
#endif
      if (good_name(w[0])) {
        auto it = name_of.find(v);
        if (it == name_of.end() || w[0].size() < it->second.size()) name_of[v] = w[0];
      }
    }
  }
}

#ifdef INSTR
static std::string desc(Tree t, int depth) {
  if (t == 1) return "\xe2\x96\xb3";
  auto it = name_of.find(t);
  if (it != name_of.end()) return it->second;
  if (depth == 0) return "*";
  uint32_t u = E.node_u(t), v = E.node_v(t);
  auto p = [&](Tree x) { std::string s = desc(x, depth - 1); return s.find(' ') != std::string::npos ? "(" + s + ")" : s; };
  if (!v) return "\xe2\x96\xb3 " + p(u);
  return "\xe2\x96\xb3 " + p(u) + " " + p(v);
}
static bool tracing = false;
static uint64_t trace_n = 0;
// profile: signature of `a` (depth-limited, named) -> count
static std::unordered_map<Tree, uint32_t> sig_cache; // cleared on gc
static uint64_t sig_gcs = 0;
static std::unordered_map<std::string, uint32_t> sig_id;
static std::vector<std::string> sig_str;
static std::vector<uint64_t> sig_count;
static int prof_depth = 3;
struct UProf { uint64_t steps = 0, hits = 0; uint32_t last_a = 0, a_changes = 0; uint32_t last_b = 0, b_changes = 0; };
static std::unordered_map<uint32_t, UProf> uprof; // key: an.u for fork a; sentinels for leaf/stem a
static uint32_t ukey(uint32_t a) { uint32_t u = E.node_u(a); if (!u) return 0x7ffffffe; if (!E.node_v(a)) return 0x7fffffff; return u; }
static void on_hit_u(void *, uint32_t a, uint32_t) { ++uprof[ukey(a)].hits; }
static void on_reduce_u(void *, uint32_t a, uint32_t b) {
  UProf &p = uprof[ukey(a)];
  ++p.steps;
  if (a != p.last_a) { ++p.a_changes; p.last_a = a; }
  if (b != p.last_b) { ++p.b_changes; p.last_b = b; }
}
// ---- provenance profile: which (named symbol, k-th argument) application each step belongs to
static std::vector<uint32_t> prov;            // node -> (sym id << 8) | args applied so far (0 = the symbol itself)
static std::vector<std::pair<uint32_t, uint32_t>> prov_static; // named symbol values
static std::vector<std::string> sym_names;
struct Open { int64_t depth; uint64_t steps0; uint32_t key; };
static std::vector<Open> open_calls;
struct CallStat { uint64_t calls = 0, incl = 0, self = 0, hits = 0, one_step = 0; };
static std::unordered_map<uint32_t, CallStat> cstat;
static uint64_t prov_gcs = 0, prov_untracked = 0;
static void prov_reset() {
  std::fill(prov.begin(), prov.end(), 0);
  for (auto &[t, v] : prov_static) prov[t] = v;
}
static inline void prov_set(uint32_t t, uint32_t v) {
  if (t <= 1) return; // the leaf is nobody's partial application
  if (t >= prov.size()) prov.resize(std::max<size_t>(t + 1, prov.size() * 2), 0);
  prov[t] = v;
}
static void on_reduce_p(void *, uint32_t a, uint32_t) {
  if (E.stats_counters.gcs != prov_gcs) { prov_gcs = E.stats_counters.gcs; prov_reset(); }
  const uint32_t p = a < prov.size() ? prov[a] : 0;
  if (p && (p & 255) < 255) open_calls.push_back({(int64_t)E.stack_depth(), E.stats_counters.steps, p + 1});
  if (!open_calls.empty()) ++cstat[open_calls.back().key].self; else ++prov_untracked;
}
static void on_hit_p(void *, uint32_t a, uint32_t) {
  if (!open_calls.empty()) { const uint32_t p = a < prov.size() ? prov[a] : 0; if (p && open_calls.back().key == p + 1 && open_calls.back().steps0 == E.stats_counters.steps) ++cstat[p + 1].hits; }
}
static std::unordered_map<uint32_t, int> arity_of; // node -> parametric arity (args absorbed without inspecting them)
static std::vector<int> sym_arity;
static std::unordered_map<std::string, int> arity_by_name;

static void on_pop_p(void *, int64_t size, uint32_t result) {
  while (!open_calls.empty() && open_calls.back().depth > size) {
    const Open o = open_calls.back(); open_calls.pop_back();
    CallStat &c = cstat[o.key];
    const uint64_t n = E.stats_counters.steps - o.steps0 + 1;
    ++c.calls; c.incl += n; if (n == 1) ++c.one_step;
    if ((int)(o.key & 255) <= sym_arity[o.key >> 8]) prov_set(result, o.key); // still a partial application
  }
}
static void pprof_register(const std::string &nm, uint32_t t) {
  if (t < prov.size() && prov[t] && (prov[t] & 255) == 0) return; // already a symbol (an alias of the same value); a dynamic label is overridden
  auto it = arity_by_name.find(nm);
  uint32_t id = sym_names.size(); sym_names.push_back(nm); sym_arity.push_back(it == arity_by_name.end() ? 0 : it->second);
  prov_static.push_back({t, id << 8}); prov_set(t, id << 8);
}
// arity probe: apply to unique marker trees; abort as soon as a rule depends on a marker
static std::vector<uint32_t> markers;
static uint64_t probe_steps = 0;
struct Inspected {};
static void on_reduce_probe(void *, uint32_t a, uint32_t b) {
  if (++probe_steps > 200000) throw Inspected{};
  for (uint32_t m : markers) {
    if (a == m) throw Inspected{};
    const uint32_t u = E.node_u(a);
    if (u && E.node_v(a)) { // a fork
      if (u == m) throw Inspected{};
      if (E.node_u(u) && E.node_v(u) && b == m) throw Inspected{};
    }
  }
}
static void on_reduce(void *, uint32_t a, uint32_t b) {
  if (tracing) {
    std::printf("  #%llu  a=%s   b=%s   depth=%zu\n", (unsigned long long)++trace_n, desc(a, 4).c_str(), desc(b, 2).c_str(), E.stack_depth());
    return;
  }
  if (E.stats_counters.gcs != sig_gcs) { sig_cache.clear(); sig_gcs = E.stats_counters.gcs; }
  auto it = sig_cache.find(a);
  uint32_t id;
  if (it != sig_cache.end()) id = it->second;
  else {
    std::string s = desc(a, prof_depth);
    auto jt = sig_id.find(s);
    if (jt == sig_id.end()) { id = sig_str.size(); sig_id.emplace(s, id); sig_str.push_back(s); sig_count.push_back(0); }
    else id = jt->second;
    sig_cache.emplace(a, id);
  }
  ++sig_count[id];
}
#endif

static void print_stats(const char *label, const Reducer::Stats &b, double ms) {
  const auto &n = E.stats_counters;
  std::printf("%-28s steps=%llu memo_hits=%llu memo_puts=%llu jets=%llu gcs=%llu ms=%.3f",
              label, (unsigned long long)(n.steps - b.steps), (unsigned long long)(n.memo_hits - b.memo_hits),
              (unsigned long long)(n.memo_puts - b.memo_puts), (unsigned long long)(n.jets - b.jets),
              (unsigned long long)(n.gcs - b.gcs), ms);
#ifdef FIXJET
  std::printf(" fix_jets=%llu b_jets=%llu c_jets=%llu/%llu/%llu", (unsigned long long)(n.fix_jets - b.fix_jets), (unsigned long long)(n.b_jets - b.b_jets),
              (unsigned long long)(n.c1_jets - b.c1_jets), (unsigned long long)(n.c2_jets - b.c2_jets), (unsigned long long)(n.c3_jets - b.c3_jets));
#endif
#ifdef INSTR
  std::printf(" rules[L S K S FL FS FF hit]=");
  for (int i = 0; i < 8; ++i) std::printf("%s%llu", i ? "/" : "", (unsigned long long)(n.rule[i] - b.rule[i]));
  std::printf(" gc_ms=%.1f", (n.gc_ns - b.gc_ns) / 1e6);
  std::printf(" lookups=%llu cold_skips=%llu sshape=", (unsigned long long)(n.lookups - b.lookups), (unsigned long long)(n.cold_skips - b.cold_skips));
  for (int i = 0; i < 8; ++i) std::printf("%s%llu", i ? "/" : "", (unsigned long long)(n.sshape[i] - b.sshape[i]));
#endif
  std::printf("\n");
}

// structural hash (merkle over the DAG), memoized per index; valid within one GC epoch
static std::unordered_map<uint32_t, uint64_t> shash_memo;
static uint64_t shash(uint32_t t) {
  if (t == 1) return 0x1234567ULL;
  auto it = shash_memo.find(t);
  if (it != shash_memo.end()) return it->second;
  std::vector<std::pair<uint32_t, bool>> st{{t, false}};
  while (!st.empty()) {
    auto [x, ex] = st.back(); st.pop_back();
    if (x == 1 || shash_memo.count(x)) continue;
    uint32_t u = NU(x), v = NV(x);
    if (!ex) { st.push_back({x, true}); st.push_back({u, false}); if (v) st.push_back({v, false}); continue; }
    uint64_t hu = u == 1 ? 0x1234567ULL : shash_memo[u];
    uint64_t hv = v ? (v == 1 ? 0x1234567ULL : shash_memo[v]) : 0x9999ULL;
    uint64_t h = (hu * 0x9e3779b97f4a7c15ULL) ^ (hv + 0x7f4a7c159e3779b9ULL + (hu << 6) + (hu >> 2));
    h ^= h >> 29; h *= 0xbf58476d1ce4e5b9ULL; h ^= h >> 32;
    shash_memo[x] = h;
  }
  return shash_memo[t];
}
static Tree apply_chain(const std::vector<std::string> &w, size_t from) {
  Tree r = get(w[from]);
  for (size_t i = from + 1; i < w.size(); ++i) r = E.apply(r, get(w[i]));
  E.roots().push_back(r);
  return r;
}

// ternary: 0 leaf, 1 stem, 2 fork (prefix)
static Tree of_ternary(const std::string &s, size_t &i) {
  char c = s[i++];
  if (c == '0') return E.leaf();
  if (c == '1') { Tree u = of_ternary(s, i); return E.stem(u); }
  Tree u = of_ternary(s, i); Tree v = of_ternary(s, i); return E.fork(u, v);
}

int main(int argc, char **argv) {
  std::string script = argc > 1 ? read_file(argv[1]) : std::string(std::istreambuf_iterator<char>(std::cin), {});
  E.set_budget(size_t(512) * 1024 * 1024 / 8);
  std::istringstream in(script);
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ws(line);
    std::vector<std::string> w; for (std::string x; ws >> x;) w.push_back(x);
    if (w.empty()) continue;
    const std::string &cmd = w[0];
    auto t0 = std::chrono::steady_clock::now();
    auto before = E.stats_counters;
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    if (cmd == "budget") E.set_budget(std::stoull(w[1]) * 1024 * 1024 / 8);
    else if (cmd == "jets") E.set_jets(w[1] == "on");
#ifdef INSTR
    else if (cmd == "memo") E.memo_enabled = (w[1] == "on");
    else if (cmd == "reset_memo") E.reset_memo();
    else if (cmd == "trace") {
      E.on_reduce = on_reduce; tracing = true; trace_n = 0;
      std::printf("trace %s\n", line.c_str() + 6);
      apply_chain(w, 1);
      tracing = false; E.on_reduce = nullptr;
      print_stats("  (trace)", before, ms());
    } else if (cmd == "arity") {
      std::mt19937_64 rng(42);
      markers.clear();
      for (int k = 0; k < 6; ++k) { Tree l = E.leaf(); for (int i = 0; i < 48; ++i) l = E.fork(rng() & 1 ? E.stem(E.leaf()) : E.leaf(), l); markers.push_back(l); E.roots().push_back(l); }
      std::map<int, int> hist;
      for (auto &[t, nm] : name_of) {
        if (t <= 1) continue;
        Tree x = t; int ar = 0;
        size_t r0 = E.roots().size();
        E.on_reduce = on_reduce_probe; probe_steps = 0;
        for (int k = 0; k < 6; ++k) {
          try { x = E.apply(x, markers[k]); E.roots().push_back(x); }
          catch (Inspected &) { break; }
          if (std::find(markers.begin(), markers.end(), x) != markers.end()) break; // returned an argument: saturated
          { // a partial application captures an argument; a result with no marker in it is a constant: saturated
            bool has = false; std::vector<uint32_t> st{x}; std::unordered_set<uint32_t> seen;
            while (!st.empty() && !has && seen.size() < 200000) {
              uint32_t y = st.back(); st.pop_back();
              if (y <= 1 || !seen.insert(y).second) continue;
              if (std::find(markers.begin(), markers.end(), y) != markers.end()) { has = true; break; }
              st.push_back(E.node_u(y)); if (E.node_v(y)) st.push_back(E.node_v(y));
            }
            if (!has) break;
          }
          ar = k + 1;
        }
        E.on_reduce = nullptr;
        E.roots().resize(r0);
        arity_of[t] = ar; ++hist[ar];
      }
      if (w.size() > 1) { std::ofstream o(w[1]); for (auto &[nm, t] : env) if (good_name(nm) && arity_of.count(t)) o << nm << " " << arity_of[t] << "\n"; }
      std::printf("arity: probed %zu symbols; histogram:", arity_of.size());
      for (auto &[a, n] : hist) std::printf(" %d:%d", a, n);
      std::printf("\n");
      for (const char *nm : {":k", ":i", ":s", ":b", ":c", ":ct", "fix", "triage", "wait", "compose", "const", "let", "id", "List.map", "List.length", "List.match", "Bool.match", "Pair.match"}) {
        auto it = env.find(nm); if (it != env.end()) std::printf("  arity(%s) = %d\n", nm, arity_of.count(it->second) ? arity_of[it->second] : -1);
      }
    } else if (cmd == "arityload") {
      std::ifstream f(w[1]); std::string nm; int ar; while (f >> nm >> ar) arity_by_name[nm] = ar;
      std::printf("arityload: %zu names\n", arity_by_name.size());
    } else if (cmd == "pprofile") {
      if (w[1] == "on") {
        prov.assign(size_t(1) << 26, 0); prov_static.clear(); sym_names.clear();
        std::vector<std::pair<std::string, uint32_t>> named;
        for (auto &[t, nm] : name_of) if (t > 1) named.push_back({nm, t});
        std::sort(named.begin(), named.end());
        sym_arity.clear();
        for (auto &[nm, t] : named) { uint32_t id = sym_names.size(); sym_names.push_back(nm); sym_arity.push_back(arity_of.count(t) ? arity_of[t] : 0); prov_static.push_back({t, id << 8}); }
        prov_reset(); prov_gcs = E.stats_counters.gcs;
        pprof_live = w.size() > 2 && w[2] == "live";
        E.on_reduce = on_reduce_p; E.on_hit = on_hit_p; E.on_pop = on_pop_p;
      } else if (w[1] == "off") { E.on_reduce = nullptr; E.on_hit = nullptr; E.on_pop = nullptr; }
      else if (w[1] == "dump") {
        size_t N = std::stoul(w[2]);
        const bool by_self = w.size() > 3 && w[3] == "self";
        std::vector<std::pair<uint64_t, uint32_t>> rows; uint64_t totself = 0;
        for (auto &[k, c] : cstat) { rows.push_back({by_self ? c.self : c.incl, k}); totself += c.self; }
        std::sort(rows.rbegin(), rows.rend());
        const uint64_t tot = totself + prov_untracked;
        std::printf("pprofile (sorted by %s): %llu steps, %llu untracked (%.2f%%), %zu (symbol,arg) keys\n", by_self ? "self" : "inclusive",
                    (unsigned long long)tot, (unsigned long long)prov_untracked, 100.0 * prov_untracked / tot, rows.size());
        std::printf("%-40s %4s %3s %12s %12s %8s %14s %8s %10s %9s %8s\n", "symbol", "arg", "ar", "calls", "memo-hit", "1-step", "self steps", "self%", "self/call", "incl/call", "incl%");
        for (size_t i = 0; i < std::min(N, rows.size()); ++i) {
          const uint32_t k = rows[i].second; const CallStat &c = cstat[k];
          std::printf("%-40s %4u %3d %12llu %12llu %8llu %14llu %7.2f%% %10.2f %9.1f %7.2f%%\n", sym_names[k >> 8].c_str(), k & 255, sym_arity[k >> 8], (unsigned long long)c.calls, (unsigned long long)c.hits,
                      (unsigned long long)c.one_step, (unsigned long long)c.self, 100.0 * c.self / tot, c.calls ? double(c.self) / c.calls : 0.0, c.calls ? double(c.incl) / c.calls : 0.0, 100.0 * c.incl / tot);
        }
      }
    } else if (cmd == "uprofile") {
      if (w[1] == "on") { E.on_reduce = on_reduce_u; E.on_hit = on_hit_u; }
      else if (w[1] == "off") { E.on_reduce = nullptr; E.on_hit = nullptr; }
      else if (w[1] == "reset") uprof.clear();
      else if (w[1] == "dump") {
        size_t N = std::stoul(w[2]);
        int dd = w.size() > 3 ? std::stoi(w[3]) : 4;
        std::vector<std::pair<uint64_t, uint32_t>> rows;
        uint64_t tot = 0, toth = 0;
        for (auto &[k, p] : uprof) { rows.push_back({p.steps, k}); tot += p.steps; toth += p.hits; }
        std::sort(rows.rbegin(), rows.rend());
        std::printf("uprofile: %llu steps (%llu memo hits), %zu distinct u\n", (unsigned long long)tot, (unsigned long long)toth, rows.size());
        uint64_t cum = 0;
        for (size_t i = 0; i < std::min(N, rows.size()); ++i) {
          auto &p = uprof[rows[i].second]; cum += p.steps;
          uint32_t k = rows[i].second;
          std::string kind, d;
          if (k == 0x7ffffffe) { kind = "L"; d = "a=\xe2\x96\xb3"; }
          else if (k == 0x7fffffff) { kind = "St"; d = "a=\xe2\x96\xb3u"; }
          else { uint32_t uu = E.node_u(k), uv = E.node_v(k); kind = !uu ? "K" : (!uv ? "S" : "F"); d = "u#" + std::to_string(k) + " = " + desc(k, dd); }
          std::printf("%12llu %6.2f%% %6.2f%% hits=%-10llu a_chg=%-10u b_chg=%-10u %-2s %s\n", (unsigned long long)p.steps, 100.0 * p.steps / tot, 100.0 * cum / tot,
                      (unsigned long long)p.hits, p.a_changes, p.b_changes, kind.c_str(), d.c_str());
        }
      }
    }
    else if (cmd == "pathnames") {
      int D = std::stoi(w[1]);
      std::vector<std::pair<uint32_t, std::string>> named(name_of.begin(), name_of.end());
      std::unordered_map<uint32_t, std::string> extra;
      for (auto &[t, nm] : named) {
        std::vector<std::pair<uint32_t, std::string>> st{{t, ""}};
        while (!st.empty()) {
          auto [x, path] = st.back(); st.pop_back();
          if (x < 2) continue;
          if (!path.empty() && !name_of.count(x)) {
            std::string cand = nm + "/" + path;
            auto it = extra.find(x);
            if (it == extra.end() || cand.size() < it->second.size()) extra[x] = cand;
          }
          if ((int)path.size() >= D) continue;
          uint32_t u = E.node_u(x), v = E.node_v(x);
          if (u) st.push_back({u, path + "u"});
          if (v) st.push_back({v, path + "v"});
        }
      }
      for (auto &[x, nm] : extra) name_of.emplace(x, nm);
      std::printf("pathnames: %zu extra names\n", extra.size());
    } else if (cmd == "profile") {
      if (w[1] == "on") { E.on_reduce = on_reduce; if (w.size() > 2) prof_depth = std::stoi(w[2]); }
      else if (w[1] == "off") E.on_reduce = nullptr;
      else if (w[1] == "reset") { sig_cache.clear(); sig_id.clear(); sig_str.clear(); sig_count.clear(); }
      else if (w[1] == "dump") {
        size_t N = std::stoul(w[2]);
        std::vector<uint32_t> ids(sig_str.size());
        for (uint32_t i = 0; i < ids.size(); ++i) ids[i] = i;
        std::sort(ids.begin(), ids.end(), [](uint32_t x, uint32_t y) { return sig_count[x] > sig_count[y]; });
        uint64_t tot = 0; for (auto c : sig_count) tot += c;
        std::printf("profile: %llu steps, %zu signatures\n", (unsigned long long)tot, sig_str.size());
        uint64_t cum = 0;
        for (size_t i = 0; i < std::min(N, ids.size()); ++i) {
          cum += sig_count[ids[i]];
          std::printf("%12llu %6.2f%% %6.2f%%  %s\n", (unsigned long long)sig_count[ids[i]], 100.0 * sig_count[ids[i]] / tot, 100.0 * cum / tot, sig_str[ids[i]].c_str());
        }
      }
    }
#endif
    else if (cmd == "linecost") {
      record_lines = true; aliases.clear(); line_cost.clear(); line_start_steps = E.stats_counters.steps;
      parse_dag(read_file(w[1])); print_stats(("load " + w[1]).c_str(), before, ms());
      record_lines = false;
      std::vector<std::pair<uint64_t, std::string>> rows;
      for (auto &[name, id] : aliases) { auto it = line_cost.find(id); if (it != line_cost.end()) rows.push_back({it->second.first, name + " (" + std::to_string(it->second.second) + " ms)"}); }
      std::sort(rows.rbegin(), rows.rend());
      size_t N = std::stoul(w[2]);
      for (size_t i = 0; i < std::min(N, rows.size()); ++i) std::printf("%14llu  %s\n", (unsigned long long)rows[i].first, rows[i].second.c_str());
    }
#ifdef FIXJET
    else if (cmd == "fixjet") {
      Tree fk = E.apply(get("fix"), get(":k")); E.roots().push_back(fk);
      Tree knot = E.node_u(fk), y = E.node_v(fk), c = E.node_u(y), k2 = E.node_v(c);
      E.set_fix(knot, k2);
      E.set_c(get(":c"), get(":k"));
      std::printf("fixjet: knot=%u k2=%u\n", knot, k2);
    }
#endif
    else if (cmd == "hashenv") {
      shash_memo.clear();
      std::vector<std::string> keys; for (auto &[k, v] : env) if (good_name(k) || k.rfind(":test", 0) == 0) keys.push_back(k);
      std::sort(keys.begin(), keys.end());
      uint64_t h = 0; for (auto &k : keys) h = h * 31 + shash(env[k]);
      std::printf("hashenv: %zu symbols, hash %016llx\n", keys.size(), (unsigned long long)h);
    }
    else if (cmd == "load") { parse_dag(read_file(w[1])); print_stats(("load " + w[1]).c_str(), before, ms()); }
    else if (cmd == "defstr") {
      std::string text = read_file(w[2]);
      std::vector<uint32_t> cps;
      for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i]; uint32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; } else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; } else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; } else { cp = c & 0x07; len = 4; }
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (text[i + k] & 0x3F);
        i += len; cps.push_back(cp);
      }
      std::unordered_map<uint32_t, Tree> nat;
      auto of_nat = [&](uint32_t n) { std::vector<bool> bits; while (n) { bits.push_back(n & 1); n >>= 1; } Tree f = E.leaf(); for (size_t i = bits.size(); i > 0; --i) f = E.fork(bits[i-1] ? E.stem(E.leaf()) : E.leaf(), f); return f; };
      for (uint32_t cp : cps) if (!nat.count(cp)) { nat[cp] = of_nat(cp); E.roots().push_back(nat[cp]); }
      Tree l = E.leaf();
      for (size_t i = cps.size(); i > 0; --i) l = E.fork(nat[cps[i-1]], l);
      E.roots().push_back(l); env[w[1]] = l;
    }
    else if (cmd == "def") { env[w[1]] = apply_chain(w, 2); }
    else if (cmd == "measure") { Tree r = apply_chain(w, 2); double t = ms(); env["_"] = r; print_stats(w[1].c_str(), before, t); shash_memo.clear(); std::printf("  result hash %016llx\n", (unsigned long long)shash(r)); }
    else if (cmd == "show") {
#ifdef INSTR
      std::printf("%s = %s\n", w[1].c_str(), desc(get(w[1]), std::stoi(w.size() > 2 ? w[2] : "6")).c_str());
#endif
    } else if (cmd == "ternary") {
      std::string text = read_file(w[1]);
      std::istringstream ts(text);
      size_t i = 0; std::string id = "21100";
      Tree r = of_ternary(id, i);
      E.roots().push_back(r);
      std::vector<Tree> args;
      for (std::string l; std::getline(ts, l);) if (!l.empty()) { size_t j = 0; args.push_back(of_ternary(l, j)); E.roots().push_back(args.back()); }
      t0 = std::chrono::steady_clock::now(); before = E.stats_counters;
      for (Tree a : args) r = E.apply(r, a);
      print_stats(("ternary " + w[1]).c_str(), before, ms());
    } else { std::fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 1; }
    std::fflush(stdout);
  }
}
