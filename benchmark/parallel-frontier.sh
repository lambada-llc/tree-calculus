#!/bin/bash
# Scaling benchmark for the parallel-frontier reducer, emitted in the same row
# format as run.sh so it can be appended to a benchmark log.
#
# Workload: benchmark/parallel-equal.ternary is `equal (exp n) (exp n)` -- it
# builds two 2^n trees and compares them, reducing to a single bit true (ternary
# "10"). Comparing two big trees is a single realistic computation whose subtree
# comparisons are independent, so it is inherently parallel; the output is one
# bit, so nothing large is printed. Each row also asserts that result.
#
# One row per parallelism degree (OMP_NUM_THREADS) in DEGREES; degrees above the
# core count oversubscribe on purpose (the log header records the machine).
#
# Env: N = tree-size exponent 2^N (default 12); BENCH_N = repeats, best wins
#      (default 5); DEGREES = thread counts (default "1 2 4 8 16"); CXX/CXXFLAGS
#      override the compiler.
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
CPP_DIR="$BENCH_DIR/../implementation/cpp"
N=${N:-12}
BENCH_N=${BENCH_N:-5}
DEGREES=${DEGREES:-"1 2 4 8 16"}
CXX=${CXX:-clang++}
CXXFLAGS=${CXXFLAGS:--O3 -std=c++23 -stdlib=libc++ -fopenmp}

BIN="$CPP_DIR/parallel-frontier.exe"
[[ -x "$BIN" ]] || (cd "$CPP_DIR" && $CXX $CXXFLAGS parallel-frontier.cpp -o parallel-frontier.exe)

encode_nat() { # nat -> ternary numeral the program folds over
  local n=$1 r="0" i bits=()
  while [ "$n" -gt 0 ]; do bits+=( $((n & 1)) ); n=$((n >> 1)); done
  for (( i=${#bits[@]}-1; i>=0; i-- )); do
    [ "${bits[$i]}" -eq 1 ] && r="210${r}" || r="20${r}"
  done
  printf '%s' "$r"
}

infile="$(mktemp)"; trap 'rm -f "$infile"' EXIT
printf '%s\n%s\n' "$(cat "$BENCH_DIR/parallel-equal.ternary")" "$(encode_nat "$N")" > "$infile"

echo "parallel-equal (n=$N)"
for th in $DEGREES; do
  best="" ok=true
  for (( i=0; i<BENCH_N; i++ )); do
    s=$(date +%s.%N); out=$(OMP_NUM_THREADS="$th" "$BIN" < "$infile"); e=$(date +%s.%N)
    [[ "$out" == "10" ]] || ok=false
    t=$(awk "BEGIN{printf \"%.3f\", $e-$s}")
    [[ -z "$best" ]] && best="$t" || best=$(awk "BEGIN{print ($t<$best)?$t:$best}")
  done
  if $ok; then printf "  %-35s PASS  %ss\n" "C++ parallel-frontier (t=$th)" "$best"
  else         printf "  %-35s FAIL  (not true)\n" "C++ parallel-frontier (t=$th)"; fi
done
