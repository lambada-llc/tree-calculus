#include <iostream>
#include <vector>
#include <functional>
#include <numeric>
#include <chrono>

typedef int Tree;

class Evaluator {
private:
  std::vector<int> _type; // 0 = leaf, 1 = stem, 2 = fork
  std::vector<int> _u;    // (left) child for stems or forks
  std::vector<int> _v;    // right child for forks

public:
  Evaluator() {
    // put the one and only leaf node at index 0
    _type.push_back(0);
    _u.push_back(0);
    _v.push_back(0);
  }

  int size() {
    return _type.size();
  }

  Tree leaf() {
    return 0;
  }

  Tree stem(Tree u) {
    _type.push_back(1);
    _u.push_back(u);
    _v.push_back(0);
    return _type.size() - 1;
  }

  Tree fork(Tree u, Tree v) {
    _type.push_back(2);
    _u.push_back(u);
    _v.push_back(v);
    return _type.size() - 1;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case, std::function<T(Tree)> stem_case, std::function<T(Tree, Tree)> fork_case, Tree x) {
    switch (_type[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(_u[x]);
      case 2: return fork_case(_u[x], _v[x]);
      default: throw std::runtime_error("invariant violation: type " + std::to_string(_type[x]) + " at index " + std::to_string(x) + " not 0, 1 or 2");
    }
  }

  Tree apply(Tree a, Tree b) {
    switch (_type[a]) {
      case 0: return stem(b);
      case 1: return fork(_u[a], b);
      case 2:
        Tree u = _u[a];
        switch (_type[u]) {
          case 0: return _v[a];
          case 1: return apply(apply(_u[u], b), apply(_v[a], b));
          case 2:
            switch (_type[b]) {
              case 0: return _u[u];
              case 1: return apply(_v[u], _u[b]);
              case 2: return apply(apply(_v[a], _u[b]), _v[b]);
            }
            throw std::runtime_error("invariant violation: type " + std::to_string(_type[b]) + " at index " + std::to_string(b) + " not 0, 1 or 2");
        }
        throw std::runtime_error("invariant violation: type " + std::to_string(_type[u]) + " at index " + std::to_string(u) + " not 0, 1 or 2");
    }
    throw std::runtime_error("invariant violation: type " + std::to_string(_type[a]) + " at index " + std::to_string(a) + " not 0, 1 or 2");
  }

  Tree of_ternary(std::string s) {
    std::vector<int> stack;
    int u, v;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
      char c = *it;
      switch (c) {
        case '0': stack.push_back(leaf()); break;
        case '1':
          u = stack.back(); stack.pop_back();
          stack.push_back(stem(u));
          break;
        case '2':
          u = stack.back(); stack.pop_back();
          v = stack.back(); stack.pop_back();
          stack.push_back(fork(u, v));
          break;
        default:
          throw std::runtime_error("unexpected character in ternary encoding: " + std::string(1, c));
      }
    }
    if (stack.size() != 1) throw std::runtime_error("invariant violation: stack size is not 1 after decoding");
    return stack.back();
  }

  std::string to_ternary(Tree x) {
    std::string res;
    std::function<void(Tree)> triage = [&](Tree x) {
      switch (_type[x]) {
        case 0: res.push_back('0'); break;
        case 1: res.push_back('1'); triage(_u[x]); break;
        case 2: res.push_back('2'); triage(_u[x]); triage(_v[x]); break;
        default: throw std::runtime_error("invariant violation: type " + std::to_string(_type[x]) + " at index " + std::to_string(x) + " not 0, 1 or 2");
      }
    };
    triage(x);
    return res;
  }

  Tree t_false() {
    return leaf();
  }

  Tree t_true() {
    return stem(leaf());
  }

  bool to_bool(Tree x) {
    return triage<bool>(
      []() { return false; }, 
      [&](Tree) { return true; }, 
      [&](Tree, Tree) -> bool { throw std::runtime_error("tree is not a bool"); },
      x);
  }

  Tree of_bool(bool b) {
    return b ? t_true() : t_false();
  }

