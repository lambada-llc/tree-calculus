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
static int counter = 0;

static std::string fresh() { return "r" + std::to_string(counter++); }

static std::string resolve(const std::string& name) {
  const std::string* cur = &name;
  while (true) {
    auto it = alias.find(*cur);
    if (it == alias.end()) return *cur;
    cur = &it->second;
  }
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

// a = fork(u, v) applied to c — this is where tree calculus reduction happens
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
      auto t1 = fresh();
      process_apply(t1, iu.a, c);
      auto t2 = fresh();
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
          auto t = fresh();
          process_apply(t, v, ic.a);
          process_apply(target, t, ic.b);
          return;
        }
      }
    }
  }
}

static void process_apply(const std::string& target, const std::string& func, const std::string& arg) {
  alias.erase(target);
  auto info = lookup(func);
  switch (info.type) {
    case LEAF:
      // apply(leaf, x) = stem(x) — construction, no reduction
      env[target] = {STEM, arg, ""};
      std::cout << target << " " << resolve(func) << " " << resolve(arg) << "\n";
      return;
    case STEM:
      // apply(stem(u), x) = fork(u, x) — construction, no reduction
      env[target] = {FORK, info.a, arg};
      std::cout << target << " " << resolve(func) << " " << resolve(arg) << "\n";
      return;
    case FORK:
      // apply(fork(u, v), x) — reduction!
      reduce(target, info.a, info.b, arg);
      return;
  }
}

int main() {
  env["\xe2\x96\xb3"] = {LEAF, "", ""}; // △

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream iss(line);
    std::string a, b, c, extra;
    if (!(iss >> a)) continue;
    if (!(iss >> b)) { std::cout << resolve(a) << "\n"; continue; }  // 1-word: terminal
    if (!(iss >> c)) { alias.erase(a); env[a] = lookup(b); std::cout << a << " " << resolve(b) << "\n"; continue; } // 2-word: alias
    if (iss >> extra) continue;                                       // 4+ words: drop
    process_apply(a, b, c);                                           // 3-word: application
  }
}
