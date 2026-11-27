# Mini Exchange

A limit order book and matching engine written in C++17.

Orders are matched by **price-time priority** — the standard used by real exchanges. The engine handles limit orders, market orders, and stop-loss orders; records every trade with a timestamp; and reports each trader's realized P&L on demand.

The goal: build an engine that is **measurably fast, provably correct, and transparent** — addressing real pain points in platforms like Zerodha (outages under load, unverifiable SL execution, no real-time analytics visibility).

---

## Architecture

```
  ┌─────────────────────────────────────────────────────────┐
  │                      C++ Engine                         │
  │                                                         │
  │   Client / Simulator ──► Exchange ──► OrderBook         │
  │                               │          │              │
  │                               │          ▼              │
  │                               │       Trade log         │
  │                               │    (append-only, replay)│
  │                               │          │              │
  │                               ▼          ▼              │
  │                         TraderReport  Benchmark         │
  │                         (cold path)   (p50/p99 μs)      │
  └────────────────────┬────────────────────────────────────┘
                       │  WebSocket / TCP
          ┌────────────┴────────────┐
          │                         │
          ▼                         ▼
  ┌───────────────┐       ┌──────────────────────┐
  │ React / Live  │       │  Python analytics     │
  │ order book UI │       │  P&L curves, latency  │
  │ (hackathon    │       │  histograms, market   │
  │  demo layer)  │       │  simulator            │
  └───────────────┘       └──────────────────────┘
```

**Hot path** (`OrderBook`): receives an order, walks the opposite side by price then time, fills, parks remainder, checks stop-loss cascade. This loop is what gets benchmarked.

**Cold path** (`TraderReport`): scans the trade log on demand — never touches the hot path.

**Live layer**: after every trade, the engine pushes a small JSON event over WebSocket. The dashboard re-renders in under 16 ms. Judges see numbers change with every order.

---

## Features

- **Price-time priority matching** — best price wins; ties broken by submission time
- **Limit, market, and stop-loss orders** — SL orders sit pending until the last traded price crosses the trigger; cascades handled
- **O(1) order cancellation and modification** — cancel resting or pending SL orders by ID with ownership checks; in-place quantity cuts preserve FIFO time priority while price/size increases re-enter matching
- **Self-trade prevention (STP)** — configurable `CANCEL_OLDEST` (cancel passive resting order) or `CANCEL_NEWEST` (cancel incoming remainder) prevents a trader from matching against themselves or crossing the book
- **Trade history with timestamps** — every execution recorded in an append-only log
- **Per-trader P&L reports** — quantity, price, value, net position, realized P&L over *N* months
- **Deterministic replay** — reprocessing the order log produces bit-identical trades (provable correctness)

---

## Project structure

```
mini-exchange/
├── CMakeLists.txt
├── include/
│   ├── Types.hpp           # Side enum, formatDate, InvalidOrderException
│   ├── Order.hpp           # Order (abstract), LimitOrder, MarketOrder, StopLossOrder
│   ├── Trade.hpp           # Immutable trade record
│   ├── Trader.hpp          # Trader (abstract), RetailTrader, InstitutionalTrader
│   ├── TraderReport.hpp    # Cold-path analytics
│   ├── OrderBook.hpp       # Hot-path matching engine
│   └── Exchange.hpp        # Top-level facade
└── src/
    ├── Order.cpp
    ├── Trade.cpp
    ├── TraderReport.cpp
    ├── OrderBook.cpp
    ├── Exchange.cpp
    └── main.cpp
```

---

## Build

**Requirements:** CMake ≥ 3.16, g++ or clang++ (C++17), Ninja

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/exchange        # Windows: .\build\exchange.exe
```

**Debug build:**
```bash
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg
```

---

## Roadmap

Each step is reviewed for correctness, speed, and behaviour under load before the next begins.

### Phase 1 — Make it correct
- [x] **Integer tick prices** — replace `double` map keys with `int64_t` ticks; eliminates floating-point comparison bugs and makes the order book a true integer structure. Real exchanges do not use floats.
- [x] **Order cancellation and modification** — `O(1)` lookup by order ID via a parallel `unordered_map` with iterator-stable `std::list` price queues; ownership-checked and priority-aware.
- [x] **Self-trade prevention** — configurable `CANCEL_OLDEST` / `CANCEL_NEWEST` modes; prevents self-matches and crossed books without adding overhead to non-crossing orders.

### Phase 2 — Prove it correct
- [x] **Unit tests** (Google Test via CMake `FetchContent`) — 19 tests covering integer tick drift, price-time priority, partial fills, SL cascade, O(1) cancel/modify, STP modes, and P&L reports.
- [ ] **Append-only order log** — every submitted order written to a binary log before matching. Replaying the log must produce bit-identical trades (determinism test run in CI).

### Phase 3 — Make it fast
- [ ] **Decouple I/O & POD hot-path structs** — remove `std::cout` and heap-allocated `std::string`s from `OrderBook` / `Trade`; use integer `TraderId` (`uint32_t`) on the hot path and keep string names at the cold boundary.
- [ ] **Benchmark harness** — pace orders at fixed arrival rates (avoiding coordinated omission) plus adversarial cancel/replace burst traces; report throughput (orders/sec) and tail latency (**p50, p99, p99.9** in microseconds) on this machine.
- [ ] **Arena pool & flat price levels** — replace `std::shared_ptr` + `std::list` + `std::map` with a contiguous order pool using `uint32_t` intrusive FIFO indices, an open-addressed/flat ID lookup table, and a flat price array with bitmask (`__builtin_ctzll`) for $O(1)$ best-bid/ask jumps.
- [ ] **Multiple symbols & sharded threading** — one lock-free single-threaded `OrderBook` shard per symbol, fed by cache-line-aligned (`alignas(64)`) SPSC/MPSC ring buffers with acquire/release memory ordering.

### Phase 4 — Make it a server
- [ ] **TCP gateway** — accept order submissions and return trade confirmations over a socket (Boost.Asio / `epoll` on Linux).
- [ ] **Batched WebSocket event stream** — coalesce trade/book updates into batched JSON frames (avoiding per-trade wakeups under burst load) and measure end-to-end match-to-browser latency on the dashboard.

### Phase 5 — Make it visual (hackathon demo layer)
- [ ] **Python market simulator** — generates realistic synthetic order flow (Poisson arrivals, Gaussian price drift) at configurable rates; connects to the TCP gateway. Used to stress-test the engine and populate the dashboard with live data.
- [ ] **Live dashboard** (React + recharts or plain canvas) — panels updating in real time with every trade event:
  - Order book depth (bid/ask ladder)
  - Trade tape (last N executions)
  - Per-trader P&L curve
  - Throughput gauge (orders/sec)
  - Latency histogram (p50 / p99)
- [ ] **Stress demo** — at the hackathon, run the Python simulator at 10 k orders/sec and show the dashboard updating live. Show the latency histogram staying flat. Show the replay test passing after the run.
