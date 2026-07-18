#!/bin/bash
# Test + scaling benchmark for the parallel-frontier reducer on a "parallel
# equal" workload.
#
# benchmark/parallel-and.ternary is a balanced AND of WIDTH=16 independent
# `equal (exp n) (exp n)` computations, each = true, so the whole program
# reduces to a single bit true (ternary "10"). That shape -- many independent
# expensive reductions combined into one bit -- is exactly what a bulk-
# synchronous parallel reducer can exploit.
#
# This first checks correctness (the result must be true), then times the
# reducer across thread counts and reports self-speedup.
#
# Env: N = per-leaf work exponent 2^N (default 14); BEST_OF = timing repeats
#      (default 4); CXX / CXXFLAGS override the compiler used to build the binary.
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
CPP_DIR="$BENCH_DIR/../implementation/cpp"
N=${N:-14}
BEST_OF=${BEST_OF:-4}
CXX=${CXX:-clang++}
CXXFLAGS=${CXXFLAGS:--O3 -std=c++23 -stdlib=libc++ -fopenmp}

BIN="$CPP_DIR/parallel-frontier.exe"
[[ -x "$BIN" ]] || {
  echo "building parallel-frontier.exe ($CXX)..."
  (cd "$CPP_DIR" && $CXX $CXXFLAGS parallel-frontier.cpp -o parallel-frontier.exe)
}

encode_nat() { # nat -> ternary Church-ish numeral the program folds over
  local n=$1 r="0" i bits=()
  while [ "$n" -gt 0 ]; do bits+=( $((n & 1)) ); n=$((n >> 1)); done
  for (( i=${#bits[@]}-1; i>=0; i-- )); do
    [ "${bits[$i]}" -eq 1 ] && r="210${r}" || r="20${r}"
  done
  printf '%s' "$r"
}

PROG="$(cat "$BENCH_DIR/parallel-and.ternary")"
INPUT="$(encode_nat "$N")"
infile="$(mktemp)"; trap 'rm -f "$infile"' EXIT
printf '%s\n%s\n' "$PROG" "$INPUT" > "$infile"

cores=$(nproc)

# --- correctness: the parallel equal must reduce to true ("10") ---
got="$(OMP_NUM_THREADS="$cores" "$BIN" < "$infile")"
if [[ "$got" != "10" ]]; then echo "FAIL: expected 10 (true), got '$got'"; exit 1; fi
echo "correctness: PASS (parallel equal = true)"
echo

timeit() { local s e; s=$(date +%s.%N); "$@" <"$infile" >/dev/null 2>&1; e=$(date +%s.%N); awk "BEGIN{printf \"%.3f\", $e-$s}"; }
best()   { local m=99 i t; for ((i=0;i<BEST_OF;i++)); do t=$(timeit "$@"); awk "BEGIN{exit !($t<$m)}" && m=$t; done; echo "$m"; }

echo "parallel-frontier  (WIDTH=16 parallel equal, n=$N, best of $BEST_OF, $cores cores)"
t1=""
for th in $(seq 1 "$cores"); do
  t=$(OMP_NUM_THREADS="$th" best "$BIN")
  [[ -z "$t1" ]] && t1="$t"
  sp=$(awk "BEGIN{printf \"%.2fx\", $t1/$t}")
  printf "  t=%-2s  %ss   self-speedup %s\n" "$th" "$t" "$sp"
done
