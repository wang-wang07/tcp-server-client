# TCP Server and Client

A C++TCP client and server backed by an in-memory key-value store. WORK IN PROGRESS

## Disclaimer Of AI Use
So far I did use Codex to setup Cmake and the testing for this project, and to help reorganize some files

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


