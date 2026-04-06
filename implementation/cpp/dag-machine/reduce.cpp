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
static int counter = 0;

static std::string fresh() { return ":reduced:" + std::to_string(counter++); }

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

struct Task {
  std::string target, func, arg;
};

static void process_apply(const std::string& target, const std::string& func, const std::string& arg) {
  std::vector<Task> stack;
  stack.push_back({target, func, arg});

  while (!stack.empty()) {
    auto task = std::move(stack.back());
    stack.pop_back();

    alias.erase(task.target);
    auto info = lookup(task.func);
    switch (info.type) {
      case LEAF:
        env[task.target] = {STEM, task.arg, ""};
        std::cout << task.target << " " << resolve(task.func) << " " << resolve(task.arg) << "\n";
        break;
      case STEM:
        env[task.target] = {FORK, info.a, task.arg};
        std::cout << task.target << " " << resolve(task.func) << " " << resolve(task.arg) << "\n";
        break;
      case FORK: {
        // Inline reduce(target, info.a, info.b, arg)
        auto iu = lookup(info.a);
        switch (iu.type) {
          case LEAF:
            env[task.target] = lookup(info.b);
            alias[task.target] = resolve(info.b);
            break;
          case STEM: {
            auto t1 = fresh();
            auto t2 = fresh();
            // Push in reverse order so t1 executes first
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
                break;
              case STEM:
                stack.push_back({task.target, iu.b, ic.a});
                break;
              case FORK: {
                auto t = fresh();
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

int main(int argc, char* argv[]) {
  bool progress = argc > 1 && std::string(argv[1]) == "--progress";
  env["\xe2\x96\xb3"] = {LEAF, "", ""}; // △

  std::string line;
  int lineno = 0;
  while (std::getline(std::cin, line)) {
    if (progress) std::cerr << ++lineno << " " << line << "\n";
    std::istringstream iss(line);
    std::string a, b, c, extra;
    if (!(iss >> a)) continue;
    if (!(iss >> b)) { std::cout << resolve(a) << "\n"; continue; }  // 1-word: terminal
    if (!(iss >> c)) { alias.erase(a); env[a] = lookup(b); std::cout << a << " " << resolve(b) << "\n"; continue; } // 2-word: alias
    if (iss >> extra) continue;                                       // 4+ words: drop
    process_apply(a, b, c);                                           // 3-word: application
  }
}
