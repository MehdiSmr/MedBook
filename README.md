# MedBook
Limit Order Book Matching Engine with Signal &amp; Backtest

## Build & test

Supported platforms: macOS and Linux.

### Requirements
- A C++23 compiler: Clang 15+ or GCC 11+
- CMake 3.20+

macOS (Homebrew):
```sh
brew install cmake googletest
```

Ubuntu / Debian:
```sh
sudo apt install build-essential cmake libgtest-dev
```

### Build
```sh
cmake -S . -B build
cmake --build build
./build/main
```

To pick a compiler, set it when configuring, e.g. `cmake -S . -B build -DCMAKE_CXX_COMPILER=g++`.

### Run the tests
```sh
ctest --test-dir build --output-on-failure
```

Each test case runs as its own process, so one crashing test doesn't hide the results of the others.
Builds use AddressSanitizer and UndefinedBehaviorSanitizer by default; to turn them off:
`cmake -S . -B build -DMEDBOOK_SANITIZE=OFF`.
