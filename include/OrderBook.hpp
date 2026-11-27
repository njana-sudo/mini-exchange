#pragma once
// OrderBook.hpp - HOT PATH: price-time-priority matching engine.
// Map keys are Price (int64_t ticks) — no floating-point comparison.
// Each price level uses std::list so iterators remain valid across inserts/erases,
// enabling O(1) lookup, cancellation, and modification via orderIndex_.

#include "Order.hpp"
#include "Trade.hpp"

#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class OrderBook {
private:
    using OrderQueue = std::list<std::shared_ptr<Order>>;
    using StopQueue  = std::list<std::shared_ptr<StopLossOrder>>;

    struct OrderLocation {
        bool                 isStopLoss = false;
        Side                 side       = Side::BUY;
        Price                price      = 0;
        OrderQueue::iterator bookIt;
        StopQueue::iterator  stopIt;
    };

    // Bids: highest tick first (std::greater).
    // Asks: lowest tick first (default).
    // Using integer Price keys eliminates all ULP/floating-point ambiguity.
    std::map<Price, OrderQueue, std::greater<Price>> bids_;
    std::map<Price, OrderQueue>                      asks_;

    std::vector<Trade>                       trades_;
    StopQueue                                stopOrders_;
    std::unordered_map<int, OrderLocation>   orderIndex_;  // O(1) lookup by orderId

    template <typename Book>
    void match(const std::shared_ptr<Order>& incoming, Book& opposite);

    void parkOrder(const std::shared_ptr<Order>& order);
    void executeOrder(const std::shared_ptr<Order>& order);
    void checkStopOrders();

    // Remove an order from its resting book level or pending SL queue and from orderIndex_.
    std::shared_ptr<Order> removeOrder(int orderId, const std::string& trader);

public:
    void submit(const std::shared_ptr<Order>& order);
    void cancel(int orderId, const std::string& trader);
    void modify(int orderId, const std::string& trader, int newQty, Price newPrice);

    bool hasOrder(int orderId) const { return orderIndex_.find(orderId) != orderIndex_.end(); }
    const std::vector<Trade>& trades() const { return trades_; }

    void print() const;
};
