
## Getting Started

### Prerequisites
* Install [OCaml](https://ocaml.org/docs/installing-ocaml)
* Install [Dune](https://dune.build/install)

### Build
```
dune build
```
or
```
dune build --watch
```

### Auto-format
```
dune fmt
```

### Build and run self-check
```
dune exec tree_calculus_reference_implementation
```

### Test

The tests are inline [`ppx_expect`](https://github.com/janestreet/ppx_expect)
expectation tests embedded in the library sources (for example the `let%expect_test`
blocks in [`lib/stepper.ml`](lib/stepper.ml)). Run them with:
```
dune runtest
```
Each test prints to stdout and compares the output against the expected text in its
`[%expect {| ... |}]` block; a mismatch is reported as a diff.

When you change behaviour (or add a new test with a placeholder block) the printed
output is the new source of truth. Review the diff, then accept it with:
```
dune promote
```
or do both in one step:
```
dune runtest --auto-promote
```
This rewrites the `[%expect]` blocks in place, so commit the updated sources afterwards.
