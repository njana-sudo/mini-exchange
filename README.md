# Mini Stock Exchange (C++17)

A stock exchange matching engine demonstrating core OOP and software-engineering concepts in C++17.

## Features
- **Limit, Market, and Stop-Loss orders** matched by price-time priority (best price first, then oldest first).
- **Stop-loss cascade:** a triggered SL can cause trades that trigger further SL orders.
- **Trader reports:** per-trader buy/sell history, net position, and realized P&L over the last *N* months (average-cost method, like Zerodha Console).
- **Validation:** unknown traders, oversized orders, negative prices all throw typed exceptions.

## Project structure

```
mini-exchange/
├── CMakeLists.txt          # CMake build (Release / Debug, static lib + executable)
├── include/
│   ├── Types.hpp           # Side enum, formatDate helper, InvalidOrderException
│   ├── Order.hpp           # Order (abstract), LimitOrder, MarketOrder, StopLossOrder
│   ├── Trade.hpp           # Immutable trade record with timestamp
│   ├── Trader.hpp          # Trader (abstract), RetailTrader, InstitutionalTrader
│   ├── TraderReport.hpp    # Cold-path analytics: P&L and trade history
│   ├── OrderBook.hpp       # Hot-path matching engine
│   └── Exchange.hpp        # Top-level facade
├── src/
│   ├── Order.cpp
│   ├── Trade.cpp
│   ├── TraderReport.cpp
│   ├── OrderBook.cpp
│   ├── Exchange.cpp
│   └── main.cpp            # Demo / entry point
└── README.md
```

## Class relationships

```mermaid
classDiagram
    direction TB

    class Order {
        <<abstract>>
        -static int nextId
        -int id_
        -string trader_
        -Side side_
        -int quantity_
        +id() int
        +trader() string
        +side() Side
        +quantity() int
        +isFilled() bool
        +fill(int qty)
        +isMarket() bool*
        +price() double*
        +type() string*
        +info() string
    }

    class LimitOrder {
        -double price_
        +isMarket() bool
        +price() double
        +type() string
    }

    class MarketOrder {
        +isMarket() bool
        +price() double
        +type() string
    }

    class StopLossOrder {
        -double triggerPrice_
        -double limitPrice_
        -bool triggered_
        +triggerPrice() double
        +isTriggered() bool
        +activate()
        +shouldTrigger(double) bool
        +info() string
    }

    class Trader {
        <<abstract>>
        #string name_
        +name() string
        +maxOrderSize() int*
        +category() string*
    }

    class RetailTrader {
        +maxOrderSize() int
        +category() string
    }

    class InstitutionalTrader {
        +maxOrderSize() int
        +category() string
    }

    class Trade {
        -int buyOrderId_
        -int sellOrderId_
        -string buyer_
        -string seller_
        -double price_
        -int quantity_
        -time_t timestamp_
        +buyOrderId() int
        +sellOrderId() int
        +buyer() string
        +seller() string
        +price() double
        +quantity() int
        +timestamp() time_t
    }

    class TraderReport {
        -string name_
        -int months_
        -vector~Entry~ buys_
        -vector~Entry~ sells_
        +addBuy(int, double, int, time_t)
        +addSell(int, double, int, time_t)
        +totalBought() int
        +totalSold() int
        +avgBuyPrice() double
        +avgSellPrice() double
        +netPosition() int
        +realizedPnL() double
    }

    class OrderBook {
        -map bids_
        -map asks_
        -vector~Trade~ trades_
        -vector~StopLossOrder~ stopOrders_
        -match(Order, Book)
        -checkStopOrders()
        +submit(Order)
        +trades() vector~Trade~
        +print()
    }

    class Exchange {
        -OrderBook book_
        -map~Trader~ traders_
        -validate(string, int)
        +registerTrader(Trader)
        +placeOrder() ×3 overloads
        +traderReport(string, int)
        +listTraders()
        +book() OrderBook
    }

    class InvalidOrderException {
        +what() string
    }

    Order <|-- LimitOrder : inherits
    Order <|-- MarketOrder : inherits
    Order <|-- StopLossOrder : inherits
    Trader <|-- RetailTrader : inherits
    Trader <|-- InstitutionalTrader : inherits
    runtime_error <|-- InvalidOrderException : inherits

    Exchange *-- OrderBook : composition
    Exchange *-- Trader : composition
    Exchange ..> TraderReport : creates
    OrderBook *-- Trade : composition
    OrderBook o-- Order : aggregation
    OrderBook o-- StopLossOrder : aggregation
```

## OOP concepts used
| Concept | Where |
|---|---|
| Encapsulation | `Order`, `Trade`, `OrderBook`, `TraderReport`: private data with const getters |
| Abstraction | Abstract classes `Order` and `Trader` (pure virtual functions) |
| Inheritance | `LimitOrder`/`MarketOrder`/`StopLossOrder` from `Order`; `RetailTrader`/`InstitutionalTrader` from `Trader` |
| Polymorphism (runtime) | Virtual functions called via base pointers (`type()`, `info()`, `maxOrderSize()`) |
| Polymorphism (compile time) | Function overloading (`placeOrder` × 3), operator overloading (`<<`), templates (`match`, `printAll`) |
| Constructors / destructors | Constructor overloading, initializer lists, virtual destructors |
| Static members | `Order::nextId` |
| Friend functions | `operator<<` for `Order`, `Trade`, and `TraderReport` |
| Composition | `Exchange` owns an `OrderBook`, its traders, and builds `TraderReport`s |
| Exception handling | `InvalidOrderException` |
| Dynamic cast | `dynamic_pointer_cast` to identify `StopLossOrder` at runtime |
| Nested struct | `Entry` inside `TraderReport` groups trade line-item fields |

## Build and run

### Prerequisites
- CMake ≥ 3.16
- g++ (C++17) or clang++
- Ninja (recommended) or Make

### Configure and build (Release)
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/exchange        # Windows: .\build\exchange.exe
```

### Debug build (with sanitizers-ready flags)
```bash
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg
```

### Legacy single-file build (still works)
```bash
g++ -std=c++17 -Wall -Wextra -o exchange main.cpp   # from v1.0-single-file tag
```

## Possible next steps
- Order cancellation and modification
- Multiple symbols (`RELIANCE`, `TCS`, …)
- Unit tests (Google Test via CMake `FetchContent`)
- Performance benchmark (flat arrays, fixed-point prices)
- WebSocket/HTTP API layer (Python or Go client)
