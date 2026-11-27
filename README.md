# Mini Stock Exchange (C++17)

A small order-matching engine that demonstrates core object-oriented programming concepts in C++.

## What it does
- Traders place **limit**, **market**, and **stop-loss** orders (buy or sell).
- Orders are matched by **price-time priority**: best price first, and the oldest order first at the same price.
- **Stop-loss orders** stay pending until the last traded price crosses a trigger level, then activate as limit orders — just like Zerodha's SL orders.  Triggered stop-losses can cascade (one trigger causes trades that trigger another).
- Unmatched limit orders rest in the order book; trades are recorded and printed.
- **Trader reports** (`traderReport(name, months)`) show the quantity, price, and value of all stocks bought and sold by a user over the last *N* months, along with net position and realized P&L.
- Invalid orders (bad price, unknown trader, size above the trader's limit) are rejected with exceptions.

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
```bash
g++ -std=c++17 -Wall -Wextra -o exchange main.cpp
./exchange        # on Windows: exchange.exe
```

## Possible next steps
- Order cancellation and modification
- Multiple symbols
- Unit tests and a performance benchmark
- Split into headers and source files
