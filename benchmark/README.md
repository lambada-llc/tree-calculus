# Mini benchmark

This small suite measures the performance of various implementations.
The key design principle is that the cost of parsing inputs and printing outputs is much smaller than the cost of actual reduction, such that timings are dominated by true computational work of the evaluator.

Each implementation is run `BENCH_N` times (default: 5) and the best (minimum) time is reported.

See [BREAKING-RECORDS.md](BREAKING-RECORDS.md) for notes on the fastest
strategies (cache-dense representation and the peeking super-rules, both now
in-tree) plus not-yet-in-tree avenues: a parallel prototype and memoization /
GPU / compile designs, with honest notes on where each does and doesn't pay off.
