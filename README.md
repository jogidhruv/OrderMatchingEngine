# Qode — Multi-Symbol Order Matching Engine

A C++20 electronic order matching engine supporting multiple symbols, each with an
independent order book, price-time priority matching, and a multi-producer /
single-matcher-per-symbol threading model.

Order types: **Limit**, **Market**, **IOC**, **Cancel**, **Modify**. Sides: **BUY**,
**SELL**.

---

## Overall Architecture

```
producers --submit()-->  Exchange  --> per-symbol CommandQueue --> SymbolWorker
(N threads)                 |                                     (1 matching thread
                            |                                      per symbol)
                            |                                          |
                            |                                     MatchingEngine
                            |                                      + OrderBook
                            |                                          |
                            +-------------------------> per-symbol Sink (1 writer
                                                         thread) --> <symbol>.log
```

- **Exchange** — entry point. Validates each request synchronously (field / symbol
  checks), assigns a globally unique order id, and routes the command to the
  owning symbol's queue. Read-only after construction, so many producers can call
  `submit()` concurrently.
- **SymbolWorker** — owns one symbol's `CommandQueue`, the single matching thread,
  the `MatchingEngine`, and the `Sink`. The matching thread drains commands in
  batches and processes them in queue order.
- **MatchingEngine** — pure, single-threaded core: matching, cancel, modify,
  self-trade prevention. Produces trades and a top-of-book snapshot.
- **OrderBook** — the per-symbol book (bids / asks, indices).
- **Sink** — a dedicated writer thread per symbol that drains a `ResultQueue` and
  writes execution logs and market-data lines, keeping I/O off the matching thread.

The engine is **sharded by symbol**: each symbol is fully independent, so symbols
match in parallel with no shared book state and no cross-symbol locking.

---

## Data Structures

**Order book (per symbol)**
- `std::map<price, std::list<Order>>` for each side — bids ordered high->low
  (`std::greater`), asks low->high. The map gives ordered price levels; `begin()`
  is always the best price.
- `std::list<Order>` per price level — FIFO time priority within a level; stable
  iterators allow O(1) removal from the middle.
- `unordered_map<global_id, list-iterator>` — locates an order's list node for
  O(1) removal.
- `unordered_map<{clientId, clientOrderId}, global_id>` — resolves the client key
  for cancel / modify and for duplicate / unknown-order detection.

**Concurrency**
- `CommandQueue` / `ResultQueue` — bounded ring buffers (`std::vector` backing) with
  a mutex and two condition variables (not-full / not-empty), providing blocking
  push and batched drain with backpressure.
- `OrderIdGenerator` — a single `std::atomic<uint64_t>` incremented per accepted
  order for globally unique ids.

---

## Time & Space Complexity

Let **L** = number of distinct price levels on a side, **k** = number of fills an
incoming order generates.

| Operation | Complexity |
|-----------|-----------|
| Best bid / ask | O(1) |
| Insert (rest) | O(log L) |
| Remove / cancel | O(1) avg lookup + O(log L) if a level empties |
| Find (by client key) | O(1) avg |
| Match an incoming order | O(k log L) |
| Modify (in-place, qty down) | O(1) avg |
| Modify (priority reset) | O(log L) + match |
| Top-of-book snapshot | O(orders at best level) |

**Space:** O(N) for N active resting orders, plus the two hash indices (also O(N)).
Bounded queues add O(capacity) fixed memory per symbol.

---

## Threading Model

- **Multiple producer threads** call `Exchange::submit()` concurrently. The submit
  path touches only: a read-only symbol map, the atomic id generator, and the
  target symbol's mutex-guarded queue — so it is safe under contention.
- **One matching thread per symbol** drains that symbol's queue and runs the engine.
  Because a single thread processes a single symbol's commands in queue order,
  **matching is deterministic per symbol**.
- **One sink writer thread per symbol** performs all file I/O, decoupled from
  matching via the result queue.
- **Shutdown ordering** guarantees no lost work: close the command queue -> join the
  matching thread (no further results published) -> stop the sink (close the result
  queue, drain remaining results, join the writer).


---

## Design Decisions & Trade-offs

- **Shard by symbol.** Independent books give parallelism and per-symbol determinism
  with no cross-symbol synchronization. Trade-off: no atomicity across symbols
  (not required here).
- **`std::map` + `std::list` book.** Chosen for correctness and simple ordered
  iteration with O(log L) level access and O(1) FIFO within a level. Trade-off:
  per-order node allocation rather than a fully preallocated / intrusive structure.
- **Bounded queues with backpressure.** Producers block when a queue is full,
  bounding memory. Queue capacity and drain batch size are the dominant latency
  knob: smaller values reduce queue residency (lower latency) at the risk of
  producers stalling; the benchmark was tuned to the point where latency dropped
  ~50% with throughput unchanged.
- **Sink on its own thread.** Keeps logging I/O off the matching hot path.
- **Self-trade prevention = cancel incoming.** An incoming order that would trade
  against the same client's resting order is cancelled instead.

---

## Build & Run

Requires a C++20 compiler and CMake >= 3.16.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Targets: `engine` (library), `engine_app` (CLI), `engine_tests`, `engine_bench`.

### Run the CLI

The CLI is self-contained under a data directory (default `data/`, overridable as
the first argument):

```
data/symbols.txt   one symbol per line ('#' comments and blank lines ignored)
data/input.csv     one command per line
data/out/          per-symbol <symbol>.log + errors.log (created if absent)
```

```bash
./build/engine_app            # uses ./data
./build/engine_app path/to/data
```

Input grammar (comma-separated):

```
NEW,<clientId>,<clientOrderId>,<symbol>,<BUY|SELL>,<LIMIT|MARKET|IOC>,<price>,<qty>
CANCEL,<clientId>,<clientOrderId>,<symbol>
MODIFY,<clientId>,<clientOrderId>,<symbol>,<price>,<qty>
```

Output lines: `TRADE <symbol> <maker> <taker> <price> <qty>`,
`MD <symbol> bid <px> <qty> ask <px> <qty>`, `REJECT <clientId> <clientOrderId>`.

### Tests

```bash
cmake -S . -B build -DOME_BUILD_TESTS=ON
cmake --build build --target engine_tests
ctest --test-dir build --output-on-failure
```

### Benchmark

Multiple producer threads submit a fixed, pre-generated workload to a shared
exchange; reports throughput and end-to-end (enqueue -> processed) latency
percentiles. Build in Release.

```bash
./build/engine_bench [producers] [orders_per_producer] [symbols]
```

Reports: orders/sec, mean, P50, P75, P90, P99, and max latency.
