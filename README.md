# MedBook
Limit Order Book Matching Engine

## Video Demo
https://github.com/user-attachments/assets/d3cbefc3-b916-4aad-9210-282d87940bf2

## Build & test

Supported platforms: macOS and Linux.

### Requirements
- A C++23 compiler: Clang 15+ or GCC 11+
- CMake 3.20+

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

### Replay the 2008 crisis feed
Generate the mock feeds (Python 3, standard library only; ~6M lines, ~210 MB in `data/`):
```sh
python3 crisis_feed.py --out data
```

Build without sanitizers for realistic latency numbers, then replay with the live dashboard:
```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMEDBOOK_SANITIZE=OFF -DMEDBOOK_BUILD_TESTS=OFF
cmake --build build-release
./build-release/main data/crisis_2008.csv            # 2 minutes, real time
./build-release/main data/crisis_2008.csv --speed 0 --no-ui   # as fast as possible, report only
```

Keys: `n`/`p` change the symbol in the depth panel, `+`/`-` change speed, space pauses, `q` quits.
At the end the engine's trades are compared per symbol with `data/crisis_2008_trades.csv`, the generator's
own reference matcher. All options are listed at the top of `main.cpp`.
