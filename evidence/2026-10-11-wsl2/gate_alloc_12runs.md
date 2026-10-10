### Latency gate (flat book, ALL messages)

Fails only if the 95% bootstrap interval for median(head) / median(base) lies entirely above 1.05.

| metric | runs (base/head) | base median ns | head median ns | ratio | 95% CI | verdict |
|---|---|---|---|---|---|---|
| p99_ns | 12/12 | 423 | 554 | 1.311 | [1.120, 1.606] | REGRESSION |
| p999_ns | 12/12 | 997 | 1204 | 1.208 | [0.947, 1.509] | no change |

**Result: FAIL** (tail latency regressed)
