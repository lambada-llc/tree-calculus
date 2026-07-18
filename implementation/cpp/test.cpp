#include "eager-value-mem.hpp"
#include "eager-ternary.hpp"
#include "eager-ternary-ref.hpp"
#include "eager-ternary-len.hpp"
#include "eager-ternary-vm.hpp"
#include "eager-ternary-nil.hpp"
#include "eager-ternary-nil-32.hpp"
#include "eager-ternary-nil-vm.hpp"
#include "eager-ternary-nil-vm-32.hpp"
#include "eager-ternary-nil-mmap.hpp"
#include "eager-ternary-nil-mmap-32.hpp"
#include "eager-ternary-nil-mmap-vm.hpp"
#include "eager-ternary-nil-mmap-vm-32.hpp"
#include "eager-value-mem-peek.hpp"
#include "eager-ternary-nil-mmap-peek.hpp"
#include "eager-ternary-nil-mmap-32-peek.hpp"
#include "eager-stacks.hpp"
#include "lazy-app-stream.hpp"
#include "evaluator.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>

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

// merge-sort program from benchmark/run.sh: sorts a fork-encoded list of nats.
std::string bench_merge_sort_ternary =
  R"(212120112121100211001020212021201200212011212021212011200020021020021212021212021200021201021200020102120112021212021200021201021200020102120112011202120212000102120112011201200212120212120212000212010212000201021201120212021200010212011202121202120002120102120002010212011201120212021200010212011201120212120112121100211001020212021201221100021201121202120002120112021212011200020002121202121202120002120102120002010212011202121202120002120102120002010212011201120212000212011202121202120002120102120002010212120212000212011202121202120002120102120002010212120212021200010212021202120001021212021212021212021200021201021200020102120212011212011200010102002120212011212021202120001021202120212000102120212021200010010212120212120212000212010212000201021201120212021200010112021202120001020021201120112021201121202120212000100102121202120212000102120212000102002021201120221002120212120112121100211001020212021201121212011200020021212021202120001021202120001020212012002121202121202121202120002120102120002010212021201121201120001010200212121202121202120002120102120002010212021201121201120001010200200212121201120002020102002120112120112121100211001010220022221020100202202010002121201121211002110010202120212011212021200022202121201121211002110010202120212012002120112120212021200010212001010212011212011212110021100101002120102120122121201121211002110010202120212012002120112120212021200010212002102001021201121201121211002110010100212011201021201120002120122110002120212011212021200021201120212120112000200010112010212011212011212110021100101020211002021100212011212011212110021100101021212021202120001021202120001020021212021202120001021202120001020020212120112121100211001020212021201220002120112120212021200010212012002120212120212000212010212000201021201120001021201121201121211002110010102120112120112121100211001010)";

