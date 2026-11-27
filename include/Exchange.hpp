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
    explicit Exchange(StpMode stpMode = StpMode::CANCEL_OLDEST) : book_(stpMode) {}

    void registerTrader(std::unique_ptr<Trader> trader);
    void setStpMode(StpMode mode) { book_.setStpMode(mode); }
    StpMode stpMode() const       { return book_.stpMode(); }

    // [POLYMORPHISM] function overloading — same name, three distinct signatures
    // Returns the assigned order ID so callers can cancel or modify the order later.
    int placeOrder(const std::string& trader, Side side, int qty, double price);               // limit
    int placeOrder(const std::string& trader, Side side, int qty);                            // market
    int placeOrder(const std::string& trader, Side side, int qty,                             // stop-loss
                   double triggerPrice, double limitPrice);

    void cancelOrder(const std::string& trader, int orderId);
    void modifyOrder(const std::string& trader, int orderId, int newQty, double newPrice);

    // [COMPOSITION] builds a TraderReport (cold path) over the last `months` (0 = all time)
    void traderReport(const std::string& name, int months = 0) const;

    void listTraders() const;

    const OrderBook& book() const { return book_; }
};
