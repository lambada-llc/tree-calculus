#include "eager-value-mem.hpp"
#include "eager-ternary.hpp"
#include "eager-ternary-ref.hpp"
#include "eager-ternary-len.hpp"
#include "eager-ternary-vm.hpp"
#include "eager-rc.hpp"
#include "lazy-app-stream.hpp"
#include "lazy-rc.hpp"
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
    } else {
      std::cerr << "Unknown argument: " << arg << std::endl;
      return 1;
    }
  }

  if (evaluator == "eager-value-mem") {
    return run<EagerValueMem>();
  } else if (evaluator == "eager-ternary") {
    return run<EagerTernary>();
  } else if (evaluator == "eager-ternary-ref") {
    return run<EagerTernaryRef>();
  } else if (evaluator == "eager-ternary-len") {
    return run<EagerTernaryLen>();
  } else if (evaluator == "eager-ternary-vm") {
    return run<EagerTernaryVM>();
  } else if (evaluator == "eager-rc") {
    return run<EagerRC>();
  } else if (evaluator == "lazy-app-stream") {
    return run<LazyAppStream>();
  } else if (evaluator == "lazy-rc") {
    return run<LazyRC>();
  } else {
    std::cerr << "Unknown evaluator: " << evaluator << std::endl;
    return 1;
  }
}
