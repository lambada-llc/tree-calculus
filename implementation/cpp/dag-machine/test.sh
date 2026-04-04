#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_JS="$DIR/../../typescript/main.js"

# Compile
clang++ "$DIR/reduce.cpp" -O3 -std=c++23 -o "$DIR/reduce.exe"

pass=0
fail=0

for dag in "$DIR"/*.dag; do
  [[ "$dag" == *.out.dag ]] && continue

  name=$(basename "$dag")
  out="${dag%.dag}.out.dag"

  "$DIR/reduce.exe" < "$dag" > "$out"

  expected=$(node "$MAIN_JS" --dag --file "$dag" --ternary)
  actual=$(node "$MAIN_JS" --dag --file "$out" --ternary)

  if [ "$expected" = "$actual" ]; then
    echo "PASS $name (ternary: $expected)"
    ((pass++)) || true
  else
    echo "FAIL $name: expected $expected, got $actual"
    ((fail++)) || true
  fi
done

echo ""
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
