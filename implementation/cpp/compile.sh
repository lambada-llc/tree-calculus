#!/bin/bash

set -euo pipefail

COMMON_FLAGS=(-O3 -std=c++23 -stdlib=libc++)

clang++ main.cpp "${COMMON_FLAGS[@]}" -o main.exe
clang++ test.cpp "${COMMON_FLAGS[@]}" -o test.exe

# Experimental parallel (fork-join) reducer — needs OpenMP. Best-effort: skip
# quietly if the OpenMP runtime isn't available. See benchmark/BREAKING-RECORDS.md.
clang++ parallel-peek.cpp   "${COMMON_FLAGS[@]}" -fopenmp -o parallel-peek   2>/dev/null || echo "note: skipped parallel-peek (OpenMP not available)" >&2
clang++ parallel-packed.cpp "${COMMON_FLAGS[@]}" -fopenmp -o parallel-packed 2>/dev/null \
  || echo "note: skipped parallel-packed (OpenMP not available)" >&2