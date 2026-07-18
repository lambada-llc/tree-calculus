#!/bin/bash

set -euo pipefail

COMMON_FLAGS=(-O3 -std=c++23 -stdlib=libc++)

clang++ main.cpp "${COMMON_FLAGS[@]}" -o main.exe
clang++ test.cpp "${COMMON_FLAGS[@]}" -o test.exe

# Experimental reducers. See benchmark/BREAKING-RECORDS.md.
# frontier-reduce: bulk-synchronous graph reducer that measures span/work (no OpenMP).
clang++ frontier-reduce.cpp "${COMMON_FLAGS[@]}" -o frontier-reduce
# parallel-{peek,packed}: fork-join reducers; need OpenMP, skip quietly if absent.
clang++ parallel-frontier.cpp "${COMMON_FLAGS[@]}" -fopenmp -o parallel-frontier 2>/dev/null || echo "note: skipped parallel-frontier (OpenMP not available)" >&2
clang++ parallel-peek.cpp   "${COMMON_FLAGS[@]}" -fopenmp -o parallel-peek   2>/dev/null || echo "note: skipped parallel-peek (OpenMP not available)" >&2
clang++ parallel-packed.cpp "${COMMON_FLAGS[@]}" -fopenmp -o parallel-packed 2>/dev/null \
  || echo "note: skipped parallel-packed (OpenMP not available)" >&2