# TCP Server and Client

A C++TCP client and server backed by an in-memory key-value store

## Disclaimer Of AI Use
- This project idea was literally generated from a prompt along the lines of "I want to learn c++ through building a project, please suggest a project that is a valuable for learning"
- A lot of the requirements and sequential goals for this project were generate by AI, this being that I should achieve "tcp client/server echo -> RAII File Descriptor Wrapper -> Message Framing -> Read all send all -> so on so farth"
- Use of AI to help find learning resources and for supplemental explanation along with them (man pages, OSTEP, C++ concurrency in action, etc.)
- I setup the CMake, github workflow, and testing for this project using AI
- I have used AI to reorganize the project structure ex) folder names, how hpp and cpp files should be separated, where executables should go
- More recently for this project timeline I used AI to help me research "how" concurrency should be implemented. Mainly what the methods for implementation would be (Thread pool vs Event Polling). I intend to pursue and implement both in order to understand the tradeoffs and as a general learning exercise for concurrency

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


