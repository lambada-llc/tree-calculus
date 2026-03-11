#include "eager-value-mem.hpp"
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

template <typename Impl>
void sanity_checks(std::string name) {
  std::cout << "Testing " << name << "..." << std::endl;
  Evaluator<Impl> e;
  test_basic_reduction_rules(e);
  std::cout << "  All reduction rules passed." << std::endl;
  std::cout << "    Stats: " << e.stats() << std::endl;
  auto bench_recursive_fib = e.of_ternary(bench_recursive_fib_ternary);
  auto bench_linear_fib = e.of_ternary(bench_linear_fib_ternary);
  if (e.to_nat(e.apply(bench_recursive_fib, e.of_nat(7))) != 21)
    throw std::runtime_error("fib misbehavior");
  if (e.to_nat(e.apply(bench_linear_fib, e.of_nat(7))) != 21)
    throw std::runtime_error("fib misbehavior");
  std::cout << "  Fib behaves as expected." << std::endl;
  std::cout << "    Stats: " << e.stats() << std::endl;
}

int main(int argc, char *argv[]) {
  sanity_checks<EagerValueMem>("EagerValueMem");
  sanity_checks<LazyAppStream>("LazyAppStream");

  bool bench = argc > 1 && std::string(argv[1]) == "--bench";
  if (bench) {
    print_statistics(
      "[EagerValueMem] Setup, should be negligibly fast",
      repeat_measure_sec(
        []() { 
          Evaluator<EagerValueMem> e;
          e.of_ternary(bench_recursive_fib_ternary);
          e.of_ternary(bench_linear_fib_ternary);
        }));
    print_statistics(
      "[EagerValueMem] Linear fib(90)",
      repeat_measure_sec(
        []() {
          Evaluator<EagerValueMem> e;
          auto fib = e.of_ternary(bench_linear_fib_ternary);
          auto result = e.to_nat(e.apply(fib, e.of_nat(90)));
          if (result != 4660046610375530309)
            throw std::runtime_error("fib misbehavior: " + std::to_string(result));
        }));
    print_statistics(
      "[EagerValueMem] Recursive fib(28)",
      repeat_measure_sec(
        []() {
          Evaluator<EagerValueMem> e;
          auto fib = e.of_ternary(bench_recursive_fib_ternary);
          auto result = e.to_nat(e.apply(fib, e.of_nat(28)));
          if (result != 514229)
            throw std::runtime_error("fib misbehavior: " + std::to_string(result));
        }));
  }

  std::cout << std::endl << "All tests passed." << std::endl;
  return 0;
}
