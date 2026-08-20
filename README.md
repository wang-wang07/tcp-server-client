# TCP Server and Client
A C++TCP client and server backed by an in-memory key-value store

## Build

```sh
cmake -S . -B build
cmake --build build
```

The build produces the `server`, `client`, and `playground` executables.

## Test

Tests use GoogleTest through CMake's `FetchContent`. The first configuration
downloads the pinned GoogleTest source.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

To run a subset of tests directly, use a GoogleTest filter:

```sh
./build/tests/tcp_tests --gtest_filter='ParserTest.*'
```

For a production-only build that does not download or build GoogleTest:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
```

## AI Use Disclaimer
- I used AI to guide me toward the milestones. Basically giving me the project goals to complete (TCP echo server/client -> Key Value Store -> Message Framing -> Concurrency, etc). I believed that this was a good use of AI in this project, because the goal was to learn more about c++ and systems programming.

