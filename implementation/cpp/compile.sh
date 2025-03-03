#/bin/sh

set -euo pipefail

clang++ eager-value-mem.cpp -O3 -std=c++20 -o eager-value-mem.exe
