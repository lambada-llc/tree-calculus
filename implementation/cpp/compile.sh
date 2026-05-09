#!/bin/bash

set -euo pipefail

COMMON_FLAGS=(-O3 -std=c++23 -stdlib=libc++)

clang++ main.cpp "${COMMON_FLAGS[@]}" -o main.exe
clang++ test.cpp "${COMMON_FLAGS[@]}" -o test.exe