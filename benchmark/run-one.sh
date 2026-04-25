#!/bin/bash
# Benchmark all implementations on a given set of ternary-encoded trees.
# Usage: ./run-one.sh <expected_output> <ternary1> <ternary2> [...]
#
# Trees are left-fold applied starting from the identity tree.
# Each implementation's output is asserted against expected_output.
# Set BENCH_N to control number of runs (default: 5).

set -euo pipefail
ulimit -s unlimited 2>/dev/null || true  # prevent stack overflow in some recursive evaluators

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <expected_output> <ternary1> <ternary2> [...]" >&2
  exit 1
fi

EXPECTED="$1"; shift
ARGS=("$@")

# Bootstrap versioned tool managers so the script works outside login shells
[ -s "$HOME/.nvm/nvm.sh" ] && \. "$HOME/.nvm/nvm.sh"
[ -s "$HOME/.pyenv/bin/pyenv" ] && export PATH="$HOME/.pyenv/bin:$HOME/.pyenv/shims:$PATH"

REPO_ROOT="$(dirname "$0")/.."
TIMEFORMAT='%3Rs'
BENCH_N=${BENCH_N:-5}
BENCH_TIMEOUT=${BENCH_TIMEOUT:-2}
FAILURES=0
TIMEFILE=$(mktemp)
trap 'rm -f "$TIMEFILE"' EXIT

# Detect a timeout command (GNU coreutils; 'gtimeout' on macOS via Homebrew)
if command -v timeout &>/dev/null; then
  TIMEOUT=( timeout "$BENCH_TIMEOUT" )
elif command -v gtimeout &>/dev/null; then
  TIMEOUT=( gtimeout "$BENCH_TIMEOUT" )
else
  TIMEOUT=()
fi

# Join arguments with newlines for stdin-based implementations
stdin_args() { printf '%s\n' "$@"; }

# Run CMD BENCH_N times, report best time, assert output each run.
# Usage: bench LABEL [--stdin] CMD [ARGS...]
#   --stdin  pipe stdin_args to CMD instead of running it directly
# Stops early on non-zero exit (crash or timeout); reports "FAIL <exit code>".
bench() {
  local label="$1"; shift
  local pipe_stdin=false
  if [[ "${1-}" == "--stdin" ]]; then pipe_stdin=true; shift; fi

  local i t output times=() best exit_code fail_exit=0 fail_output="" any_fail=false timed_out=false

  for (( i=0; i<BENCH_N; i++ )); do
    exit_code=0
    if $pipe_stdin; then
      if [[ ${#TIMEOUT[@]} -gt 0 ]]; then
        { time output=$(stdin_args "${ARGS[@]}" | "${TIMEOUT[@]}" "$@" 2>/dev/null); } 2>"$TIMEFILE" || exit_code=$?
      else
        { time output=$(stdin_args "${ARGS[@]}" | "$@" 2>/dev/null); } 2>"$TIMEFILE" || exit_code=$?
      fi
    else
      if [[ ${#TIMEOUT[@]} -gt 0 ]]; then
        { time output=$("${TIMEOUT[@]}" "$@" 2>/dev/null); } 2>"$TIMEFILE" || exit_code=$?
      else
        { time output=$("$@" 2>/dev/null); } 2>"$TIMEFILE" || exit_code=$?
      fi
    fi
    t=$(<"$TIMEFILE")
    times+=("${t%s}")  # strip trailing 's' for numeric sorting
    if [[ $exit_code -ne 0 && $exit_code -ne 124 ]]; then
      any_fail=true
      fail_exit=$exit_code
      break  # deterministic crash — no point retrying
    elif [[ $exit_code -eq 124 ]]; then
      any_fail=true
      timed_out=true
    elif [[ "$output" != "$EXPECTED" ]]; then
      any_fail=true
      fail_output="$output"
    fi
  done

  best=$(printf '%s\n' "${times[@]}" | sort -g | head -1)

  if ! $any_fail; then
    printf "  %-35s PASS  %ss\n" "$label" "$best"
  elif [[ $fail_exit -ne 0 ]]; then
    printf "  %-35s FAIL  exit %d\n" "$label" "$fail_exit"
    FAILURES=$((FAILURES + 1))
  elif $timed_out; then
    printf "  %-35s FAIL  exit 124  (timeout)\n" "$label"
    FAILURES=$((FAILURES + 1))
  else
    printf "  %-35s FAIL  %ss\n" "$label" "$best"
    printf "    expected: %s\n" "$EXPECTED"
    printf "    got:      %s\n" "$fail_output"
    FAILURES=$((FAILURES + 1))
  fi
}

# Returns 0 if node >= min_major is available, 1 otherwise
require_node() {
  local min_major=$1 ctx=$2
  if ! command -v node &>/dev/null; then
    printf "  %-35s SKIP  node not found\n" "$ctx"
    return 1
  fi
  local major
  major=$(node --version | tr -d 'v' | cut -d. -f1)
  if [ "$major" -lt "$min_major" ]; then
    printf "  %-35s SKIP  node >= v%s required\n" "$ctx" "$min_major"
    return 1
  fi
}

# --- JavaScript (Node.js, Reference Implementation) ---
JS_BIN="$REPO_ROOT/bin/main.js"
if require_node 18 "JavaScript (reference)"; then
  bench "JavaScript (reference)" node "$JS_BIN" -ternary "${ARGS[@]}"
fi

# --- C++ ---
CPP_BIN="$REPO_ROOT/implementation/cpp/main.exe"
if [[ -x "$CPP_BIN" ]]; then
  for eval in eager-value-mem eager-ternary-ref eager-ternary-vm; do
    bench "C++ $eval" --stdin "$CPP_BIN" --evaluator "$eval"
  done
else
  printf "  %-35s SKIP  not built\n" "C++"
fi

# --- Python ---
PYTHON_IMPL="$REPO_ROOT/implementation/python/tree-calculus.py"
if [[ -f "$PYTHON_IMPL" ]]; then
  if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
    bench "Python" --stdin python3 "$PYTHON_IMPL"
  else
    printf "  %-35s SKIP  Python >= 3.10 required\n" "Python"
  fi
else
  printf "  %-35s SKIP  not found\n" "Python"
fi

# --- WASM ---
WASM_MAIN="$REPO_ROOT/implementation/wasm/eager-value/main.mjs"
if [[ -f "$WASM_MAIN" ]] && require_node 21 "WASM"; then
  bench "WASM" --stdin node "$WASM_MAIN"
elif [[ ! -f "$WASM_MAIN" ]]; then
  printf "  %-35s SKIP  not found\n" "WASM"
fi

# --- ASM (x86_64 only) ---
ASM_DIR="$REPO_ROOT/implementation/asm"
if [[ -f "$ASM_DIR/test.mjs" ]]; then
  if [[ "$(uname -m)" == "x86_64" ]]; then
    for variant in x64 x64-jay x64-noid x64-minbin x64-minbin-deep; do
      BIN_PATH="$ASM_DIR/bin/$variant"
      if [[ -x "$BIN_PATH" ]]; then
        bench "ASM $variant" --stdin "$BIN_PATH"
      else
        printf "  %-35s SKIP  not built\n" "ASM $variant"
      fi
    done
  else
    printf "  %-35s SKIP  x86_64 required\n" "ASM"
  fi
else
  printf "  %-35s SKIP  not found\n" "ASM"
fi

exit "$FAILURES"
