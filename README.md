# TCP key-value server

This is a small Linux C++23 project for learning network server fundamentals. It
uses nonblocking TCP sockets and `epoll` directly. It runs as a single process
and keeps its key-value store in memory, so data is lost when it exits. Raft,
persistence, and sharding are not implemented.

One event-loop thread buffers partial requests and writes, with bounded work
per connection and backpressure for slow readers. The server allows 128 clients,
64 KiB input and 128 KiB output buffers per connection, and a 30-second inactivity
timeout. The store itself has no size limit.

Clients send one uppercase command per line. Keys and values are single
whitespace-separated tokens, with no escaping. Replies are newline terminated.
Commands execute in order within each connection, but replies are untyped; if a
connection is lost, a client may not know whether its last change took effect.

| Request | Reply |
| --- | --- |
| `SET key value` | `OK` |
| `GET key` | Value or `NOT_FOUND` |
| `DELETE key` | `OK` or `NOT_FOUND` |
| `EXISTS key` | `1` or `0` |
| `COUNT` | Number of keys |

## Build and run

Build on Linux (or in the included dev container) with CMake 3.20+, a C++23
compiler and standard library supporting `std::expected`, and Perl for the
integration tests. CMake downloads GoogleTest when tests are enabled; pass
`-DBUILD_TESTING=OFF` to skip tests and that download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/server
```

The server listens on port 8080. In another terminal:

```sh
printf 'SET greeting hello\nGET greeting\n' | ./build/client
```

Stop the server to free port 8080, then run
`ctest --test-dir build --output-on-failure`. Tests cover the protocol, store,
partial I/O, backpressure, and connection limits. The
[benchmark instructions](benchmarks/README.md) describe the mixed GET/SET
workload and link to its [measured results](benchmarks/RESULTS.md).
