# Latency Gate

A C++20 performance-regression gate for tail latency. Every pull request is benchmarked against
its base branch on the same machine, and the check fails when p99 or p99.9 latency gets worse by
more than a threshold **and** more than run-to-run noise can explain.

The code it guards is a small market-data handler: a Nasdaq TotalView-ITCH 5.0 decoder feeding a
single-stock limit order book, in two versions (a `std::map` reference and a cache-friendly flat
array) that are checked against each other on every event.

![ci](https://github.com/SirjanSingh/latency-gate/actions/workflows/ci.yml/badge.svg)

## Results so far

Measured on one laptop under WSL2 (environment below). Medians of 5 interleaved runs,
250,094 book events per run. Raw rows: [`evidence/2026-10-11-wsl2/`](evidence/2026-10-11-wsl2/).

| book | p50 | p99 | p99.9 | throughput (untimed pass) |
|---|---|---|---|---|
| `std::map` price levels | 199 ns | 599 ns | 1,334 ns | 18.1 M frames/s |
| flat array price levels | 159 ns | 418 ns | 913 ns | 22.6 M frames/s |

Latency is decode + book update + top-of-book read for one message. A single `steady_clock` read
pair costs about 45 to 70 ns on this VM and is included in every sample (reported, not subtracted),
so the medians overstate the work itself.

Does the gate tell a real slowdown from noise? Local experiments, same input, base and head runs
interleaved:

| experiment | runs per side | p99 ratio head/base | 95% CI | verdict |
|---|---|---|---|---|
| identical binaries | 6 | 0.980 | [0.793, 1.222] | pass (no false alarm) |
| head allocates on the heap once per event | 6 | 1.222 | [1.046, 1.420] | **pass: missed** |
| same allocation | 12 | 1.311 | [1.120, 1.606] | **fail: caught** |

The 6-run miss is kept on purpose. On a noisy VM the interval is wide, and a 20 to 30% p99
regression was not reliably separated from noise with 6 runs per side. With 12 it was, so the
CI gate uses 12.

## How the gate decides

1. `lg-bench` replays the same input and writes one row per run: p50, p90, p99, p99.9, max.
2. The CI job builds the PR head and its base branch, then runs them alternately 12 times each on
   one runner, so drift on the machine hits both sides equally.
3. `lg-gate` takes the per-run p99 (and p99.9) values from each side and bootstraps a 95%
   confidence interval for `median(head) / median(base)`, resampling whole runs.
4. Verdict: **regression** only if the whole interval is above `1 + threshold` (default 5%);
   **improvement** if it is entirely below `1 - threshold`; otherwise no change. Fewer than 3 runs
   per side is reported as insufficient data rather than guessed.

Shared CI runners are noisy VMs. Their absolute numbers are not meaningful and are never quoted;
only the base/head comparison inside one job is used.

## The workload

**Decoder** (`include/lg/itch.hpp`). ITCH 5.0 messages S, R, A, F, E, C, X, D, U in Nasdaq's
BinaryFILE framing (2-byte big-endian length before each message). The frame length is checked
against the spec length before any field is read; other message types are counted and skipped.

**Books** (`include/lg/book.hpp`), one stock locate each, same interface:

- `BookMap`: `std::map` per side plus an order index. Simple and obviously correct; the oracle.
- `BookFlat`: one slot per one-cent tick in a declared window ($80.00 to $119.99), best bid and ask
  cached, walk to the next level only when the best empties. Prices outside the window are rejected
  and counted, never silently dropped.

Book rules, applied identically by both: an execute or cancel for more shares than remain is
clamped and counted; an event for an unknown order reference is counted and ignored; a replace
removes the original order and adds the new reference on the same side.

**Correctness.** `lg-replay --check` runs both books side by side and compares top of book after
every event and full depth at the end. On the 1M-message input: 0 mismatches on all 4 symbols.
CI runs the same check on every push.

**Input** (`include/lg/synth.hpp`). A seeded **synthetic** session, not real exchange traffic:
4 symbols, adds near the touch, partial cancels, deletes, executions and replaces, the book kept
uncrossed (a mid-price move fills the resting orders it crosses) and depth bounded. It lets the
benchmark run anywhere without multi-gigabyte downloads. No Nasdaq data is stored in this repo.

## Benchmark method

- Input loaded and split into frames before timing; no I/O or printing in the timed loop.
- Thread pinned with `sched_setaffinity`. That limits migration; it is not core isolation
  (no `isolcpus`, no IRQ steering, and on WSL2 the Windows scheduler still sits underneath).
- One untimed warm-up pass, then each run on a fresh book. Sample vectors reserved up front and
  written after the second clock read.
- Percentiles are nearest-rank over every sample; mean is reported but not used for decisions.

## Environment for the numbers above

```
cpu       13th Gen Intel Core i5-13420H (hybrid P/E cores), benchmark pinned to cpu 2
os        WSL2, kernel 6.18.40.1-microsoft-standard-WSL2, on Windows 11 (not bare metal)
compiler  g++ 13.3.0, CMake Release (-O3 -DNDEBUG), no -march=native
input     lg-gen --seed 42 --messages 1000000, locate 1
```

These numbers compare versions of this code on this machine. They are not comparable to
production feed handlers on tuned servers.

## Build and run

Needs a C++20 compiler and CMake 3.20+. No third-party dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure

./build/lg-gen --seed 42 --messages 1000000 --out data/synth.itch
./build/lg-replay data/synth.itch --locate 1 --check
./build/lg-bench data/synth.itch --book flat --reps 3 --cpu 2 --csv flat.csv

# compare two result files the way CI does
./build/lg-gate base.csv head.csv --threshold 0.05
```

## Layout

```
include/lg/itch.hpp         decoder and BinaryFILE frame reader
include/lg/itch_encode.hpp  encoder (generator and tests)
include/lg/book.hpp         BookMap, BookFlat, apply()
include/lg/synth.hpp        seeded synthetic session
include/lg/stats.hpp        percentiles, bootstrap interval
include/lg/gate.hpp         regression verdict
src/ bench/                 lg-gen, lg-replay, lg-gate, lg-bench
tests/test_main.cpp         decoder, books, differential, gate false-alarm rate
.github/workflows/          ci.yml (build, tests, differential), gate.yml (PR latency gate)
evidence/                   raw benchmark rows behind every number in this README
```

## Next

- Simulated MoldUDP64 packet loss, duplication and reordering, with sequence-gap recovery that
  must end in the same book as a clean replay.
- Optimisation steps one at a time, each with its own before/after row: open-addressing order
  index, occupancy bitmap for the best-level walk, stock-locate filtering before full decode.
- Per-message-type gating, with a correction for testing several types at once.
