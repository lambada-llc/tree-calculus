#!/bin/bash

set -euo pipefail

COMMON_FLAGS=(-O3 -std=c++23 -stdlib=libc++)

clang++ main.cpp "${COMMON_FLAGS[@]}" -o main.exe
clang++ test.cpp "${COMMON_FLAGS[@]}" -o test.exe

# Parallel bulk-synchronous frontier reducer (needs OpenMP; skip quietly if absent).
clang++ parallel-frontier.cpp "${COMMON_FLAGS[@]}" -fopenmp -o parallel-frontier.exe 2>/dev/null \
  || echo "note: skipped parallel-frontier (OpenMP not available)" >&2