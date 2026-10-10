### Latency gate (flat book, ALL messages)

Fails only if the 95% bootstrap interval for median(head) / median(base) lies entirely above 1.05.

| metric | runs (base/head) | base median ns | head median ns | ratio | 95% CI | verdict |
|---|---|---|---|---|---|---|
| p99_ns | 6/6 | 452 | 552 | 1.222 | [1.046, 1.420] | no change |
| p999_ns | 6/6 | 1139 | 1159 | 1.018 | [0.817, 1.424] | no change |

**Result: PASS**
