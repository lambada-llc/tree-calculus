#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

static std::unordered_map<std::string, std::string> canon;
static std::unordered_map<std::string, std::string> hash_cons;
static int counter = 0;

static std::string fresh() { return ":" + std::to_string(counter++); }

static std::string resolve(const std::string& name) {
  auto it = canon.find(name);
  if (it != canon.end()) return it->second;
  return name; // undefined symbol — keep as-is
}

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream iss(line);
    std::string a, b, c, extra;
    if (!(iss >> a)) continue;
    if (!(iss >> b)) {
      // 1-word: terminal reference
      std::cout << resolve(a) << "\n";
      continue;
    }
    if (!(iss >> c)) {
      // 2-word: export — keep LHS name
      canon[a] = a;
      std::cout << a << " " << resolve(b) << "\n";
      continue;
    }
    if (iss >> extra) continue; // 4+ words: drop
    // 3-word: application — hash-cons on (resolved func, resolved arg)
    auto cb = resolve(b);
    auto cc = resolve(c);
    std::string key;
    key.reserve(cb.size() + 1 + cc.size());
    key += cb;
    key += '\0';
    key += cc;
    auto it = hash_cons.find(key);
    if (it != hash_cons.end()) {
      canon[a] = it->second;
    } else {
      auto ca = fresh();
      canon[a] = ca;
      hash_cons[key] = ca;
      std::cout << ca << " " << cb << " " << cc << "\n";
    }
  }
}
