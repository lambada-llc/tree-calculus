#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

enum Type { LEAF, STEM, FORK };

struct Info {
  Type type;
  std::string a, b; // STEM: a=child; FORK: a=left(u), b=right(v)
};

static std::unordered_map<std::string, Info> env;
static std::unordered_map<std::string, std::string> alias;
static std::unordered_map<std::string, std::string> canon;
static std::unordered_map<std::string, std::string> hash_cons;
static std::unordered_map<std::string, std::string> apply_memo;
static int canon_counter = 0;
static int temp_counter = 0;

static std::string fresh_canon() { return ":" + std::to_string(canon_counter++); }
static std::string fresh_temp() { return ":t:" + std::to_string(temp_counter++); }

// Follow reduction alias chain to the final name
static std::string resolve(const std::string& name) {
  const std::string* cur = &name;
  while (true) {
    auto it = alias.find(*cur);
    if (it == alias.end()) return *cur;
    cur = &it->second;
  }
}

// Get the canonical output name for a node (follows aliases then looks up canon)
static std::string canonical(const std::string& name) {
  auto r = resolve(name);
  auto it = canon.find(r);
  if (it != canon.end()) return it->second;
  return r; // undefined symbol (e.g., △) — keep as-is
}

static Info lookup(const std::string& name) {
  auto it = env.find(name);
  if (it == env.end()) {
    std::cerr << "unbound: " << name << std::endl;
    std::exit(1);
  }
  return it->second;
}

static void process_apply(const std::string& target, const std::string& func, const std::string& arg);

// Emit a construction node, using hash-consing to deduplicate
static void emit(const std::string& target, const std::string& func, const std::string& arg) {
  auto cf = canonical(func);
  auto ca = canonical(arg);
  std::string key;
  key.reserve(cf.size() + 1 + ca.size());
  key += cf;
  key += '\0';
  key += ca;
  auto it = hash_cons.find(key);
  if (it != hash_cons.end()) {
    canon[target] = it->second;
  } else {
    auto cn = fresh_canon();
    canon[target] = cn;
    hash_cons[key] = cn;
    std::cout << cn << " " << cf << " " << ca << "\n";
  }
}

// a = fork(u, v) applied to c — tree calculus reduction
static void reduce(const std::string& target, const std::string& u, const std::string& v, const std::string& c) {
  auto iu = lookup(u);
  switch (iu.type) {
    case LEAF:
      // apply(fork(leaf, v), c) = v
      env[target] = lookup(v);
      alias[target] = resolve(v);
      return;
    case STEM: {
      // apply(fork(stem(u'), v), c) = apply(apply(u', c), apply(v, c))
      auto t1 = fresh_temp();
      process_apply(t1, iu.a, c);
      auto t2 = fresh_temp();
      process_apply(t2, v, c);
      process_apply(target, t1, t2);
      return;
    }
    case FORK: {
      // apply(fork(fork(u', v'), v), c) = triage on c
      auto ic = lookup(c);
      switch (ic.type) {
        case LEAF:
          // c = leaf -> u'
          env[target] = lookup(iu.a);
          alias[target] = resolve(iu.a);
          return;
        case STEM:
          // c = stem(c') -> apply(v', c')
          process_apply(target, iu.b, ic.a);
          return;
        case FORK: {
          // c = fork(cu, cv) -> apply(apply(v, cu), cv)
          auto t = fresh_temp();
          process_apply(t, v, ic.a);
          process_apply(target, t, ic.b);
          return;
        }
      }
    }
  }
}

static void process_apply(const std::string& target, const std::string& func, const std::string& arg) {
  auto cf = canonical(func);
  auto ca = canonical(arg);
  std::string memo_key;
  memo_key.reserve(cf.size() + 1 + ca.size());
  memo_key += cf;
  memo_key += '\0';
  memo_key += ca;

  auto memo_it = apply_memo.find(memo_key);
  if (memo_it != apply_memo.end()) {
    auto& prev = memo_it->second;
    env[target] = env[prev];
    alias[target] = prev;
    return;
  }

  alias.erase(target);
  auto info = lookup(func);
  switch (info.type) {
    case LEAF:
      // apply(leaf, x) = stem(x) — construction, no reduction
      env[target] = {STEM, arg, ""};
      emit(target, func, arg);
      break;
    case STEM:
      // apply(stem(u), x) = fork(u, x) — construction, no reduction
      env[target] = {FORK, info.a, arg};
      emit(target, func, arg);
      break;
    case FORK:
      // apply(fork(u, v), x) — reduction!
      reduce(target, info.a, info.b, arg);
      break;
  }

  apply_memo[memo_key] = resolve(target);
}

int main() {
  env["\xe2\x96\xb3"] = {LEAF, "", ""}; // △

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream iss(line);
    std::string a, b, c, extra;
    if (!(iss >> a)) continue;
    if (!(iss >> b)) { std::cout << canonical(a) << "\n"; continue; }  // 1-word: terminal
    if (!(iss >> c)) {                                                   // 2-word: alias/export — keep LHS name
      alias.erase(a);
      env[a] = lookup(b);
      canon[a] = a;
      std::cout << a << " " << canonical(b) << "\n";
      continue;
    }
    if (iss >> extra) continue;                                          // 4+ words: drop
    process_apply(a, b, c);                                             // 3-word: application
  }
}
