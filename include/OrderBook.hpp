#pragma once
// OrderBook.hpp - HOT PATH: price-time-priority matching engine.
// Map keys are Price (int64_t ticks) — no floating-point comparison.

#include "Order.hpp"
#include "Trade.hpp"

#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

class OrderBook {
private:
    // Bids: highest tick first (std::greater).
    // Asks: lowest tick first (default).
    // Using integer Price keys eliminates all ULP/floating-point ambiguity.
    std::map<Price, std::deque<std::shared_ptr<Order>>, std::greater<Price>> bids_;
    std::map<Price, std::deque<std::shared_ptr<Order>>>                       asks_;

    std::vector<Trade>                          trades_;
    std::vector<std::shared_ptr<StopLossOrder>> stopOrders_;

    template <typename Book>
    void match(const std::shared_ptr<Order>& incoming, Book& opposite);

    void checkStopOrders();

public:
    void submit(const std::shared_ptr<Order>& order);

    const std::vector<Trade>& trades() const { return trades_; }

    void print() const;
};
