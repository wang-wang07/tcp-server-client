# TCP Server and Client
A Linux-only C++23 TCP client and nonblocking epoll server backed by an
in-memory key-value store.

Read the [detailed server design](docs/EPOLL_SERVER_DESIGN.md) for ownership,
scheduling, buffers, backpressure, and failure handling. The
[guarantees](docs/SERVER_GUARANTEES.md) describe the current contract; the
[tutorial roadmap](docs/EPOLL_TUTORIAL.md) records verified progress.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The build produces the `server`, `client`, and `playground` executables.

## Test

Tests use GoogleTest through CMake's `FetchContent`. The first configuration
downloads the pinned GoogleTest source. Perl is required for the TCP integration
test. Keep port 8080 free during CTest; its resource workload starts its own
server and deliberately waits through the 30-second inactivity timeout.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

To run a subset of tests directly, use a GoogleTest filter:

```sh
./build/tests/tcp_tests --gtest_filter='ParserTest.*'
```

For a build without tests that does not download or build GoogleTest:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
```
