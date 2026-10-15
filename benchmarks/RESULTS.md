# Benchmark results — Sept. 28, 2026

The useful result: **145K GET/SET operations per second with 127 working clients
and one stalled reader. p99 latency was 1.18 ms.**

Ran on an Apple M4 Pro with 48 GiB RAM, using Linux in Docker. The VM had 14
virtual CPUs and about 7.75 GiB RAM. The C++23 server was built with GCC 13.3.0
in Release mode (`-O3 -DNDEBUG`). No server code or limits were changed.

Each run used 10,000 keys, 256-byte values, and 80% GET / 20% SET requests.
There were 5 seconds of warmup followed by 60 seconds of measurement.

| Working clients | Stalled readers | Ops/sec | p95 (ms) | p99 (ms) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 26,105 | 0.043 | 0.054 |
| 1 | 1 | 25,984 | 0.042 | 0.049 |
| 32 | 0 | 192,703 | 0.221 | 0.248 |
| 32 | 1 | 184,532 | 0.235 | 0.259 |
| 127 | 0 | 142,188 | 1.142 | 1.209 |
| 127 | 1 | 145,447 | 1.127 | 1.177 |
| 128 | 0 | 139,466 | 1.121 | 1.199 |

The server only allows 128 connections, which is why the last stalled-reader
test uses 127 working clients. The 127-client run without a stalled reader is
there for comparison.

All **51.4 million measured requests** got correct replies. All **47 Release
tests** passed before benchmarking. The stalled readers had queued server output
in all 60 samples of each run, so they really were holding up their connections.

A few things to keep in mind: this was one run per case, all on loopback. The
small speedup in the 127-client stalled case isn't proof that stalled readers
help. Throughput is a 60-second average, and p99 doesn't mean every reply was
that fast. Each client waits for a reply, so this also isn't an unlimited-load test.

## Files

[Run instructions](README.md) · [Plain-English walkthrough](../docs/BENCHMARK_WALKTHROUGH.md)

The [CSV](results/2026-09-28/summary.csv) has the main numbers. The
[JSON results](results/2026-09-28/results.json) have the detailed measurements.
The same folder also holds the environment, build details, test log, and audit.
Each case has its own server log and per-worker latency counts.
