# MedBook

A limit order book and matching engine in C++23, with a feed replayer and a live terminal dashboard.
It ships with a mock feed that replays the 2008 financial crisis (Sep 2008 to Mar 2009) in two minutes.

## Video Demo
https://github.com/user-attachments/assets/d3cbefc3-b916-4aad-9210-282d87940bf2

## Architecture

```
crisis_feed.py ──► feed file (CSV) ──► FeedSource ──► MatchingEngine ──► OrderBook (one per symbol)
                                                           │
                                                           ├──► trades_<symbol>.csv
                                                           └──► Dashboard (live terminal view)
```

| Component | Role |
|---|---|
| `OrderBook` | Resting orders for one symbol. Each side is a red-black tree of price levels, and each level is a FIFO queue of orders (price-time priority). Hash maps give O(1) lookup by order id and by price. |
| `OrderPool` | Preallocated storage for orders, so orders are never heap-allocated one by one. |
| `MatchingEngine` | One book per symbol. Matches incoming limit orders against the opposite side at the resting price, rests any remainder, and records trades. A price-changing modify is matched like a new order. |
| `FeedSource` | Parses the feed file and replays it into the engine. It maps the feed's own order ids to the ids the engine assigns. Malformed or invalid lines are counted and skipped. |
| `Dashboard` | Terminal UI: top of book for every symbol, a depth ladder, throughput and latency percentiles. |
| `crisis_feed.py` | Generates the mock feed, a file of the trades its reference matcher expects, and hand-written test scenarios. |

Supported commands: limit `ADD`, `MODIFY` and `CANCEL`. A modify that only lowers the quantity keeps the
order's place in the queue; raising the quantity or changing the price sends it to the back with a new
id. Orders that have partially filled can't be modified.

## Build & test

Supported platforms: macOS and Linux.

### Requirements
- A C++23 compiler: Clang 15+ or GCC 11+
- CMake 3.20+
- Python 3 (only to generate the mock feed)

### Build and run the tests
```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To pick a compiler, set it when configuring, e.g. `cmake -S . -B build -DCMAKE_CXX_COMPILER=g++`.
Debug builds use AddressSanitizer and UndefinedBehaviorSanitizer by default; turn them off with
`-DMEDBOOK_SANITIZE=OFF`. Each test case runs as its own process, so one crashing test doesn't hide the others.

## Replay the 2008 crisis feed

Generate the feed (about 6M lines, ~210 MB in `data/`):
```sh
python3 crisis_feed.py --out data
```

Build without sanitizers, then replay:
```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMEDBOOK_SANITIZE=OFF -DMEDBOOK_BUILD_TESTS=OFF
cmake --build build-release
./build-release/main data/crisis_2008.csv                     # live dashboard, 2 minutes
./build-release/main data/crisis_2008.csv --speed 0 --no-ui   # as fast as possible, report only
```

Dashboard keys: `n`/`p` switch symbol, `+`/`-` change speed, space pauses, `q` quits.

At the end, the engine's trades are compared per symbol with the generator's expected trades, and the
report lists latency percentiles and the slowest commands. All options are listed at the top of `main.cpp`.

## License

MIT, see [LICENSE](LICENSE).