  std::vector<Tree> to_list(Tree x) {
    std::vector<Tree> res;
    while (true) {
      switch (_type[x]) {
        case 0: return res;
        case 1: throw std::runtime_error("tree is not a list");
        case 2:
          res.push_back(_u[x]);
          x = _v[x];
          break;
        default:
          throw std::runtime_error("invariant violation: type " + std::to_string(_type[x]) + " at index " + std::to_string(x) + " not 0, 1 or 2");
      }
    }
  }

  Tree of_list(std::vector<Tree> l) {
    Tree f = leaf();
    for (int i = l.size(); i; i--) f = fork(l[i - 1], f);
    return f;
  }

  int64_t to_nat(Tree x) {
    int64_t result = 0;
    std::vector<Tree> list = to_list(x);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
      result = 2 * result + (to_bool(*it) ? 1 : 0);
    return result;
  }

  Tree of_nat(int64_t n) {
    std::vector<Tree> l;
    for (; n; n >>= 1)
      l.push_back(of_bool(n % 2 == 1));
    return of_list(l);
  }

  std::string to_string(Tree x) {
    std::string result;
    for (Tree b : to_list(x))
      result += static_cast<char>(to_nat(b));
    return result;
  }

  Tree of_string(std::string s) {
    std::vector<Tree> l;
    for (char c : s)
      l.push_back(of_nat(c));
    return of_list(l);
  }
};

std::string bench_recursive_fib_ternary =
  "21212021212011212110021100102021202121202120002120112021212120112000202021212"
  "01121211002110010202120212012210002121202121202121202120002120102120002010212"
  "02120112120112000101020011201020110212011212011212110021100101020021202120112"
  "12021202120001021202120112110010212120112121100211001020212021201200212021212"
  "12011200020202200212011201002001120110212011212011212110021100101010212120212"
  "02120001021200021202121202120002120102120002010212120212000102021212011212110"
  "02110010202120212012002120212121201120002020220021201120100200112011021201121"
  "20112121100211001010202210200202002120112120112121100211001010212120112121100"
  "21100102021202120120021202121212011200020202200212011201002001120110212011212"
  "0112121100211001010200";

std::string bench_linear_fib_ternary =
  "21202200102121212011212110021100102021202120122110002120112011201200212120212"
  "12021200021201021200020102120112021212021200021201021200020102120112011202120"
  "21200010212011201120212021200010212120212021200010212021200010202120112021212"
  "01121211002110010202120212012002120212121201120002020220021201120100200112011"
  "02120112120112121100211001010020212011021212021212021200021201021200020102120"
  "11202121201121211002110010202120212012220202100002121202120212000102120212120"
  "21200021201021200020102120112021202120001021201120120011202120112120212021200"
  "01001020212011212012002222210200202121200221020002110002022212120022102000211"
  "00202102010001021201121201121211002110010102121201121211002110010202120212011"
  "21202120002220212120112121100211001020212021201200212011212021202120001021200"
  "10102120112120112121100211001010021201021201221212011212110021100102021202120"
  "12002120112120212021200010212002102001021201121201121211002110010100212011201"
  "02120112000212012211000212021201121202120002120112021212011200020001011201021"
  "20112120112121100211001010200212011212011212110021100101020221002100";

void test_basic_reduction_rules(Evaluator &e) {
  auto ruleCheck = [&](std::string rule, std::string expected, std::string a, std::string b) {
    auto actual = e.to_ternary(e.apply(e.of_ternary(a), e.of_ternary(b)));
    if (actual != expected) {
      throw std::runtime_error("rule " + rule + " failed: " + a + " " + b + " --> " + expected + " expected but got " + actual);
    }
  };

  std::string tl = "0";
  std::string ts = "10";
  std::string tf = "200";
  std::vector<std::string> t = {tl, ts, tf}; // some simple trees

  for (const auto &z : t)
    ruleCheck("0a", "1" + z, "0", z);

  for (const auto &y : t)
    for (const auto &z : t)
      ruleCheck("0b", "2" + y + z, "1" + y, z);

  for (const auto &y : t)
    for (const auto &z : t)
      ruleCheck("1", y, "20" + y, z);

  for (const auto &z : t)
    ruleCheck("2", "2" + z + "1" + z, "2100", z); // x = 0, y = 0

  for (const auto &yc : t)
    for (const auto &z : t)
      ruleCheck("2", "2" + z + "2" + yc + z, "2101" + yc, z); // x = 0, y = 1+yz

  for (const auto &y : t)
    for (const auto &z : t)
      ruleCheck("2", z, "2110" + y, z); // x = 10

  for (const auto &w : t)
    for (const auto &x : t)
      for (const auto &y : t)
        ruleCheck("3a", w, "22" + w + x + y, "0");

  for (const auto &w : t)
    for (const auto &y : t)
      for (const auto &u : t)
        ruleCheck("3b", "1" + u, "22" + w + "0" + y, "1" + u); // x = 0

  for (const auto &w : t)
    for (const auto &y : t)
      for (const auto &u : t)
        ruleCheck("3b", "20" + u, "22" + w + "10" + y, "1" + u); // x = 10

  for (const auto &w : t)
    for (const auto &x : t)
      for (const auto &u : t)
        for (const auto &v : t)
          ruleCheck("3c", "2" + u + v, "22" + w + x + "0", "2" + u + v); // y = 0

  for (const auto &w : t)
    for (const auto &x : t)
      for (const auto &u : t)
        for (const auto &v : t)
          ruleCheck("3c", u, "22" + w + x + "10", "2" + u + v); // y = 10
}

