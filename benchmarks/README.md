# Server benchmark

This script sends GET and SET requests to the server and checks every reply.
It also tries a client that asks for lots of data but never reads it, to see
whether the other clients can still get work done.

## Run it

Use the Linux dev container, with Python 3.10+ and at least 3 available logical CPUs.
Leave port 8080 free. The script starts the server itself.

```sh
# If Python is missing from the container:
apt-get update
apt-get install -y --no-install-recommends python3

cmake -S . -B build/benchmark-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/benchmark-release --parallel 4
ctest --test-dir build/benchmark-release --output-on-failure

# Quick check before the longer run
python3 benchmarks/mixed_workload.py \
  --server build/benchmark-release/server \
  --output build/benchmark-smoke --seconds 2 --warmup 1 --smoke

# Full run, roughly 8 minutes
python3 benchmarks/mixed_workload.py \
  --server build/benchmark-release/server \
  --output build/benchmark-results --seconds 60 --warmup 5
```

Use a new output folder each time. Keep other heavy programs and tests stopped
while it runs. Inside the container, these commands run from the project folder.

## What it tests

- 10,000 keys, 256-byte values, 80% GET and 20% SET.
- 5 seconds to warm up, then 60 seconds of measurements per case.
- 1, 32, and 127 working clients, each with and without a stalled reader.
- One extra run with 128 working clients. That's the server's connection limit,
  so the biggest stalled-reader case uses 127 working clients + 1 stalled client.

Each working client waits for its reply before sending another request. Stalled
clients are replaced every 20 seconds so the idle timeout doesn't remove the
problem halfway through. Only working-client replies count toward the results.

The output folder has request counts, latency histograms, CPU/memory samples,
and server logs. These are local loopback measurements, not internet or EC2 speeds.

[Results and resume bullet](RESULTS.md) ·
[Step-by-step explanation](../docs/BENCHMARK_WALKTHROUGH.md)
