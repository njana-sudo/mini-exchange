# Mini Exchange

A limit order book and matching engine written in C++17.

Orders are matched by **price-time priority** using exact integer ticks (`int64_t`), supporting limit, market, and stop-loss orders, $O(1)$ ID-indexed cancellation and priority-aware modification, configurable self-trade prevention, and per-trader realized P&L reports.

*(Note: Platforms like Zerodha are brokers that route retail orders to exchanges such as NSE/BSE. This project models the **exchange-side matching core** alongside broker-like risk limits, stop-loss orders, STP, and P&L reporting so we can study correctness, determinism, and tail latency end-to-end.)*

---

## Architecture

```
  ┌─────────────────────────────────────────────────────────┐
  │                 C++ Matching Engine                     │
  │                                                         │
  │   Client / Tests ──► Exchange ──► OrderBook (hot path)  │
  │                         │            │                  │
  │                         │            ▼                  │
  │                         │       Trade history           │
  │                         │            │                  │
  │                         ▼            ▼                  │
  │                   TraderReport   [Planned: replay log,  │
  │                   (cold path)     benchmark, gateway]   │
  └─────────────────────────────────────────────────────────┘
```

- **Hot path (`OrderBook`)**: receives validated orders, matches across integer tick levels by price then FIFO time, updates $O(1)$ ID index, and triggers stop-loss cascades.
- **Cold path (`TraderReport`)**: aggregates trades for a trader over a configurable window and computes average buy/sell prices, net position, and realized P&L.

---

## Implemented features

- **Integer tick prices (`int64_t`)** — 1 tick = ₹0.01; floats are validated (including overflow bounds before rounding) and converted at the API boundary only
- **Price-time priority matching** — best price wins; FIFO order within each price level
- **Limit, market, and stop-loss orders** — pending stops are indexed by trigger price (`std::map` descending for sell stops, ascending for buy stops) and cascade cleanly when trades move the last traded price
- **$O(1)$ cancel & priority-aware modify** — stored map + list iterators allow constant-time removal; same-price quantity cuts preserve queue position while price/size increases re-enter matching
- **Self-trade prevention (STP)** — `CANCEL_OLDEST` (cancel resting order) or `CANCEL_NEWEST` (cancel incoming remainder)
- **Deterministic timestamps & non-burning IDs** — injected `Clock` and per-exchange `uint64_t` order IDs assigned only after validation succeeds
- **Per-trader P&L reports** — buy/sell history, average prices, net position, and continuous integer-tick realized P&L (indexed per trader, accurate across time windows)
- **Append-only order journal & deterministic replay** — binary command log (`OrderJournal`) replays an entire session to reproduce bit-identical trades, timestamps, order book state, and ledgers
- **Unit & differential tests** — GoogleTest suite verifying invariants (`OrderBook::checkInvariants()`), deterministic replay, and randomized parity against a naive $O(N)$ reference book

---

## Project structure

```
mini-exchange/
├── CMakeLists.txt          # CMake build (LTO, warnings, Sanitizers, GoogleTest)
├── include/
│   ├── Types.hpp           # OrderId/Quantity/Price ticks, Clock, StpMode, validation
│   ├── Order.hpp           # Order (abstract), LimitOrder, MarketOrder, StopLossOrder
│   ├── Trade.hpp           # Immutable trade record with deterministic timestamp
│   ├── Trader.hpp          # Trader (abstract), RetailTrader, InstitutionalTrader
│   ├── TraderLedger.hpp    # O(1) continuous integer-tick position & P&L tracker
│   ├── TraderReport.hpp    # Cold-path analytics
│   ├── OrderJournal.hpp    # Binary append-only command log for deterministic replay
│   ├── OrderBook.hpp       # Matching engine + invariant checker
│   └── Exchange.hpp        # Top-level facade
├── src/
│   ├── Order.cpp
│   ├── Trade.cpp
│   ├── TraderLedger.cpp
│   ├── TraderReport.cpp
│   ├── OrderJournal.cpp
│   ├── OrderBook.cpp
│   ├── Exchange.cpp
│   └── main.cpp            # Demo CLI
├── tests/
│   └── exchange_test.cpp   # Unit tests + randomized differential tests
└── bench/                  # (Planned) paced latency & throughput benchmark harness
```

