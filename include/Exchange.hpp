#pragma once
// Exchange.hpp - top-level facade that owns an OrderBook, Traders, and per-exchange OrderId generator.

#include "OrderBook.hpp"
#include "Trader.hpp"
#include "TraderReport.hpp"

#include <map>
#include <memory>
#include <string>

class Exchange {
private:
    Clock                                          clock_;
    OrderBook                                      book_;
    std::map<std::string, std::unique_ptr<Trader>> traders_;
    OrderId                                        nextOrderId_ = 1;

    void validate(const std::string& traderName, Quantity qty) const;

public:
    explicit Exchange(StpMode stpMode = StpMode::CANCEL_OLDEST,
                      Clock clock = systemClock())
        : clock_(clock), book_(stpMode, clock) {}

    void    registerTrader(std::unique_ptr<Trader> trader);
    void    setStpMode(StpMode mode) { book_.setStpMode(mode); }
    StpMode stpMode() const          { return book_.stpMode(); }

    OrderId placeOrder(const std::string& trader, Side side, Quantity qty, double price);
    OrderId placeOrder(const std::string& trader, Side side, Quantity qty);
    OrderId placeOrder(const std::string& trader, Side side, Quantity qty,
                       double triggerPrice, double limitPrice);

    void cancelOrder(const std::string& trader, OrderId orderId);
    void modifyOrder(const std::string& trader, OrderId orderId, Quantity newQty, double newPrice);

    TraderReport buildTraderReport(const std::string& name, int months = 0) const;
    void         traderReport(const std::string& name, int months = 0) const;

    void listTraders() const;

    const OrderBook& book() const { return book_; }
};
