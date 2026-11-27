#pragma once
// Exchange.hpp - top-level facade that owns an OrderBook and all registered Traders.
//               Validates orders before routing them to the book, and builds TraderReports
//               on demand.
//
// OOP tags: [COMPOSITION] [POLYMORPHISM (overloading)] [ENCAPSULATION]

#include "OrderBook.hpp"
#include "Trader.hpp"
#include "TraderReport.hpp"

#include <map>
#include <memory>
#include <string>

// ======================= Exchange =======================

// [COMPOSITION] Exchange "has an" OrderBook and "has" Traders (unique ownership).
class Exchange {
private:
    OrderBook book_;
    std::map<std::string, std::unique_ptr<Trader>> traders_;

    void validate(const std::string& traderName, int qty) const;

public:
    void registerTrader(std::unique_ptr<Trader> trader);

    // [POLYMORPHISM] function overloading — same name, three distinct signatures
    void placeOrder(const std::string& trader, Side side, int qty, double price);               // limit
    void placeOrder(const std::string& trader, Side side, int qty);                            // market
    void placeOrder(const std::string& trader, Side side, int qty,                             // stop-loss
                    double triggerPrice, double limitPrice);

    // [COMPOSITION] builds a TraderReport (cold path) over the last `months` (0 = all time)
    void traderReport(const std::string& name, int months = 0) const;

    void listTraders() const;

    const OrderBook& book() const { return book_; }
};