---

## Build & test

**Requirements:** CMake ≥ 3.16, g++ or clang++ (C++17), Ninja

```bash
# Release build (LTO enabled) + run unit & differential tests
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/exchange        # Windows: .\build\exchange.exe
```

**Offline build (skip fetching GoogleTest):**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build
```

**Debug + Sanitizers (ASan + UBSan on Linux/Clang; `_GLIBCXX_DEBUG` iterator checks fallback on MinGW):**
```bash
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-dbg
ctest --test-dir build-dbg --output-on-failure
```

---

## Known limitations (current stage)

- **Single symbol** — one `OrderBook` instance per `Exchange`; multi-symbol partitioning comes in Phase 3
- **No continuous SQL analytics store yet** — trade/command durability is via the binary replay journal; indexed SQL/OLAP queries are optional for the cold path later
- **Continuous matching only** — no opening/closing auction or IOC/FOK/post-only time-in-force flags yet

---

## Benchmark results

*(To be measured in Phase 3 on a dedicated harness — engine throughput and p50/p99/p99.9 latency reported separately from any network/UI layer.)*

| Configuration | Throughput (orders/sec) | p50 (μs) | p99 (μs) | p99.9 (μs) |
|---|---|---|---|---|
| Baseline (`std::map` + `std::list` + `shared_ptr`) | *TBD* | *TBD* | *TBD* | *TBD* |
| Optimized (arena pool + flat ladder + `ctz64()`) | *TBD* | *TBD* | *TBD* | *TBD* |

---

## Planned roadmap

Each step is reviewed for correctness, speed, and behaviour under load before the next begins.

### Phase 1 — Make it correct
- [x] **Integer tick prices** — `int64_t` ticks with boundary range/overflow validation
- [x] **Order cancellation and modification** — $O(1)$ cancel via stored map + list iterators; priority-preserving quantity cuts
- [x] **Self-trade prevention** — `CANCEL_OLDEST` / `CANCEL_NEWEST` modes preventing self-matches and crossed books

### Phase 2 — Prove it correct
- [x] **Unit & differential tests** (GoogleTest) — edge cases, `checkInvariants()`, and randomized differential testing against a naive $O(N)$ reference book
- [x] **Continuous trader ledger & append-only order log** — track integer-tick realized P&L continuously across windows; log every input command to disk and replay for bit-identical trades

### Phase 3 — Make it fast
- [ ] **Decouple I/O & POD hot-path structs** — replace `std::cout` with an event listener, use integer `TraderId` (`uint32_t`) and result codes instead of exceptions on the hot path
- [ ] **Benchmark harness (`bench/`)** — paced arrival rates (avoiding coordinated omission) + cancel/replace burst traces reporting throughput and **p50 / p99 / p99.9**
- [ ] **Arena pool & flat price ladder** — contiguous order pool with `uint32_t` intrusive FIFO indices, flat ID table, and flat price array with portable `ctz64()` bitmap (`__builtin_ctzll` / `_BitScanForward64` / `std::countr_zero`)
- [ ] **Multiple symbols & sharded threading** — one single-threaded `OrderBook` shard per symbol fed by `alignas(64)` lock-free SPSC/MPSC ring buffers

### Phase 4 — Make it a server
- [ ] **TCP gateway** — accept order submissions and return trade confirmations over a socket
- [ ] **Session auth & TOTP 2FA** — RFC 6238 6-digit TOTP login at the gateway edge issuing session tokens (zero hot-path auth overhead)
- [ ] **Batched WebSocket event stream** — coalesce market data / trade updates into batched frames for downstream visualization

### Phase 5 — Make it visual (demo layer)
- [ ] **Python market simulator** — synthetic order flow (Poisson arrivals, price drift) driving the TCP gateway
- [ ] **Live visualization dashboard** — real-time order book ladder, trade tape, P&L curve, and latency histogram (explicitly decoupled from engine matching latency)
