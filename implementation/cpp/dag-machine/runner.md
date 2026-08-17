# runner — fast tree-calculus program runner

`runner.cpp` single-file C++ port of some subset of the `bin/main.js` CLI functionality. Where `reduce`/`canonicalize` transform a DAG in place, `runner` runs a program against data: it marshals host strings/bytes into tree-calculus values, applies the program, and decodes the result back.

## Modes

### One-shot

    runner <dag-file> <string>

reads the DAG from `<dag-file>`, applies it to `<string>` (marshalled as a list-of-bytes), reduces, and prints the result as a string with a
trailing newline (matching `console.log`).

### Server

    runner -s

reads commands from stdin, writes responses to stdout. Commands are
newline-terminated; some carry a length-prefixed payload.

| request                                      | response                              |
| -------------------------------------------- | ------------------------------------- |
| `load <path>\n`                              | `ok\n` (replaces env)                 |
| `bind <name> <byte-len>\n<bytes>`            | `ok\n` (until the next `reduce`)      |
| `reduce <dag\|string> <byte-len>\n<bytes>`   | `data <len>\n<bytes>`                 |
| `dump\n`                                     | `data <len>\n<bytes>` (evaluated env) |
| `quit\n`                                     | `ok\n` (and exits)                    |

On any failure: `err <message>\n`. `<bytes>` in responses is exactly
`<len>` raw bytes, no trailing newline (length is exact).

Two commands do the work, because there are two questions: *what* to
reduce, and *how* to render it.

`reduce`'s payload is a DAG, read with the loaded module in scope and in
a scope of its own, so what it defines shadows nothing and is released
along with the answer. That is what lets a module be loaded apart from
the expressions asked of it — under the eager evaluator especially,
where putting them in the module means reading it evaluates every one of
them.

A name in the module is a DAG of one word, so looking a symbol up needs
no command of its own:

    reduce dag 8\nBool.not\n          the symbol's value, as a DAG
    reduce string 8\nGreeting\n       the same, marshalled as a string

`bind` is how host data gets in. Its payload is marshalled as a
tree-calculus string and bound under `<name>` in that same request
scope, so a payload mentioning it applies the module to it:

    bind ~src 5\nx = △
    reduce string 33\n~r Lambada.compile_to_dag ~src\n~r\n

Bindings last until the next `reduce`, successful or not — a request
scope ends when the request does. Names beginning with `~` are the
convention for this scaffolding, since nothing a module exports uses
them.

An `err` is recoverable only where the framing was understood. A command
carrying a payload whose length could not be read leaves that payload in
the stream, where it would be read as commands; the server reports and
exits rather than answer nonsense to everything after it. A `load` that
fails leaves no module loaded — half of one would answer `unbound` to
every symbol it did not reach, which reads as a broken bundle rather
than a failed load.

`load` names a path where every other command carries its payload
inline. That is deliberate: the module cache is content-addressed on
disk and a cached dump is re-loaded by path, so a module the runner can
open by name is the thing being cached. Clients holding module text in
memory write it to a file first (see `as_file` in
`implementation/typescript/src/runner/native.mts`).

`dump` (eager only) renders the loaded module back out as one
hash-consed DAG whose every binding is in the state eager loading left
it: fully reduced. Its structural lines live in a `~` id space of their
own, and every application in them merely *builds* a value — △ applied
to a child is a stem, a stem applied to a child is a fork — so loading a
dump costs parsing and interning but no reduction. That is what the
TypeScript runtime's module cache keeps (see `TREE_CALCULUS_CACHE` in
`implementation/typescript/src/runner/native.mts`): evaluating a
module's bindings is paid once per module version, not once per process
that reads it.

State across commands: reduction happens in place, so trees in the env
get progressively more reduced as commands fire — pure benefit. The
flip side is that allocations from reduction are never freed until a
collection (see `RUNNER_RSS_THRESHOLD_MB` below).

## Reduction

Either of two evaluators over the same representation — 8-byte
nil-packed nodes in an mmap'd arena, the one the fastest evaluators in
the benchmark suite use — picked when `runner.cpp` is compiled.

`../lazy-graph-nil-mmap-32.hpp` (default) reduces lazily and in place.
Most of a bundle's bindings have no normal form, so evaluating each one
as it is read diverges; here a binding is an unreduced application node,
forced only as far as a request looks at it.

`../eager-graph-nil-mmap-32.hpp` (`-DRUNNER_EAGER`) normalizes every
binding as the module is read. That is a claim about the module — one
definition that only converges lazily hangs the build — so it is the
caller's to make, and `TREE_CALCULUS_RUNNER=eager` is where they make it.
In exchange, a repository that holds itself to eager termination builds
in about half the time.

