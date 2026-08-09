#pragma once

#include "eager-value-mem.hpp"
#include "eager-value-heap.hpp"
#include "eager-ternary.hpp"
#include "eager-ternary-ref.hpp"
#include "eager-ternary-len.hpp"
#include "eager-ternary-vm.hpp"
#include "eager-ternary-nil.hpp"
#include "eager-ternary-nil-32.hpp"
#include "eager-ternary-nil-vm.hpp"
#include "eager-ternary-nil-vm-32.hpp"
#include "eager-ternary-nil-vm-32-rc.hpp"
#include "eager-ternary-nil-mmap.hpp"
#include "eager-ternary-nil-mmap-32.hpp"
#include "eager-ternary-nil-mmap-vm.hpp"
#include "eager-ternary-nil-mmap-vm-32.hpp"
#include "eager-value-mem-peek.hpp"
#include "eager-value-heap-peek.hpp"
#include "eager-ternary-nil-mmap-peek.hpp"
#include "eager-ternary-nil-mmap-32-peek.hpp"
#include "eager-graph-nil-mmap-32.hpp"
#include "lazy-graph-nil-mmap-32.hpp"
#include "lazy-app-stream.hpp"

// EVALUATORS(X) expands X once per evaluator:
//
//   X(Class, "cli-name", in_suite, linear_fib_n, recursive_fib_n)
//
// Everything that needs the roster derives it from here — main.cpp's dispatch
// and --list, test.cpp's checks and benchmarks — so adding an evaluator is one
// line plus its header above, and no list can fall behind another.
//
// in_suite is whether benchmark/run-one.sh times it, which main.cpp's --list
// reports. The three marked 0 are excluded because they are far slower than
// that suite's per-case timeout: the fib arguments below are tuned so each case
// takes about 0.1s, and needing fib(14) where the others take fib(24) means
// roughly two orders of magnitude more work for the same wall time.
//
// linear_fib_n and recursive_fib_n are test.cpp's --bench arguments, tuned the
// same way. Linear fib is capped at 90 to avoid int64_t overflow.
#define EVALUATORS(X)                                                    \
  X(EagerValueMem,             "eager-value-mem",              1, 90, 24) \
  X(EagerValueHeap,            "eager-value-heap",             1, 90, 19) \
  X(EagerTernary,              "eager-ternary",                0, 55, 14) \
  X(EagerTernaryRef,           "eager-ternary-ref",            1, 90, 24) \
  X(EagerTernaryLen,           "eager-ternary-len",            0, 55, 14) \
  X(EagerTernaryVM,            "eager-ternary-vm",             1, 90, 24) \
  X(EagerTernaryNil,           "eager-ternary-nil",            1, 90, 24) \
  X(EagerTernaryNil32,         "eager-ternary-nil-32",         1, 90, 24) \
  X(EagerTernaryNilVM,         "eager-ternary-nil-vm",         1, 90, 24) \
  X(EagerTernaryNilVM32,       "eager-ternary-nil-vm-32",      1, 90, 24) \
  X(EagerTernaryNilVM32RC,     "eager-ternary-nil-vm-32-rc",   1, 90, 24) \
  X(EagerTernaryNilMmap,       "eager-ternary-nil-mmap",       1, 90, 24) \
  X(EagerTernaryNilMmap32,     "eager-ternary-nil-mmap-32",    1, 90, 24) \
  X(EagerTernaryNilMmapVM,     "eager-ternary-nil-mmap-vm",    1, 90, 24) \
  X(EagerTernaryNilMmapVM32,   "eager-ternary-nil-mmap-vm-32", 1, 90, 24) \
  X(EagerValueMemPeek,         "eager-value-mem-peek",         1, 90, 24) \
  X(EagerValueHeapPeek,        "eager-value-heap-peek",        1, 90, 19) \
  X(EagerTernaryNilMmapPeek,   "eager-ternary-nil-mmap-peek",  1, 90, 24) \
  X(EagerTernaryNilMmap32Peek, "eager-ternary-nil-mmap-32-peek", 1, 90, 24) \
  X(EagerGraphNilMmap32,       "eager-graph-nil-mmap-32",      1, 90, 24) \
  X(LazyGraphNilMmap32,        "lazy-graph-nil-mmap-32",       1, 90, 24) \
  X(LazyAppStream,             "lazy-app-stream",              0, 22,  9)
