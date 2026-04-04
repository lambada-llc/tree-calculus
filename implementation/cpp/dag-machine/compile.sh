#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"

clang++ "$DIR/reduce.cpp" -O3 -std=c++23 -o "$DIR/reduce.exe"