template <typename Impl>
void test_basic_reduction_rules(Evaluator<Impl> &e) {
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

std::vector<double> repeat_measure_sec(std::function<void()> func, int iterations = 10) {
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

// Compute expected fib value matching the tree-calculus fib program's indexing:
// fib(0)=1, fib(1)=1, fib(2)=2, ..., fib(10)=89
int64_t expected_fib(int n) {
  int64_t a = 0, b = 1;
  for (int i = 0; i <= n; ++i) { int64_t t = a + b; a = b; b = t; }
  return a;
}

// Build a fork-encoded list of natural numbers on evaluator e.
template <typename E>
typename E::Tree nat_list(E &e, const std::vector<int> &xs) {
  std::vector<typename E::Tree> elems;
  for (int x : xs)
    elems.push_back(e.of_nat(x));
  return e.of_list(elems);
}

template <typename Impl>
void sanity_checks(std::string name) {
  std::cout << "Testing " << name << "..." << std::endl;
  Evaluator<Impl> e;
  test_basic_reduction_rules(e);
  std::cout << "  All reduction rules passed." << std::endl;
  std::cout << "    Stats: " << e.stats() << std::endl;
  auto bench_recursive_fib = e.of_ternary(bench_recursive_fib_ternary);
  auto bench_linear_fib = e.of_ternary(bench_linear_fib_ternary);
  if (e.to_nat(e.apply(bench_recursive_fib, e.of_nat(10))) != 89)
    throw std::runtime_error("fib misbehavior");
  if (e.to_nat(e.apply(bench_linear_fib, e.of_nat(10))) != 89)
    throw std::runtime_error("fib misbehavior");
  std::cout << "  Fib behaves as expected." << std::endl;
  // Merge-sort: sorting the descending list [n..1] yields the ascending [1..n].
  const int sort_n = 20;
  std::vector<int> descending, ascending;
  for (int i = sort_n; i >= 1; --i) descending.push_back(i);
  for (int i = 1; i <= sort_n; ++i) ascending.push_back(i);
  auto merge_sort = e.of_ternary(bench_merge_sort_ternary);
  if (e.to_ternary(e.apply(merge_sort, nat_list(e, descending))) !=
      e.to_ternary(nat_list(e, ascending)))
    throw std::runtime_error("merge-sort misbehavior");
  std::cout << "  Merge-sort behaves as expected." << std::endl;
  std::cout << "    Stats: " << e.stats() << std::endl;
}

template <typename Impl>
void bench_evaluator(std::string name, int linear_fib_n, int recursive_fib_n, int iterations = 10) {
  int64_t expected_linear = expected_fib(linear_fib_n);
  int64_t expected_recursive = expected_fib(recursive_fib_n);

  print_statistics(
    "[" + name + "] Setup, should be negligibly fast",
    repeat_measure_sec(
      []() {
        Evaluator<Impl> e;
        e.of_ternary(bench_recursive_fib_ternary);
        e.of_ternary(bench_linear_fib_ternary);
      }, iterations));
  print_statistics(
    "[" + name + "] Linear fib(" + std::to_string(linear_fib_n) + ")",
    repeat_measure_sec(
      [&]() {
        Evaluator<Impl> e;
        auto fib = e.of_ternary(bench_linear_fib_ternary);
        auto result = e.to_nat(e.apply(fib, e.of_nat(linear_fib_n)));
        if (result != expected_linear)
          throw std::runtime_error("fib misbehavior: " + std::to_string(result));
      }, iterations));
  print_statistics(
    "[" + name + "] Recursive fib(" + std::to_string(recursive_fib_n) + ")",
    repeat_measure_sec(
      [&]() {
        Evaluator<Impl> e;
        auto fib = e.of_ternary(bench_recursive_fib_ternary);
        auto result = e.to_nat(e.apply(fib, e.of_nat(recursive_fib_n)));
        if (result != expected_recursive)
          throw std::runtime_error("fib misbehavior: " + std::to_string(result));
      }, iterations));
}

int main(int argc, char *argv[]) {
  sanity_checks<EagerValueMem>("EagerValueMem");
  sanity_checks<EagerTernary>("EagerTernary");
  sanity_checks<EagerTernaryRef>("EagerTernaryRef");
  sanity_checks<EagerTernaryLen>("EagerTernaryLen");
  sanity_checks<EagerTernaryVM>("EagerTernaryVM");
  sanity_checks<EagerTernaryNil>("EagerTernaryNil");
  sanity_checks<EagerTernaryNil32>("EagerTernaryNil32");
  sanity_checks<EagerTernaryNilVM>("EagerTernaryNilVM");
  sanity_checks<EagerTernaryNilVM32>("EagerTernaryNilVM32");
  sanity_checks<EagerTernaryNilMmap>("EagerTernaryNilMmap");
  sanity_checks<EagerTernaryNilMmap32>("EagerTernaryNilMmap32");
  sanity_checks<EagerTernaryNilMmapVM>("EagerTernaryNilMmapVM");
  sanity_checks<EagerTernaryNilMmapVM32>("EagerTernaryNilMmapVM32");
  sanity_checks<EagerValueMemPeek>("EagerValueMemPeek");
  sanity_checks<EagerTernaryNilMmapPeek>("EagerTernaryNilMmapPeek");
  sanity_checks<EagerTernaryNilMmap32Peek>("EagerTernaryNilMmap32Peek");
  sanity_checks<LazyAppStream>("LazyAppStream");
  sanity_checks<EagerStacks>("EagerStacks");

  bool bench = argc > 1 && std::string(argv[1]) == "--bench";
  if (bench) {
    // Fib arguments tuned so each benchmark takes ~0.1s.
    // Linear fib is capped at 90 to avoid int64_t overflow.
    bench_evaluator<EagerValueMem>("EagerValueMem", 90, 24);
    bench_evaluator<EagerTernary>("EagerTernary", 55, 14);
    bench_evaluator<EagerTernaryLen>("EagerTernaryLen", 55, 14);
    bench_evaluator<EagerTernaryRef>("EagerTernaryRef", 90, 24);
    bench_evaluator<EagerTernaryVM>("EagerTernaryVM", 90, 24);
    bench_evaluator<EagerTernaryNil>("EagerTernaryNil", 90, 24);
    bench_evaluator<EagerTernaryNil32>("EagerTernaryNil32", 90, 24);
    bench_evaluator<EagerTernaryNilVM>("EagerTernaryNilVM", 90, 24);
    bench_evaluator<EagerTernaryNilVM32>("EagerTernaryNilVM32", 90, 24);
    bench_evaluator<EagerTernaryNilMmap>("EagerTernaryNilMmap", 90, 24);
    bench_evaluator<EagerTernaryNilMmap32>("EagerTernaryNilMmap32", 90, 24);
    bench_evaluator<EagerTernaryNilMmapVM>("EagerTernaryNilMmapVM", 90, 24);
    bench_evaluator<EagerTernaryNilMmapVM32>("EagerTernaryNilMmapVM32", 90, 24);
    bench_evaluator<EagerValueMemPeek>("EagerValueMemPeek", 90, 24);
    bench_evaluator<EagerTernaryNilMmapPeek>("EagerTernaryNilMmapPeek", 90, 24);
    bench_evaluator<EagerTernaryNilMmap32Peek>("EagerTernaryNilMmap32Peek", 90, 24);
    bench_evaluator<LazyAppStream>("LazyAppStream", 22, 9);
  }

  std::cout << std::endl << "All tests passed." << std::endl;
  return 0;
}
