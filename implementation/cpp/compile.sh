#!/bin/sh

set -euo pipefail

clang++ main.cpp -O3 -std=c++23 -o main.exe
clang++ test.cpp -O3 -std=c++23 -o test.exe