template <typename T>
double measure_sec(std::function<T()> func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  return duration.count();
}

template <typename T>
std::vector<double> repeat_measure_sec(std::function<T()> func, int iterations = 10) {
  std::vector<double> samples;
  for (int i = 0; i < iterations; ++i)
    samples.push_back(measure_sec(func));
  return samples;
}

void print_statistics(std::string title, const std::vector<double>& samples) {
  if (samples.empty()) {
    std::cout << "No data available." << std::endl;
    return;
  }

  double min = *std::min_element(samples.begin(), samples.end());
  double max = *std::max_element(samples.begin(), samples.end());
  double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  double average = sum / samples.size();

  std::vector<double> sorted_samples = samples;
  std::sort(sorted_samples.begin(), sorted_samples.end());
  double median = sorted_samples.size() % 2 == 0
    ? (sorted_samples[sorted_samples.size() / 2 - 1] + sorted_samples[sorted_samples.size() / 2]) / 2
    : sorted_samples[sorted_samples.size() / 2];

  std::cout << "Benchmark: " << title << " (in seconds)" << std::endl;
  std::cout << "  Min: " << min << std::endl;
  std::cout << "  Max: " << max << std::endl;
  std::cout << "  Average: " << average << std::endl;
  std::cout << "  Median: " << median << std::endl;
}

void sanity_checks() {
  Evaluator e;
  test_basic_reduction_rules(e);
  Tree bench_recursive_fib = e.of_ternary(bench_recursive_fib_ternary);
  Tree bench_linear_fib = e.of_ternary(bench_linear_fib_ternary);
  if (e.to_nat(e.apply(bench_recursive_fib, e.of_nat(9))) != 55)
    throw std::runtime_error("fib misbehavior");
  if (e.to_nat(e.apply(bench_linear_fib, e.of_nat(9))) != 55)
    throw std::runtime_error("fib misbehavior");
  std::cout << std::endl << "Tree nodes allocated: " << e.size() << std::endl;
}

int main() {
  sanity_checks();
  print_statistics(
    "Setup, should be negligibly fast",
    repeat_measure_sec<void>(
      []() { 
        Evaluator e;
        e.of_ternary(bench_recursive_fib_ternary);
        e.of_ternary(bench_linear_fib_ternary);
      }));
  print_statistics(
    "Linear fib",
    repeat_measure_sec<void>(
      []() {
        Evaluator e;
        Tree fib = e.of_ternary(bench_linear_fib_ternary);
        auto result = e.to_nat(e.apply(fib, e.of_nat(90)));
        if (result != 4660046610375530309)
          throw std::runtime_error("fib misbehavior: " + std::to_string(result));
      }));
  print_statistics(
    "Recursive fib",
    repeat_measure_sec<void>(
      []() { 
        Evaluator e;
        Tree fib = e.of_ternary(bench_recursive_fib_ternary);
        auto result = e.to_nat(e.apply(fib, e.of_nat(26)));
        if (result != 196418)
          throw std::runtime_error("fib misbehavior: " + std::to_string(result));
      }));
  return 0;
}
