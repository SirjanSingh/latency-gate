### Latency gate (flat book, ALL messages)

Fails only if the 95% bootstrap interval for median(head) / median(base) lies entirely above 1.05.

| metric | runs (base/head) | base median ns | head median ns | ratio | 95% CI | verdict |
|---|---|---|---|---|---|---|
| p99_ns | 6/6 | 432 | 424 | 0.980 | [0.793, 1.222] | no change |
| p999_ns | 6/6 | 1064 | 987 | 0.927 | [0.669, 1.360] | no change |

**Result: PASS**
