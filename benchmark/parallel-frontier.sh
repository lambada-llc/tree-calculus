#!/bin/bash
# Scaling benchmark for the parallel-frontier reducer, emitted in the same row
# format as run.sh so it can be appended to a benchmark log.
#
# Workload: benchmark/parallel-and.ternary is a balanced AND of WIDTH=16
# independent `equal (exp n) (exp n)` computations, each = true, so the whole
# program reduces to a single bit true (ternary "10"). Many independent
# expensive reductions combined into one bit is the shape a bulk-synchronous
# parallel reducer can exploit. Each row also asserts that result (correctness).
#
# One row per parallelism degree (OMP_NUM_THREADS) in DEGREES; degrees above the
# core count oversubscribe on purpose (the log header records the machine).
#
# Env: N = per-leaf work exponent 2^N (default 9); BENCH_N = repeats, best wins
#      (default 5); DEGREES = thread counts (default "1 2 4 8 16"); CXX/CXXFLAGS
#      override the compiler.
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
CPP_DIR="$BENCH_DIR/../implementation/cpp"
N=${N:-9}
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
printf '%s\n%s\n' "$(cat "$BENCH_DIR/parallel-and.ternary")" "$(encode_nat "$N")" > "$infile"

echo "parallel-and (WIDTH=16, n=$N)"
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
