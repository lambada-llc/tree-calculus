#include "evaluators.hpp"
#include "evaluator.hpp"
#include <iostream>

template <typename Impl>
int run() {
  Evaluator<Impl> e;
  auto result = e.of_ternary("21100"); // identity
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    auto tree = e.of_ternary(line);
    result = e.apply(result, tree);
  }
  std::cout << e.to_ternary(result) << std::endl;
  return 0;
}

int main(int argc, char *argv[]) {
  std::string evaluator = "eager-value-mem";

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--evaluator" && i + 1 < argc) {
      evaluator = argv[++i];
    } else if (arg == "--list") {
      // The roster benchmark/run-one.sh times, so it need not keep its own copy.
#define PRINT_IF_IN_SUITE(Class, Name, InSuite, L, R) \
      if (InSuite) std::cout << Name << std::endl;
      EVALUATORS(PRINT_IF_IN_SUITE)
#undef PRINT_IF_IN_SUITE
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << std::endl;
      return 1;
    }
  }

#define DISPATCH(Class, Name, InSuite, L, R) \
  if (evaluator == Name) return run<Class>();
  EVALUATORS(DISPATCH)
#undef DISPATCH

  std::cerr << "Unknown evaluator: " << evaluator << std::endl;
  return 1;
}
