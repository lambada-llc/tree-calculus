#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

enum Type : uint8_t { LEAF, STEM, FORK };

struct Info {
  Type type;
  int32_t a, b; // STEM: a=child; FORK: a=left(u), b=right(v)
};

// ── ID allocation ───────────────────────────────────────────────────────────
// Named symbols (from input) get IDs 0..N-1 via string interning.
// Canon/temp IDs are allocated from a shared counter that starts above named IDs.
// We defer string generation until output time.

static std::vector<std::string> named_strs; // id -> string (only for named/canon IDs)
static std::unordered_map<std::string, int32_t> str_to_id;
static int32_t next_id = 0;

static int32_t intern(const std::string& s) {
  auto [it, inserted] = str_to_id.emplace(s, next_id);
  if (inserted) {
    int32_t id = next_id++;
    if ((size_t)id >= named_strs.size()) named_strs.resize(id + 1);
    named_strs[id] = s;
    return id;
  }
  return it->second;
}

// Allocate a fresh ID without string interning (hot path)
static int32_t alloc_id() { return next_id++; }

// ── Core state (all integer-keyed) ──────────────────────────────────────────

static std::vector<Info> env;
static std::vector<int32_t> alias_v;     // id -> alias target (or -1)
static std::vector<int32_t> canon_v;     // id -> canonical output id (or -1)

static void ensure_id(int32_t id) {
  if (id >= (int32_t)env.size()) {
    size_t n = (size_t)id + 1;
    env.resize(n, {LEAF, -1, -1});
    alias_v.resize(n, -1);
    canon_v.resize(n, -1);
  }
}

static void ensure_alloc() {
  // After alloc_id(), ensure the latest ID has room
  ensure_id(next_id - 1);
}

// Integer-pair hash for memo/hash-cons
struct PairHash {
  size_t operator()(std::pair<int32_t, int32_t> p) const {
    return std::hash<int64_t>()((int64_t)p.first << 32 | (uint32_t)p.second);
  }
};
static std::unordered_map<std::pair<int32_t, int32_t>, int32_t, PairHash> hash_cons;
static std::unordered_map<std::pair<int32_t, int32_t>, int32_t, PairHash> apply_memo;

static int32_t canon_counter = 0;
static bool stats_enabled = false;
static int64_t stat_contractions = 0;
static int64_t stat_reduction_steps = 0;
static int64_t stat_output_lines = 0;

// Buffered output
struct OutputEntry {
  int8_t type; // 0 = node def, 1 = two-word, 2 = one-word
  int32_t canon_id; // for type 0: canonical id (for reachability filtering)
  int32_t w1, w2, w3; // words (ids)
};
static std::vector<OutputEntry> output_buffer;

// For reachability walk
static std::vector<std::pair<int32_t, int32_t>> node_defs_vec; // canon_id-indexed sparse
static std::unordered_map<int32_t, int32_t> node_defs_idx;     // canon_id -> index in node_defs_vec
static std::vector<int32_t> reachability_roots;

static bool has_node_def(int32_t id) { return node_defs_idx.count(id); }
static std::pair<int32_t, int32_t>& get_node_def(int32_t id) { return node_defs_vec[node_defs_idx[id]]; }
static void add_node_def(int32_t id, int32_t left, int32_t right) {
  node_defs_idx[id] = (int32_t)node_defs_vec.size();
  node_defs_vec.push_back({left, right});
}

static int32_t fresh_canon() {
  int32_t id = alloc_id();
  ensure_alloc();
  // Generate the display name lazily: ":N"
  if ((size_t)id >= named_strs.size()) named_strs.resize(id + 1);
  named_strs[id] = ":" + std::to_string(canon_counter++);
  return id;
}

static int32_t fresh_temp() {
  int32_t id = alloc_id();
  ensure_alloc();
  return id;
}

// Follow alias chain with path compression
static int32_t resolve(int32_t id) {
  int32_t cur = id;
  while (alias_v[cur] >= 0) cur = alias_v[cur];
  // Path compression
  while (alias_v[id] >= 0) {
    int32_t next = alias_v[id];
    alias_v[id] = cur;
    id = next;
  }
  return cur;
}

