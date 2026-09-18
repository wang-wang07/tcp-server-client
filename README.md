# C++ TCP Key-Value Server

A Linux-only C++23 in-memory key-value server with nonblocking TCP and direct
`epoll`, built without networking frameworks. Currently single-node;
Raft replication, persistence, and sharding are [planned](docs/DISTRIBUTED_KV_TUTORIAL.md).

## Architecture

```text
Request: client -> input buffer -> parser -> executor -> shared store
Reply:   executor -> output buffer -> client
```

- **Event loop:** one thread uses level-triggered `epoll` through `EventPoller`.
  Listener/client sockets are nonblocking; `EPOLLOUT` is enabled only for pending replies.
- **Stream handling:** each `Connection` owns its socket via RAII, buffers partial
  requests, and resumes partial writes after `EAGAIN`. Buffered commands are
  scheduled without waiting for another socket event.
- **Fairness and backpressure:** bounded accept/read/write work and 32 commands per
  visit. Slow readers pause reads and execution at 64 KiB queued output, resuming
  at 32 KiB. Limits: 128 clients, 64 KiB input / 128 KiB output per connection,
  and a 30-second inactivity timeout.
- **State:** commands execute serially against one `std::unordered_map`, preserving
  each connection's request order. Store size is unbounded; all data is lost on exit.

## Protocol

Requests and replies end in `\n`; fragmented and pipelined requests are supported.
Commands are uppercase, with whitespace-separated keys/values and no escaping.

| Request | Reply |
| --- | --- |
| `SET key value` | `OK` |
| `GET key` | Value or `NOT_FOUND` |
| `DELETE key` | `OK` or `NOT_FOUND` |
| `EXISTS key` | `1` or `0` |
| `COUNT` | Number of keys |

Malformed commands return `INVALID COMMAND MESSAGE`. Replies are untyped;
a lost reply can leave a mutation's outcome unknown to the client.

## Build and run

Requires Linux (or the included dev container), a C++23 compiler/standard library
with `std::expected`, CMake 3.20+, and Perl for integration tests. Configuration
downloads pinned GoogleTest; use `-DBUILD_TESTING=OFF` to skip tests and the download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j
./build/server                 # listens on 0.0.0.0:8080
```

In another terminal:

```sh
printf 'SET greeting hello\nGET greeting\n' | ./build/client
# Server: OK
# Server: hello
```

## Tests

```sh
ctest --test-dir build --output-on-failure
```

GoogleTest covers parsing, framing, store operations, socket ownership, epoll,
partial I/O, and backpressure. The TCP integration workload checks connection
limits, idle expiry, slow readers, resets, and half-close draining; it reports
throughput, p95 latency, CPU, and sampled RSS. Stop the server first to free port
8080; the workload includes a 30-second timeout wait.

More detail: [server design](docs/EPOLL_SERVER_DESIGN.md) ·
[guarantees](docs/SERVER_GUARANTEES.md) · [networking roadmap](docs/EPOLL_TUTORIAL.md).
