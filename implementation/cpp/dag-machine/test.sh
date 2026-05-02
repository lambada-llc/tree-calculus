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
  stats_expected="${dag%.dag}.combined.stats"
  stats_actual="${stats_expected}.actual"

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

  # Test reduce_canonicalize (also captures per-symbol stats for expect-test)
  combined="${dag%.dag}.combined.dag"
  "$DIR/reduce_canonicalize.exe" --stats-per-symbol < "$dag" > "$combined" 2> "$stats_actual"

  actual=$(node "$MAIN_JS" --dag --file "$combined" --ternary)

  if [ "$expected" = "$actual" ]; then
    echo "PASS reduce_canonicalize $name (ternary: $expected)"
    ((pass++)) || true
  else
    echo "FAIL reduce_canonicalize $name: expected $expected, got $actual"
    ((fail++)) || true
  fi

  # Expect-test: per-symbol stats output must match the committed file
  if [ -f "$stats_expected" ] && diff -q "$stats_expected" "$stats_actual" > /dev/null; then
    echo "PASS stats $name"
    ((pass++)) || true
    rm -f "$stats_actual"
  else
    echo "FAIL stats $name (committed $stats_expected differs from actual $stats_actual)"
    if [ -f "$stats_expected" ]; then
      diff "$stats_expected" "$stats_actual" || true
    else
      echo "  (no committed expected file; created $stats_actual)"
    fi
    ((fail++)) || true
  fi
done

echo ""
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
