#!/bin/bash
# Parallel-scaling benchmark. Unlike the single-thread suite (run.sh), this
# workload is built to expose parallelism: many INDEPENDENT expensive
# computations combined into a single bit (see BREAKING-RECORDS.md §4). It times
# the fork-join reducers across thread counts and reports the speedup, and
# compares against the single-thread champion evaluator.
#
#   parallel-peek   = champion backend (nil-mmap-32 + peeking) + fork-join
#   parallel-packed = plain packed backend + fork-join (earlier prototype)
#
# The program is `benchmark/parallel-and.ternary` (WIDTH=16 independent leaves,
# each `equal (exp n) (exp n)` = true); regenerate it with
#   cd implementation/typescript && npm i && \
#   node_modules/.bin/esbuild src/gen-parbench.mts --bundle --platform=node \
#     --format=esm --outfile=/tmp/g.mjs && WIDTH=16 node /tmp/g.mjs emit
#
# Env: N = per-leaf work exponent (2^N, default 14); BCUT = fork cutoff (default 5).
set -euo pipefail
BENCH_DIR="$(dirname "$0")"
REPO="$BENCH_DIR/.."
N=${N:-14}
export BCUT=${BCUT:-5}
BEST_OF=${BEST_OF:-4}

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
CPP_DIR="$REPO/implementation/cpp"
CPP="$CPP_DIR/main.exe"

build_if_missing() { # bin_name source
  [[ -x "$CPP_DIR/$1" ]] || {
    echo "building $1..."
    (cd "$CPP_DIR" && clang++ "$2" -O3 -std=c++23 -stdlib=libc++ -fopenmp -o "$1")
  }
}
build_if_missing parallel-peek parallel-peek.cpp
build_if_missing parallel-packed parallel-packed.cpp

ulimit -s unlimited 2>/dev/null || true
infile="$(mktemp)"; trap 'rm -f "$infile"' EXIT
printf '%s\n%s\n' "$PROG" "$INPUT" > "$infile"

timeit() { local s e; s=$(date +%s.%N); "$@" <"$infile" >/dev/null 2>&1; e=$(date +%s.%N); awk "BEGIN{printf \"%.3f\", $e-$s}"; }
best() { local m=99 i t; for ((i=0;i<BEST_OF;i++)); do t=$(timeit "$@"); awk "BEGIN{exit !($t<$m)}" && m=$t; done; echo "$m"; }

cores=$(nproc)
got="$(OMP_NUM_THREADS="$cores" "$CPP_DIR/parallel-peek" <"$infile" 2>/dev/null)"
[[ "$got" == "10" ]] && ok="OK" || ok="FAIL (got $got)"

# single-thread champion reference
champ="$(best "$CPP" --evaluator eager-ternary-nil-mmap-32-peek)"

echo "parallel-and benchmark  (WIDTH=16, n=$N, BCUT=$BCUT, best of $BEST_OF)  correctness: $ok"
echo
printf "  champion nil-mmap-32-peek (1 thread, no parallelism): %ss\n" "$champ"
echo
report() { # label bin
  local t1="" th t sp vs
  printf "  %s\n" "$1"
  for th in $(seq 1 "$cores"); do
    t=$(OMP_NUM_THREADS="$th" best "$CPP_DIR/$2")
    [[ -z "$t1" ]] && t1="$t"
    sp=$(awk "BEGIN{printf \"%.2fx\", $t1/$t}")
    vs=$(awk "BEGIN{printf \"%.2fx\", $champ/$t}")
    printf "    t=%-3s %ss   self %-6s   vs champion %s\n" "$th" "$t" "$sp" "$vs"
  done
  echo
}
report "parallel-peek   (champion backend + fork-join):" parallel-peek
report "parallel-packed (plain backend + fork-join):" parallel-packed
