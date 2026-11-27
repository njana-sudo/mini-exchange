#pragma once
// OrderBook.hpp - HOT PATH: price-time-priority matching engine.
//                 Receives orders, matches them, parks resting orders, and
//                 triggers stop-loss orders after each trade.
//
// OOP tags: [COMPOSITION] [POLYMORPHISM (template)] [DYNAMIC_CAST]

#include "Order.hpp"
#include "Trade.hpp"

#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

// ======================= OrderBook =======================

// [COMPOSITION] OrderBook owns its bids, asks, trades, and pending stop orders.
class OrderBook {
private:
    // buy side: highest price first (std::greater); sell side: lowest price first (default).
    // Each price level is a FIFO deque preserving time priority.
    std::map<double, std::deque<std::shared_ptr<Order>>, std::greater<double>> bids_;
    std::map<double, std::deque<std::shared_ptr<Order>>>                       asks_;

    std::vector<Trade>                       trades_;
    std::vector<std::shared_ptr<StopLossOrder>> stopOrders_;   // pending SL orders

    // [POLYMORPHISM] compile-time template: same matching logic for both sides of the book
    template <typename Book>
    void match(const std::shared_ptr<Order>& incoming, Book& opposite);

    // [DYNAMIC_CAST] checks every pending SL after each trade; cascades if a triggered SL
    // causes further trades that trigger more SL orders.
    void checkStopOrders();

public:
    void submit(const std::shared_ptr<Order>& order);

    const std::vector<Trade>& trades() const { return trades_; }

    void print() const;
};
