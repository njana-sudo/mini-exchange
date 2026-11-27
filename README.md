# Mini Exchange

A limit order book and matching engine written in C++17.

Orders are matched by **price-time priority** — the standard used by real exchanges. The engine handles limit orders, market orders, and stop-loss orders; records every trade with a timestamp; and reports each trader's realized P&L on demand.

---

## How it works

```
  placeOrder()
       │
       ▼
  ┌─────────────┐    validate    ┌──────────────┐
  │  Exchange   │ ────────────►  │  OrderBook   │  hot path
  │  (facade)   │                │  (matcher)   │
  └─────────────┘                └──────┬───────┘
       │                                │ emits Trade records
       │  traderReport()                ▼
       │                         ┌──────────────┐
       └────────────────────────►│ TraderReport │  cold path
                                 │  (analytics) │
                                 └──────────────┘
```

**Hot path** (`OrderBook`): receives an order, walks the opposite side of the book by price then time, fills as much as possible, parks the remainder, and checks whether any pending stop-loss orders should activate.

**Cold path** (`TraderReport`): scans the trade log, filters by trader and time window, and computes average buy/sell prices, net position, and realized P&L using the average-cost method.

---

## Features

- **Price-time priority matching** — best price wins; ties broken by submission time
- **Limit, market, and stop-loss orders** — SL orders sit pending until the last traded price crosses the trigger, then activate as limit orders; cascades are handled
- **Trade history with timestamps** — every execution is recorded
- **Per-trader P&L reports** — quantity, price, value, net position, and realized P&L over the last *N* months

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

- [ ] **Integer prices** — replace `double` map keys with integer ticks to eliminate floating-point comparison issues; a real design decision with measurable impact
- [ ] **Order cancellation and modification**
- [ ] **Self-trade prevention**
- [ ] **Unit tests** (Google Test via CMake `FetchContent`) — price-time priority, partial fills, SL cascade, rejections
- [ ] **Benchmark** — orders/second and latency percentiles (p50, p99) on the matching loop
- [ ] **Multiple symbols**
- [ ] **Order log and replay** — write every order to a log and replay it to reproduce identical trades (determinism test)
- [ ] **TCP gateway** — accept orders over a socket so external clients can connect