static int32_t canonical(int32_t id) {
  int32_t r = resolve(id);
  int32_t c = canon_v[r];
  return c >= 0 ? c : r;
}

// Emit a construction node, using hash-consing to deduplicate
static void emit(int32_t target, int32_t func, int32_t arg) {
  int32_t cf = canonical(func);
  int32_t ca = canonical(arg);
  auto key = std::make_pair(cf, ca);
  auto it = hash_cons.find(key);
  if (it != hash_cons.end()) {
    canon_v[target] = it->second;
  } else {
    int32_t cn = fresh_canon();
    canon_v[target] = cn;
    hash_cons[key] = cn;
    add_node_def(cn, cf, ca);
    output_buffer.push_back({0, cn, cn, cf, ca});
  }
}

struct Task {
  int32_t target, func, arg;
  std::vector<std::pair<int32_t, int32_t>> pending_memo_keys;
};

static void process_apply(int32_t target, int32_t func, int32_t arg) {
  std::vector<Task> stack;
  stack.push_back({target, func, arg, {}});

  while (!stack.empty()) {
    if (stats_enabled) ++stat_reduction_steps;
    auto task = std::move(stack.back());
    stack.pop_back();

    int32_t cf = canonical(task.func);
    int32_t ca = canonical(task.arg);
    auto memo_key = std::make_pair(cf, ca);

    auto memo_it = apply_memo.find(memo_key);
    if (memo_it != apply_memo.end()) {
      int32_t prev = memo_it->second;
      env[task.target] = env[prev];
      alias_v[task.target] = prev;
      for (auto& pk : task.pending_memo_keys) apply_memo[pk] = prev;
      continue;
    }

    alias_v[task.target] = -1;
    // Copy Info by value — env[] may be resized by fresh_temp()/ensure_alloc()
    Info info = env[task.func];
    switch (info.type) {
      case LEAF: {
        env[task.target] = {STEM, task.arg, -1};
        emit(task.target, task.func, task.arg);
        int32_t resolved = resolve(task.target);
        apply_memo[memo_key] = resolved;
        for (auto& pk : task.pending_memo_keys) apply_memo[pk] = resolved;
        break;
      }
      case STEM: {
        env[task.target] = {FORK, info.a, task.arg};
        emit(task.target, task.func, task.arg);
        int32_t resolved = resolve(task.target);
        apply_memo[memo_key] = resolved;
        for (auto& pk : task.pending_memo_keys) apply_memo[pk] = resolved;
        break;
      }
      case FORK: {
        if (stats_enabled) ++stat_contractions;
        Info iu = env[info.a];
        switch (iu.type) {
          case LEAF: {
            int32_t rb = resolve(info.b);
            env[task.target] = env[rb];
            alias_v[task.target] = rb;
            int32_t resolved = resolve(task.target);
            apply_memo[memo_key] = resolved;
            for (auto& pk : task.pending_memo_keys) apply_memo[pk] = resolved;
            break;
          }
          case STEM: {
            int32_t t1 = fresh_temp();
            int32_t t2 = fresh_temp();
            auto new_pending = std::move(task.pending_memo_keys);
            new_pending.push_back(memo_key);
            stack.push_back({task.target, t1, t2, std::move(new_pending)});
            stack.push_back({t2, info.b, task.arg, {}});
            stack.push_back({t1, iu.a, task.arg, {}});
            break;
          }
          case FORK: {
            Info ic = env[task.arg];
            switch (ic.type) {
              case LEAF: {
                int32_t ra = resolve(iu.a);
                env[task.target] = env[ra];
                alias_v[task.target] = ra;
                int32_t resolved = resolve(task.target);
                apply_memo[memo_key] = resolved;
                for (auto& pk : task.pending_memo_keys) apply_memo[pk] = resolved;
                break;
              }
              case STEM: {
                auto new_pending = std::move(task.pending_memo_keys);
                new_pending.push_back(memo_key);
                stack.push_back({task.target, iu.b, ic.a, std::move(new_pending)});
                break;
              }
              case FORK: {
                int32_t t = fresh_temp();
                auto new_pending = std::move(task.pending_memo_keys);
                new_pending.push_back(memo_key);
                stack.push_back({task.target, t, ic.b, std::move(new_pending)});
                stack.push_back({t, info.b, ic.a, {}});
                break;
              }
            }
            break;
          }
        }
        break;
      }
    }
  }
}

