# Benchmark results — Sept. 28, 2026

With 127 working clients and one stalled reader, the server handled
**145,447 GET/SET operations per second** with p99 latency of 1.177 ms.

The benchmark ran once per case on an Apple M4 Pro with 48 GiB RAM, using Linux
in Docker. The VM had 14 virtual CPUs and about 7.75 GiB RAM. The server used
GCC 13.3.0 in Release mode (`-O3 -DNDEBUG`). Each run had 5 seconds of warmup
and 60 seconds of measurement, with 10,000 keys, 256-byte values, and 80% GET /
20% SET requests.

| Working clients | Stalled readers | Ops/sec | p95 (ms) | p99 (ms) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 26,105 | 0.043 | 0.054 |
| 1 | 1 | 25,984 | 0.042 | 0.049 |
| 32 | 0 | 192,703 | 0.221 | 0.248 |
| 32 | 1 | 184,532 | 0.235 | 0.259 |
| 127 | 0 | 142,188 | 1.142 | 1.209 |
| 127 | 1 | 145,447 | 1.127 | 1.177 |
| 128 | 0 | 139,466 | 1.121 | 1.199 |

All 51.4 million measured requests received correct replies. The 47 Release
tests passed before the run. Stalled readers had queued output in all 60
samples of their runs. The server's 128-connection limit means the largest
stalled-reader case used 127 working clients plus one stalled client.

These are single-run loopback measurements. The small throughput difference
between the 127-client cases does not show that stalled readers improve
performance. Throughput is averaged across each 60-second run, and p99 does not
describe every reply. Clients wait for each reply, so this is not an
unlimited-load test.

Run instructions are in the [benchmark README](README.md). The tracked
[summary CSV](results/2026-09-28/summary.csv) and [full results JSON](results/2026-09-28/results.json)
contain the measurements; [environment details](results/2026-09-28/environment.json)
and the [audit record](results/2026-09-28/audit.txt) document the run.
