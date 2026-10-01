#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"

# Whatever compiler the host calls c++, with the flags the runtime's own
# on-demand build of runner.cpp uses — a name only one toolchain answers to, or
# a newer standard than the code needs, is a dependency this does not need.
#
# Only reduce_canonicalize: the runner is built by the runtime that drives it
# (implementation/typescript/src/runner/native.mts), which is the build
# test-runner.sh exercises.
"${CXX:-c++}" "$DIR/reduce_canonicalize.cpp" -O3 -std=c++17 -o "$DIR/reduce_canonicalize.exe"