int main(int argc, char* argv[]) {
  bool progress = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--progress") progress = true;
    if (arg == "--stats") stats_enabled = true;
  }

  // Pre-allocate for typical workload
  env.reserve(1 << 20);
  alias_v.reserve(1 << 20);
  canon_v.reserve(1 << 20);
  named_strs.reserve(1 << 18);
  output_buffer.reserve(1 << 18);

  int32_t leaf_id = intern("\xe2\x96\xb3"); // △
  ensure_id(leaf_id);
  env[leaf_id] = {LEAF, -1, -1};

  // Read all input at once for fast parsing
  std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  const char* p = input.data();
  const char* end = p + input.size();
  std::string words[4];
  int lineno = 0;

  while (p < end) {
    // Skip to start of line content
    while (p < end && *p == ' ') ++p;
    if (p >= end) break;
    if (*p == '\n') { ++p; continue; }

    // Parse up to 4 space-separated words from this line
    int nwords = 0;
    while (nwords < 4 && p < end && *p != '\n') {
      const char* ws = p;
      while (p < end && *p != ' ' && *p != '\n') ++p;
      words[nwords++].assign(ws, p - ws);
      while (p < end && *p == ' ') ++p;
    }
    // Skip rest of line
    while (p < end && *p != '\n') ++p;
    if (p < end) ++p;

    if (progress) std::cerr << ++lineno << " " << words[0] << "\n";

    if (nwords == 0) continue;
    int32_t a = intern(words[0]);
    ensure_id(a);
    if (nwords == 1) {                                                     // 1-word: terminal
      int32_t ca = canonical(a);
      reachability_roots.push_back(ca);
      output_buffer.push_back({2, -1, ca, 0, 0});
      continue;
    }
    int32_t b = intern(words[1]);
    ensure_id(b);
    if (nwords == 2) {                                                    // 2-word: alias/export
      alias_v[a] = -1;
      env[a] = env[b];
      canon_v[a] = a;
      int32_t cb = canonical(b);
      reachability_roots.push_back(cb);
      output_buffer.push_back({1, -1, a, cb, 0});
      continue;
    }
    if (nwords > 3) continue;                                             // 4+ words: drop
    int32_t c = intern(words[2]);
    ensure_id(c);
    process_apply(a, b, c);                                              // 3-word: application
  }

  // Reachability walk
  size_t max_id = named_strs.size();
  if (max_id < (size_t)next_id) max_id = (size_t)next_id;
  std::vector<bool> reachable(max_id, false);
  {
    std::vector<int32_t> worklist;
    for (int32_t r : reachability_roots) {
      if (has_node_def(r)) worklist.push_back(r);
    }
    while (!worklist.empty()) {
      int32_t name = worklist.back();
      worklist.pop_back();
      if (reachable[name]) continue;
      reachable[name] = true;
      auto& def = get_node_def(name);
      if (has_node_def(def.first) && !reachable[def.first])
        worklist.push_back(def.first);
      if (has_node_def(def.second) && !reachable[def.second])
        worklist.push_back(def.second);
    }
  }

  // Output in original order, filtering unreachable node definitions
  std::string out;
  out.reserve(16 * 1024 * 1024);
  for (auto& entry : output_buffer) {
    if (entry.type == 0) {
      if (!reachable[entry.canon_id]) continue;
      out += named_strs[entry.w1];
      out += ' ';
      out += named_strs[entry.w2];
      out += ' ';
      out += named_strs[entry.w3];
    } else if (entry.type == 1) {
      out += named_strs[entry.w1];
      out += ' ';
      out += named_strs[entry.w2];
    } else {
      out += named_strs[entry.w1];
    }
    out += '\n';
    if (stats_enabled) ++stat_output_lines;
  }
  fwrite(out.data(), 1, out.size(), stdout);

  if (stats_enabled) {
    std::cerr << std::left << std::setw(17) << "Contractions:" << stat_contractions << "\n";
    std::cerr << std::left << std::setw(17) << "Reduction steps:" << stat_reduction_steps << "\n";
    std::cerr << std::left << std::setw(17) << "Output lines:" << stat_output_lines << "\n";
  }
}
