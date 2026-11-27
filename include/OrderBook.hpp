#pragma once
// OrderBook.hpp - HOT PATH: price-time-priority matching engine.
//
// Features:
//   - Integer tick prices (Price = int64_t)
//   - True O(1) cancel via stored map + list iterators in orderIndex_
//   - Stop-loss books ordered by trigger price then FIFO time priority (O(log S + k))
//   - Configurable Self-Trade Prevention (STP)
//   - Injected deterministic Clock for reproducible Trade timestamps

#include "Order.hpp"
#include "Trade.hpp"

#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class OrderBook {
private:
    using OrderQueue  = std::list<std::shared_ptr<Order>>;
    using StopQueue   = std::list<std::shared_ptr<StopLossOrder>>;
    using BidMap      = std::map<Price, OrderQueue, std::greater<Price>>;
    using AskMap      = std::map<Price, OrderQueue>;
    using StopSellMap = std::map<Price, StopQueue, std::greater<Price>>;  // highest trigger first
    using StopBuyMap  = std::map<Price, StopQueue>;                       // lowest trigger first

    enum class Bucket { BID, ASK, STOP_BUY, STOP_SELL };

    struct OrderLocation {
        Bucket                bucket = Bucket::BID;
        BidMap::iterator      bidLevelIt;
        AskMap::iterator      askLevelIt;
        StopBuyMap::iterator  stopBuyLevelIt;
        StopSellMap::iterator stopSellLevelIt;
        OrderQueue::iterator  queueIt;
        StopQueue::iterator   stopQueueIt;
    };

    BidMap      bids_;
    AskMap      asks_;
    StopBuyMap  stopBuys_;
    StopSellMap stopSells_;

    std::vector<Trade>                         trades_;
    std::unordered_map<OrderId, OrderLocation> orderIndex_;
    std::optional<Price>                       lastTradePrice_;
    StpMode                                    stpMode_;
    Clock                                      clock_;

    template <typename Book>
    bool match(const std::shared_ptr<Order>& incoming, Book& opposite, std::time_t now);

    void parkOrder(const std::shared_ptr<Order>& order);
    void parkStopOrder(const std::shared_ptr<StopLossOrder>& sl);
    void executeOrder(const std::shared_ptr<Order>& order, std::time_t now);
    void checkStopOrders(std::time_t now);

    std::shared_ptr<Order> removeOrder(OrderId orderId, const std::string& trader);

public:
    explicit OrderBook(StpMode stpMode = StpMode::CANCEL_OLDEST,
                       Clock clock = systemClock())
        : stpMode_(stpMode), clock_(std::move(clock)) {}

    StpMode stpMode() const { return stpMode_; }
    void    setStpMode(StpMode mode) { stpMode_ = mode; }

    void submit(const std::shared_ptr<Order>& order);
    void cancel(OrderId orderId, const std::string& trader);
    void modify(OrderId orderId, const std::string& trader, Quantity newQty, Price newPrice);

    bool                       hasOrder(OrderId orderId) const { return orderIndex_.find(orderId) != orderIndex_.end(); }
    size_t                     openOrderCount() const { return orderIndex_.size(); }
    Quantity                   remainingQuantity(OrderId orderId) const;
    std::optional<Price>       bestBid() const;
    std::optional<Price>       bestAsk() const;
    std::optional<Price>       lastTradePrice() const { return lastTradePrice_; }
    const std::vector<Trade>&  trades() const { return trades_; }

    void print() const;
};