Both keep memory bounded by a non-moving mark-and-sweep over `roots()`,
run from inside the reduction loop (see `RUNNER_RSS_THRESHOLD_MB`), and
neither reduces on the C stack. The eager one additionally hash-conses
what it builds and memoizes the reductions it repeats, which is not an
optimization but the thing that makes it usable at all: a DAG bundle says
what it says by sharing, plain eager reduction re-derives each occurrence
and materializes the normal form as a tree, and that tree is
exponentially larger than the DAG it prints as.

## Environment variables

These influence runtime behaviour. All are optional; defaults aim to
"just work" on memory-tight hosted CI builders without any config.

### Read by `runner` itself

#### `RUNNER_RSS_THRESHOLD_MB` *(default: 512)*

Collection budget, in MiB of arena. Once the arena passes it, the
evaluator marks from its root set — every binding of the loaded module,
plus whatever the request itself is holding — and sweeps the rest onto a
free list. `0` lets the arena grow unchecked.

This runs from *inside* the reduction loop, not between commands: a
single `reduce` can allocate a thousand times what it keeps, so peak
memory tracks this setting rather than the size of the heaviest test.
Nothing moves, so the reduction already written into the bundle's nodes
survives — which is the sharing that makes a long-lived server worth
having.

Bump this on a roomy local machine (e.g. `RUNNER_RSS_THRESHOLD_MB=4096`)
to collect less often. The budget also raises itself if a collection
finds that most of the arena is still live, so a genuinely large term
does not turn into a collection per allocation.

Under `-DRUNNER_EAGER` the same setting also sizes the memo, which is the
rest of what a collection has to bound; a tighter budget therefore costs
a little more re-reduction as well as more sweeps.

#### `RUNNER_STATS` *(default: off)*

Any non-`0` value makes server mode print one line per command to
stderr: wall time, and under `-DRUNNER_EAGER` the evaluator's counters
for that command (reduction steps, memo hits and writes, collections and
what they marked, arena high water). This is how one finds out where a
build's time actually goes before optimizing anything.

#### `RUNNER_WORKER_STACK_MB` *(default: 64)*

`runner` runs all real work on a worker pthread with this stack size,
since the lazy evaluator's `whnf` is recursive and chains tens of
thousands of frames deep on heavy benchmark suites (`Poly.Bench`,
`Nat.Bench`). Bump this if you hit a `runner: pthread_create(stack=…): …`
error or a SIGSEGV during reduction. The eager evaluator keeps its
continuations on the heap and does not depend on it.

### Read by the TypeScript runtime that drives `runner`

#### `TREE_CALCULUS_CACHE` *(default: unset — no caching)*

A directory for the runtime's reduction caches, shared freely between
builds, branches and parallel processes because every entry is
content-addressed: evaluated modules under `module-v1/` (what `dump`
returns, keyed by module text, re-loadable without reduction — plus a
fingerprint sidecar under `module-fp-v1/` that lets the *next* version
of a module alias every binding it did not change into the previous
dump instead of re-evaluating it), and per-term results under
`reduce-v1/` (what `reduce` returned, keyed by a Merkle
fingerprint of the term, so an expect test whose term did not change is
answered without spawning the runner at all). Reduction is
deterministic, so a stale entry cannot exist, only a missing one; using
an entry refreshes its mtime, which is what lets a warmer prune by age.

### Read by the build scripts that drive `runner`

#### `RUNNER_WORKERS` *(default: 1)*

Read by `build/rules/compile/compile-all.js` and
`build/rules/dag-test.js`. Number of node worker threads, each owning
its own `runner -s` subprocess. Defaults to 1 because each subprocess can
peak at hundreds of MiB to a few GiB on heavy chunks/tests, and total
memory scales linearly with the count.

Set `RUNNER_WORKERS=$(nproc)` on a beefy local machine to parallelise.

#### `RUNNER_ARENA_MB` *(default: 64)*

Read by `build/rules/compile/compile-all.js`, which passes it as this
process's `RUNNER_RSS_THRESHOLD_MB`. A compile worker holds one runner
for every chunk it has, so what bounds its memory is the collection
budget rather than how often the process is replaced.

64 rather than the runner's own 512 because compiling is thousands of
independent reductions with nothing carried between them: a tight budget
costs a little re-reduction and keeps peak RSS flat. A phase whose
reductions are few and large wants the opposite, and says so by not
passing one.

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
