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
#include "lazy-app-stream.hpp"
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
  } else if (evaluator == "eager-ternary-nil") {
    return run<EagerTernaryNil>();
  } else if (evaluator == "eager-ternary-nil-32") {
    return run<EagerTernaryNil32>();
  } else if (evaluator == "eager-ternary-nil-vm") {
    return run<EagerTernaryNilVM>();
  } else if (evaluator == "eager-ternary-nil-vm-32") {
    return run<EagerTernaryNilVM32>();
  } else if (evaluator == "eager-ternary-nil-mmap") {
    return run<EagerTernaryNilMmap>();
  } else if (evaluator == "eager-ternary-nil-mmap-32") {
    return run<EagerTernaryNilMmap32>();
  } else if (evaluator == "eager-ternary-nil-mmap-vm") {
    return run<EagerTernaryNilMmapVM>();
  } else if (evaluator == "eager-ternary-nil-mmap-vm-32") {
    return run<EagerTernaryNilMmapVM32>();
  } else if (evaluator == "eager-value-mem-peek") {
    return run<EagerValueMemPeek>();
  } else if (evaluator == "eager-ternary-nil-mmap-peek") {
    return run<EagerTernaryNilMmapPeek>();
  } else if (evaluator == "eager-ternary-nil-mmap-32-peek") {
    return run<EagerTernaryNilMmap32Peek>();
  } else if (evaluator == "lazy-app-stream") {
    return run<LazyAppStream>();
  } else {
    std::cerr << "Unknown evaluator: " << evaluator << std::endl;
    return 1;
  }
}
