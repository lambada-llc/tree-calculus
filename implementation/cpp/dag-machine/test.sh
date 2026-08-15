#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_JS="$DIR/../../typescript/main.js"

# Compile
"$DIR/compile.sh"

# The runner has an end-to-end suite of its own: native evaluators and the
# reduction cache against the pure-Node evaluator as oracle.
"$DIR/test-runner.sh"

pass=0
fail=0

for dag in "$DIR"/*.dag; do
  [[ "$dag" == *.out.dag ]] && continue

  name=$(basename "$dag")

  # Reducing and hash-consing a DAG must not change the tree it denotes, so the
  # unreduced input read by the pure-Node evaluator is the oracle.
  out="${dag%.dag}.out.dag"
  stats="${dag%.dag}.stats"
  # --stats-per-symbol so the flag a build passes (`dag-bundle-reduce.sh
  # --stats=`) is exercised too; the table itself is written for reading, not
  # compared against anything.
  "$DIR/reduce_canonicalize.exe" --stats-per-symbol < "$dag" > "$out" 2> "$stats.csv"
  column -s, -t < "$stats.csv" > "$stats"
  rm -f "$stats.csv"

  expected=$(node "$MAIN_JS" --dag --file "$dag" --ternary)
  actual=$(node "$MAIN_JS" --dag --file "$out" --ternary)

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
