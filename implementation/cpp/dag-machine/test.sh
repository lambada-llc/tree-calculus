#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_JS="$DIR/../../typescript/main.js"

# Compile
"$DIR/compile.sh"

pass=0
fail=0

for dag in "$DIR"/*.dag; do
  [[ "$dag" == *.out.dag ]] && continue
  [[ "$dag" == *.canon.dag ]] && continue
  [[ "$dag" == *.combined.dag ]] && continue

  name=$(basename "$dag")

  # Test reduce
  out="${dag%.dag}.out.dag"
  "$DIR/reduce.exe" < "$dag" > "$out"

  expected=$(node "$MAIN_JS" --dag --file "$dag" --ternary)
  actual=$(node "$MAIN_JS" --dag --file "$out" --ternary)

  if [ "$expected" = "$actual" ]; then
    echo "PASS reduce $name (ternary: $expected)"
    ((pass++)) || true
  else
    echo "FAIL reduce $name: expected $expected, got $actual"
    ((fail++)) || true
  fi

  # Test canonicalize
  canon="${dag%.dag}.canon.dag"
  "$DIR/canonicalize.exe" < "$dag" > "$canon"

  actual=$(node "$MAIN_JS" --dag --file "$canon" --ternary)

  if [ "$expected" = "$actual" ]; then
    echo "PASS canonicalize $name (ternary: $expected)"
    ((pass++)) || true
  else
    echo "FAIL canonicalize $name: expected $expected, got $actual"
    ((fail++)) || true
  fi

  # Test reduce_canonicalize (also overwrites per-symbol stats expect file)
  combined="${dag%.dag}.combined.dag"
  stats="${dag%.dag}.stats"
  "$DIR/reduce_canonicalize.exe" --stats-per-symbol < "$dag" > "$combined" 2> "$stats.csv"
  column -s, -t < "$stats.csv" > "$stats"
  rm -f "$stats.csv"

  actual=$(node "$MAIN_JS" --dag --file "$combined" --ternary)

  if [ "$expected" = "$actual" ]; then
    echo "PASS reduce_canonicalize $name (ternary: $expected)"
    ((pass++)) || true
  else
    echo "FAIL reduce_canonicalize $name: expected $expected, got $actual"
    ((fail++)) || true
  fi
done

echo ""
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
