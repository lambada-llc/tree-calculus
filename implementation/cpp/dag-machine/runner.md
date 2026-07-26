# runner — fast tree-calculus program runner

`runner.cpp` single-file C++ port of some subset of the `bin/main.js` CLI functionality. Where `reduce`/`canonicalize` transform a DAG in place, `runner` runs a program against data: it marshals host strings/bytes into tree-calculus values, applies the program, and decodes the result back.

## Modes

### One-shot

    runner <dag-file> <string>

reads the DAG from `<dag-file>`, applies it to `<string>` (marshalled as a list-of-bytes), reduces, and prints the result as a string with a
trailing newline (matching `console.log`).

### Server

    runner -s
    runner --server

reads commands from stdin, writes responses to stdout. Commands are
newline-terminated; some carry a length-prefixed payload.

| request                                  | response                              |
| ---------------------------------------- | ------------------------------------- |
| `load <path>\n`                          | `ok\n` (replaces env)                 |
| `eval <symbol>\n`                        | `data <len>\n<bytes>` (`to_string`)   |
| `eval-dag <symbol>\n`                    | `data <len>\n<bytes>` (reduced DAG)   |
| `apply <symbol> <byte-len>\n<bytes>`     | `data <len>\n<bytes>` (`to_string`)   |
| `reset\n`                                | `ok\n` (drop arena, re-parse bundle)  |
| `quit\n`                                 | `ok\n` (and exits)                    |

On any failure: `err <message>\n`. `<bytes>` in responses is exactly
`<len>` raw bytes, no trailing newline (length is exact).

State across commands: TC reduction is confluent, so trees in the env
get progressively more reduced as commands fire — pure benefit. The
flip side is that allocations from reduction are never freed until a
recycle (see `RUNNER_RSS_THRESHOLD_MB` below).

## Environment variables

These influence runtime behaviour. All are optional; defaults aim to
"just work" on memory-tight hosted CI builders without any config.

### Read by `runner` itself

#### `RUNNER_RSS_THRESHOLD_MB` *(default: 512)*

Auto-recycle watermark for server mode. After each `eval`/`apply` we
read `/proc/self/status:VmRSS`; if it's above this threshold we drop the
arena, clear env, re-parse the bundle (~17 ms on the canonical bundle),
and `malloc_trim(0)` so the OS sees the freed pages. `0` disables
auto-recycling.

Bump this on a roomy local machine (e.g. `RUNNER_RSS_THRESHOLD_MB=4096`) to
recycle less often and squeeze more cross-eval reduction sharing out of
the in-memory state.

The watermark is checked *between* commands, so a single eval that
allocates well past the threshold can still spike. Lowering this only
helps with cumulative growth, not single-eval peaks.

#### `RUNNER_WORKER_STACK_MB` *(default: 64)*

`runner` runs all real work on a worker pthread with this stack size, since
`reduce()` is recursive and chains tens of thousands of frames deep on
heavy benchmark suites (`Poly.Bench`, `Nat.Bench`). Bump this if you
hit a `runner: pthread_create(stack=…): …` error or a SIGSEGV during
reduction.

### Read by the build scripts that drive `runner`

#### `RUNNER_WORKERS` *(default: 1)*

Read by `build/rules/compile/compile-all.js` and
`build/rules/dag-test.js`. Number of node worker threads, each owning
its own `runner -s` subprocess. Defaults to 1 because each subprocess can
peak at hundreds of MiB to a few GiB on heavy chunks/tests, and total
memory scales linearly with the count.

Set `RUNNER_WORKERS=$(nproc)` on a beefy local machine to parallelise.

#### `RUNNER_BATCH_CHUNKS` *(default: 16)*

Read by `build/rules/compile/compile-all.js`. How many LambAda chunks
one `runner -s` invocation handles before we cycle the process. Within a
batch we share reductions across chunks; between batches the OS
reclaims the leaked arena, keeping peak RSS bounded.

Larger batches (e.g. `RUNNER_BATCH_CHUNKS=64`) trade memory for slightly
more cross-chunk sharing; smaller (e.g. `RUNNER_BATCH_CHUNKS=4`) trade
sharing for tighter memory.

#### `CXX` *(default: `c++`)*

Compiler used for the on-demand build of `runner.cpp` to `.runner.exe`. The
build flags are fixed: `-O3 -std=c++17 -pthread`.

## Tuning recipes

| environment            | recommended setting                            |
| ---------------------- | ---------------------------------------------- |
| Hosted CI (Cloudflare) | leave defaults                                 |
| Local 8-core, 16 GiB   | `RUNNER_WORKERS=$(nproc)`                          |
| Local with lots of RAM | `RUNNER_WORKERS=$(nproc) RUNNER_RSS_THRESHOLD_MB=4096` |
| Debugging stack issues | `RUNNER_WORKER_STACK_MB=256`                       |
| Disable auto-recycle   | `RUNNER_RSS_THRESHOLD_MB=0`                        |
