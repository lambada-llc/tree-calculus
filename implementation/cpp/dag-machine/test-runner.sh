#!/bin/bash
# End-to-end test of runner.cpp behind the TypeScript runtime: the native
# evaluators must answer exactly what the pure-Node evaluator answers, with
# the reduction cache cold, warm, and reusing a previous module version's
# dump (the delta path). The JS evaluator is the oracle throughout.
#
# Needs a C++ compiler (the runtime builds runner.cpp on demand) and Node.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
DAG_JS="$DIR/../../../bin/dag.js"

CACHE=$(mktemp -d "${TMPDIR:-/tmp}/tc-runner-test-XXXXXX")
trap 'rm -rf "$CACHE"' EXIT

# A small module of strongly normalizing bindings, so the eager runner can
# evaluate all of them on load. M2 rebinds `mid` and everything below it —
# the prefix above stays fingerprint-identical, which is what the delta
# loader keys on.
M1="$CACHE/m1.dag"
M2="$CACHE/m2.dag"
cat > "$M1" <<'EOF'
p0 △ △
p1 p0 △
p2 △ p1
p3 p2 p0
p4 △ p3
mid p2 p1
d1 mid p4
d2 d1 mid
EOF
# Rebinding `mid` changes its value, not just its spelling, so an alias laid
# where it must not be would answer wrongly rather than coincidentally right.
sed 's/^mid p2 p1$/mid p2 p3/' "$M1" > "$M2"

pass=0
fail=0
check() { # name expected actual
  if [ "$2" = "$3" ]; then
    echo "PASS runner $1"
    ((pass++)) || true
  else
    echo "FAIL runner $1: expected '$2', got '$3'"
    ((fail++)) || true
  fi
}

evaluate() { # runner-mode module symbol [extra env...]
  local mode=$1 module=$2 symbol=$3
  TREE_CALCULUS_RUNNER=$mode TREE_CALCULUS_CACHE=$CACHE \
    node "$DAG_JS" eval --symbol "$symbol" --format term "$module"
}

oracle() { # module symbol — pure Node, no runner, no cache
  env -u TREE_CALCULUS_RUNNER -u TREE_CALCULUS_CACHE \
    node "$DAG_JS" eval --symbol "$2" --format term "$1"
}

# Cold: the eager runner evaluates M1, dumps it, and caches the answer.
check "eager cold matches Node" "$(oracle "$M1" d2)" "$(evaluate eager "$M1" d2)"
entries=$(ls "$CACHE/module-v1" | grep -cv RECENT)
check "the loaded module was dumped" 1 "$entries"

# Warm module, new question: a fresh process re-loads the dump — structural
# `~` lines and all — instead of re-evaluating, and must say the same thing.
check "eager re-load of the dump matches Node" "$(oracle "$M1" p4)" "$(evaluate eager "$M1" p4)"

# Same question again: answered from reduce-v1 without touching the runner.
check "a cached answer is the same answer" "$(oracle "$M1" p4)" "$(evaluate eager "$M1" p4)"

# A changed module: the delta loader aliases the unchanged prefix into M1's
# dump and evaluates only what `mid` rebinding reaches. RUNNER_STATS shows
# which file was actually loaded, so the delta path is asserted, not assumed.
stats=$(TREE_CALCULUS_RUNNER=eager TREE_CALCULUS_CACHE=$CACHE RUNNER_STATS=1 \
  node "$DAG_JS" eval --symbol d2 --format term "$M2" 2>&1 >"$CACHE/m2.answer")
check "changed module matches Node" "$(oracle "$M2" d2)" "$(cat "$CACHE/m2.answer")"
case "$stats" in
  *"load "*delta-*) echo "PASS runner delta path engaged"; ((pass++)) || true ;;
  *) echo "FAIL runner delta path engaged: stats said: $stats"; ((fail++)) || true ;;
esac
entries=$(ls "$CACHE/module-v1" | grep -cv RECENT)
check "the changed module was dumped too" 2 "$entries"

# The lazy runner answers a question nobody cached yet, over the same cache.
check "lazy runner matches Node" "$(oracle "$M1" p3)" "$(evaluate 1 "$M1" p3)"

echo ""
echo "runner: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
