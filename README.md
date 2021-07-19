# TermBench V2

This is a simple benchmark you can use to see how your terminal sinks large outputs.  While it cannot time how long it takes your terminal to render (since it has no idea), it _can_ time how long it takes your terminal to accept the data, which is what termbench measures.  For a full benchmark, you would need to also time how long your renderer takes to complete rendering after the sink finishes.

# Usage

On slow terminals, you will want to run termbench like this:

```
termbench_release_clang small
```

This will run very small data sizes (~1 megabyte) so that the terminal has a prayer of completing the benchmark in a reasonable amount of time.  This is the recommended setting for things like cmd.exe or Windows Terminal.

For terminals that have reasonable performance, you run it like this:

```
termbench_release_clang
```

for the regular benchmark sizes, or like this

```
termbench_release_clang large
```

for larger benchmark sizes (if you want more of a stress test than normal).

# Expected Results

On modern Windows machines with memory bandwidth in the 10-20gb/s range, the expected throughput for these tests would be in the 0.5-2.0gb/s range for a reasonable terminal.  Numbers significantly higher than that might indicate a well-optimized terminal, and numbers significantly lower than that might indicate a poorly written terminal.

Termbench has not yet been tested on Linux, so we do not have expected bandwidth numbers at this time.

Obviously, throughput numbers depend greatly on the underlying hardware, and the operating system pipe behavior, so take these expected values with a grain of salt.
