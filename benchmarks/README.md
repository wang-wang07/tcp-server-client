# Server benchmark

This script measures GET/SET traffic against the local server, including runs
with a client that stops reading its replies. It checks replies from every
working client. Results are for loopback, not internet or EC2 performance.

Use Linux, Python 3.10+, and at least three available logical CPUs. Keep port
8080 free. The script starts and stops the server. If Python is missing in the
dev container, install it with `apt-get update` and
`apt-get install -y --no-install-recommends python3`.

Build and run the full benchmark from the project root:

```sh
cmake -S . -B build/benchmark-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/benchmark-release --parallel 4
ctest --test-dir build/benchmark-release --output-on-failure
python3 benchmarks/mixed_workload.py \
  --server build/benchmark-release/server \
  --output build/benchmark-results --seconds 60 --warmup 5
```

The run takes about eight minutes. Use a new output directory each time and
avoid running other heavy work during measurement. For a quick check, add
`--seconds 2 --warmup 1 --smoke` and use a different output directory.

The workload uses 10,000 keys, 256-byte values, and 80% GET / 20% SET requests.
It measures 1, 32, and 127 working clients with and without a stalled reader,
plus 128 working clients without one. Each working client waits for its reply
before sending again. Stalled clients are renewed every 20 seconds so the
server's idle timeout does not remove them during a run. Only working-client
replies count in the results.

See the [results](RESULTS.md), including links to the tracked CSV and JSON
measurements.
