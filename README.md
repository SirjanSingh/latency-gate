# Latency Gate

A C++ performance-regression detector: every commit is benchmarked, and the merge is blocked
when tail latency (p99 / p99.9) gets worse by more than measurement noise can explain.

The workload it guards is a Nasdaq TotalView-ITCH 5.0 decoder feeding a single-stock limit
order book. A cache-friendly flat-array price book is checked against a `std::map` reference
on every event before anything is timed.

## Status

Work in progress. Planned pieces:

- ITCH 5.0 decoder (add, execute, cancel, delete, replace, stock directory) with bounds checks
- Reference (`std::map`) and flat-array order books with per-event differential checks
- Seeded synthetic ITCH generator, so the benchmark runs without large downloads
- Benchmark harness: p50 / p99 / p99.9 per message type, warm-up, pinned thread
- CI gate: repeated runs and bootstrap confidence intervals to decide whether a change is a real regression

## Measurement honesty

Numbers in this repo are measured on a laptop under WSL2. They are useful for comparing
versions of this code on the same machine, not for comparing against production hardware.

## Build

C++20, CMake. Instructions will land with the first code drop.
