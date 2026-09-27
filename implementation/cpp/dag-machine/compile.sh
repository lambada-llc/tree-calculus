#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"

# Whatever compiler the host calls c++, as the runtime's own on-demand build of
# runner.cpp does — a name only one toolchain answers to is a dependency this
# does not need.
CXX="${CXX:-c++}"
"$CXX" "$DIR/reduce_canonicalize.cpp" -O3 -std=c++23 -o "$DIR/reduce_canonicalize.exe"
"$CXX" "$DIR/runner.cpp" -O3 -std=c++23 -pthread -o "$DIR/runner.exe"
