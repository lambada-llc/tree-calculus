# Mini benchmark

This small suite measures the performance of various implementations.
The key design principle is that the cost of parsing inputs and printing outputs is much smaller than the cost of actual reduction, such that timings are dominated by true computational work of the evaluator.

Each implementation is run `BENCH_N` times (default: 5) and the best (minimum) time is reported.
