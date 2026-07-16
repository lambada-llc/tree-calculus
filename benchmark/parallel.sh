#!/bin/bash
# Parallel-scaling benchmark. Unlike the single-thread suite (run.sh), this
# workload is built to expose parallelism: many INDEPENDENT expensive
# computations combined into a single bit (see BREAKING-RECORDS.md §4). It times
# the fork-join reducer (implementation/cpp/parallel-packed.cpp) across thread
# counts and reports the speedup.
#
# The program is `benchmark/parallel-and.ternary` (WIDTH=16 independent leaves,
# each `equal (exp n) (exp n)` = true); regenerate it with
#   cd implementation/typescript && npm i && \
#   node_modules/.bin/esbuild src/gen-parbench.mts --bundle --platform=node \
#     --format=esm --outfile=/tmp/g.mjs && WIDTH=16 node /tmp/g.mjs emit
#
# Env: N = per-leaf work exponent (2^N, default 14); BCUT = fork cutoff (default 6).
set -euo pipefail
BENCH_DIR="$(dirname "$0")"
REPO="$BENCH_DIR/.."
N=${N:-14}
export BCUT=${BCUT:-6}
BEST_OF=${BEST_OF:-3}

encode_nat() {
  local n=$1 result="0" i bits=()
  while [ "$n" -gt 0 ]; do bits+=( $((n & 1)) ); n=$((n >> 1)); done
  for (( i=${#bits[@]}-1; i>=0; i-- )); do
    [ "${bits[$i]}" -eq 1 ] && result="210${result}" || result="20${result}"
  done
  printf '%s' "$result"
}

PROG="$(cat "$BENCH_DIR/parallel-and.ternary")"
INPUT="$(encode_nat "$N")"
PAR="$REPO/implementation/cpp/parallel-packed"
CPP="$REPO/implementation/cpp/main.exe"

if [[ ! -x "$PAR" ]]; then
  echo "building parallel-packed..."
  (cd "$REPO/implementation/cpp" && clang++ parallel-packed.cpp -O3 -std=c++23 -stdlib=libc++ -fopenmp -o parallel-packed)
fi

ulimit -s unlimited 2>/dev/null || true
infile="$(mktemp)"; trap 'rm -f "$infile"' EXIT
printf '%s\n%s\n' "$PROG" "$INPUT" > "$infile"

timeit() { local s e; s=$(date +%s.%N); "$@" <"$infile" >/dev/null 2>&1; e=$(date +%s.%N); awk "BEGIN{printf \"%.3f\", $e-$s}"; }
best() { local m=99 i t; for ((i=0;i<BEST_OF;i++)); do t=$(timeit "$@"); awk "BEGIN{exit !($t<$m)}" && m=$t; done; echo "$m"; }

# correctness
got="$(OMP_NUM_THREADS="$(nproc)" "$PAR" <"$infile" 2>/dev/null)"
[[ "$got" == "10" ]] && ok="OK" || ok="FAIL (got $got)"

echo "parallel-and benchmark  (WIDTH=16, n=$N, BCUT=$BCUT, best of $BEST_OF)  correctness: $ok"
echo
cores=$(nproc)
t1=""
printf "  %-22s %s\n" "threads" "time    speedup"
for th in $(seq 1 "$cores"); do
  t=$(OMP_NUM_THREADS="$th" best "$PAR")
  [[ -z "$t1" ]] && t1="$t"
  sp=$(awk "BEGIN{printf \"%.2fx\", $t1/$t}")
  printf "  parallel-packed t=%-4s   %ss   %s\n" "$th" "$t" "$sp"
done
echo
printf "  %-22s %ss  (single-thread reference, packed representation)\n" \
  "champion (nil-mmap-32-peek)" "$(best "$CPP" --evaluator eager-ternary-nil-mmap-32-peek)"
