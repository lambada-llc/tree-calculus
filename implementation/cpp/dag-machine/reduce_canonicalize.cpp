#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

struct Task {
  std::string target, func, arg;
};

static void process_apply(const std::string& target, const std::string& func, const std::string& arg) {
  std::vector<Task> stack;
  stack.push_back({target, func, arg});

  while (!stack.empty()) {
    auto task = std::move(stack.back());
    stack.pop_back();

    auto cf = canonical(task.func);
    auto ca = canonical(task.arg);
    std::string memo_key;
    memo_key.reserve(cf.size() + 1 + ca.size());
    memo_key += cf;
    memo_key += '\0';
    memo_key += ca;

    auto memo_it = apply_memo.find(memo_key);
    if (memo_it != apply_memo.end()) {
      auto& prev = memo_it->second;
      env[task.target] = env[prev];
      alias[task.target] = prev;
      continue;
    }

    alias.erase(task.target);
    auto info = lookup(task.func);
    switch (info.type) {
      case LEAF:
        env[task.target] = {STEM, task.arg, ""};
        emit(task.target, task.func, task.arg);
        apply_memo[memo_key] = resolve(task.target);
        break;
      case STEM:
        env[task.target] = {FORK, info.a, task.arg};
        emit(task.target, task.func, task.arg);
        apply_memo[memo_key] = resolve(task.target);
        break;
      case FORK: {
        // Inline reduce(target, info.a, info.b, arg)
        auto iu = lookup(info.a);
        switch (iu.type) {
          case LEAF:
            env[task.target] = lookup(info.b);
            alias[task.target] = resolve(info.b);
            apply_memo[memo_key] = resolve(task.target);
            break;
          case STEM: {
            auto t1 = fresh_temp();
            auto t2 = fresh_temp();
            stack.push_back({task.target, t1, t2});
            stack.push_back({t2, info.b, task.arg});
            stack.push_back({t1, iu.a, task.arg});
            break;
          }
          case FORK: {
            auto ic = lookup(task.arg);
            switch (ic.type) {
              case LEAF:
                env[task.target] = lookup(iu.a);
                alias[task.target] = resolve(iu.a);
                apply_memo[memo_key] = resolve(task.target);
                break;
              case STEM:
                stack.push_back({task.target, iu.b, ic.a});
                break;
              case FORK: {
                auto t = fresh_temp();
                stack.push_back({task.target, t, ic.b});
                stack.push_back({t, info.b, ic.a});
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
