// OrderBook.cpp - hot-path matching engine implementation

#include "OrderBook.hpp"
#include "Types.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
template <typename Map, typename QueueIt>
void eraseFromLevel(Map& m, typename Map::iterator levelIt, QueueIt qIt) {
    levelIt->second.erase(qIt);
    if (levelIt->second.empty()) m.erase(levelIt);
}
}  // namespace

// ======================= match (private template) =======================

template <typename Book>
bool OrderBook::match(const std::shared_ptr<Order>& incoming, Book& opposite, std::time_t now) {
    while (!incoming->isFilled() && !opposite.empty()) {
        auto  level      = opposite.begin();
        Price levelPrice = level->first;

        if (!incoming->isMarket()) {
            bool crosses = (incoming->side() == Side::BUY)
                           ? incoming->price() >= levelPrice
                           : incoming->price() <= levelPrice;
            if (!crosses) break;
        }

        auto& queue   = level->second;
        auto  resting = queue.front();

        if (resting->trader() == incoming->trader()) {
            if (stpMode_ == StpMode::CANCEL_OLDEST) {
                std::cout << "  >>> STP: cancelled resting #" << resting->id()
                          << " (self-trade prevented for " << incoming->trader() << ")\n";
                orderIndex_.erase(resting->id());
                queue.pop_front();
                if (queue.empty()) opposite.erase(level);
                continue;
            }
            std::cout << "  >>> STP: cancelled incoming #" << incoming->id()
                      << " remainder (" << incoming->quantity()
                      << " shares, self-trade prevented for " << incoming->trader() << ")\n";
            return true;
        }

        Quantity qty = std::min(incoming->quantity(), resting->quantity());
        incoming->fill(qty);
        resting ->fill(qty);

        const Order& buy  = (incoming->side() == Side::BUY)  ? *incoming : *resting;
        const Order& sell = (incoming->side() == Side::SELL) ? *incoming : *resting;
        trades_.emplace_back(buy.id(), sell.id(),
                             buy.trader(), sell.trader(),
                             levelPrice, qty, now);
        lastTradePrice_ = levelPrice;

        if (resting->isFilled()) {
            orderIndex_.erase(resting->id());
            queue.pop_front();
        }
        if (queue.empty()) opposite.erase(level);
    }
    return false;
}

template bool OrderBook::match(const std::shared_ptr<Order>&, OrderBook::BidMap&, std::time_t);
template bool OrderBook::match(const std::shared_ptr<Order>&, OrderBook::AskMap&, std::time_t);

// ======================= parkOrder / parkStopOrder / executeOrder =======================

void OrderBook::parkOrder(const std::shared_ptr<Order>& order) {
    Price p = order->price();
    OrderLocation loc;
    if (order->side() == Side::BUY) {
        auto [levelIt, inserted] = bids_.try_emplace(p);
        (void)inserted;
        levelIt->second.push_back(order);
        loc.bucket     = Bucket::BID;
        loc.bidLevelIt = levelIt;
        loc.queueIt    = std::prev(levelIt->second.end());
    } else {
        auto [levelIt, inserted] = asks_.try_emplace(p);
        (void)inserted;
        levelIt->second.push_back(order);
        loc.bucket     = Bucket::ASK;
        loc.askLevelIt = levelIt;
        loc.queueIt    = std::prev(levelIt->second.end());
    }
    orderIndex_[order->id()] = loc;
}

void OrderBook::parkStopOrder(const std::shared_ptr<StopLossOrder>& sl) {
    Price trig = sl->triggerPrice();
    OrderLocation loc;
    if (sl->side() == Side::BUY) {
        auto [levelIt, inserted] = stopBuys_.try_emplace(trig);
        (void)inserted;
        levelIt->second.push_back(sl);
        loc.bucket         = Bucket::STOP_BUY;
        loc.stopBuyLevelIt = levelIt;
        loc.stopQueueIt    = std::prev(levelIt->second.end());
    } else {
        auto [levelIt, inserted] = stopSells_.try_emplace(trig);
        (void)inserted;
        levelIt->second.push_back(sl);
        loc.bucket          = Bucket::STOP_SELL;
        loc.stopSellLevelIt = levelIt;
        loc.stopQueueIt     = std::prev(levelIt->second.end());
    }
    orderIndex_[sl->id()] = loc;
}

void OrderBook::executeOrder(const std::shared_ptr<Order>& order, std::time_t now) {
    bool stpCancelled = (order->side() == Side::BUY)
                        ? match(order, asks_, now)
                        : match(order, bids_, now);

    if (stpCancelled) return;

    if (!order->isFilled()) {
        if (order->isMarket()) {
            std::cout << "  (market order #" << order->id()
                      << " partly unfilled: " << order->quantity()
                      << " left, no liquidity)\n";
        } else {
            parkOrder(order);
        }
    }
}

// ======================= checkStopOrders =======================
// Called only when new trades occurred. Fires stops in trigger-price then FIFO order:
//   - Sell stops: highest trigger price first (stopSells_.begin())
//   - Buy stops:  lowest trigger price first  (stopBuys_.begin())

void OrderBook::checkStopOrders(std::time_t now) {
    if (!lastTradePrice_.has_value()) return;

    while (true) {
        Price lastPrice = *lastTradePrice_;
        std::shared_ptr<StopLossOrder> sl;

        if (!stopSells_.empty() && stopSells_.begin()->first >= lastPrice) {
            auto levelIt = stopSells_.begin();
            sl = levelIt->second.front();
            orderIndex_.erase(sl->id());
            levelIt->second.pop_front();
            if (levelIt->second.empty()) stopSells_.erase(levelIt);
        } else if (!stopBuys_.empty() && stopBuys_.begin()->first <= lastPrice) {
            auto levelIt = stopBuys_.begin();
            sl = levelIt->second.front();
            orderIndex_.erase(sl->id());
            levelIt->second.pop_front();
            if (levelIt->second.empty()) stopBuys_.erase(levelIt);
        } else {
            break;
        }

        sl->activate();
        std::cout << "  >>> SL triggered: " << *sl << "\n";
        executeOrder(sl, now);
    }
}

// ======================= removeOrder =======================

std::shared_ptr<Order> OrderBook::removeOrder(OrderId orderId, const std::string& trader) {
    auto idxIt = orderIndex_.find(orderId);
    if (idxIt == orderIndex_.end())
        throw InvalidOrderException("Order #" + std::to_string(orderId) +
                                    " not found (already filled or cancelled)");

    const OrderLocation& loc = idxIt->second;
    std::shared_ptr<Order> order;
    switch (loc.bucket) {
        case Bucket::BID:       order = *loc.queueIt; break;
        case Bucket::ASK:       order = *loc.queueIt; break;
        case Bucket::STOP_BUY:  order = *loc.stopQueueIt; break;
        case Bucket::STOP_SELL: order = *loc.stopQueueIt; break;
    }

    if (order->trader() != trader)
        throw InvalidOrderException(trader + " cannot modify/cancel order #" +
                                    std::to_string(orderId) + " owned by " + order->trader());

    switch (loc.bucket) {
        case Bucket::BID:       eraseFromLevel(bids_,      loc.bidLevelIt,      loc.queueIt);     break;
        case Bucket::ASK:       eraseFromLevel(asks_,      loc.askLevelIt,      loc.queueIt);     break;
        case Bucket::STOP_BUY:  eraseFromLevel(stopBuys_,  loc.stopBuyLevelIt,  loc.stopQueueIt); break;
        case Bucket::STOP_SELL: eraseFromLevel(stopSells_, loc.stopSellLevelIt, loc.stopQueueIt); break;
    }

    orderIndex_.erase(idxIt);
    return order;
}

// ======================= submit / cancel / modify =======================

void OrderBook::submit(const std::shared_ptr<Order>& order) {
    submit(order, clock_());
}

void OrderBook::submit(const std::shared_ptr<Order>& order, std::time_t now) {
    auto sl = std::dynamic_pointer_cast<StopLossOrder>(order);
    if (sl && !sl->isTriggered()) {
        if (lastTradePrice_.has_value() && sl->shouldTrigger(*lastTradePrice_)) {
            throw InvalidOrderException(
                "Stop-loss trigger price is already crossed against last traded price");
        }
        parkStopOrder(sl);
        return;
    }

    size_t tradesBefore = trades_.size();
    executeOrder(order, now);
    if (trades_.size() > tradesBefore) {
        checkStopOrders(now);
    }
}

void OrderBook::cancel(OrderId orderId, const std::string& trader) {
    removeOrder(orderId, trader);
}

void OrderBook::modify(OrderId orderId, const std::string& trader, Quantity newQty, Price newPrice) {
    modify(orderId, trader, newQty, newPrice, clock_());
}

void OrderBook::modify(OrderId orderId, const std::string& trader,
                       Quantity newQty, Price newPrice, std::time_t now) {
    if (newQty <= 0)   throw InvalidOrderException("Quantity must be positive");
    if (newPrice <= 0) throw InvalidOrderException("Price must be positive");

    auto idxIt = orderIndex_.find(orderId);
    if (idxIt == orderIndex_.end())
        throw InvalidOrderException("Order #" + std::to_string(orderId) +
                                    " not found (already filled or cancelled)");

    const OrderLocation& loc = idxIt->second;
    if (loc.bucket == Bucket::STOP_BUY || loc.bucket == Bucket::STOP_SELL) {
        auto sl = *loc.stopQueueIt;
        if (sl->trader() != trader)
            throw InvalidOrderException(trader + " cannot modify order #" +
                                        std::to_string(orderId) + " owned by " + sl->trader());
        throw InvalidOrderException("Cannot modify pending stop-loss order #" +
                                    std::to_string(orderId) + "; cancel and re-place");
    }

    auto order = *loc.queueIt;
    if (order->trader() != trader)
        throw InvalidOrderException(trader + " cannot modify order #" +
                                    std::to_string(orderId) + " owned by " + order->trader());

    Price currentPrice = (loc.bucket == Bucket::BID)
                         ? loc.bidLevelIt->first
                         : loc.askLevelIt->first;

    // Size reduction at the same price preserves FIFO time priority in O(1)
    if (newPrice == currentPrice && newQty <= order->quantity()) {
        order->setQuantity(newQty);
        return;
    }

    // Price change or quantity increase loses time priority and re-enters matching
    removeOrder(orderId, trader);
    order->setQuantity(newQty);
    order->setPrice(newPrice);

    size_t tradesBefore = trades_.size();
    executeOrder(order, now);
    if (trades_.size() > tradesBefore) {
        checkStopOrders(now);
    }
}

// ======================= queries =======================

Quantity OrderBook::remainingQuantity(OrderId orderId) const {
    auto it = orderIndex_.find(orderId);
    if (it == orderIndex_.end()) return 0;
    const OrderLocation& loc = it->second;
    if (loc.bucket == Bucket::STOP_BUY || loc.bucket == Bucket::STOP_SELL)
        return (*loc.stopQueueIt)->quantity();
    return (*loc.queueIt)->quantity();
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

// ======================= checkInvariants =======================

void OrderBook::checkInvariants() const {
    if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first) {
        throw std::logic_error("Invariant violated: crossed book");
    }

    size_t indexedCount = 0;

    auto verifyLimitSide = [&](const auto& book, Bucket expectedBucket, Side expectedSide) {
        for (const auto& [lvlPrice, queue] : book) {
            if (queue.empty())
                throw std::logic_error("Invariant violated: empty limit price level");
            for (auto qIt = queue.begin(); qIt != queue.end(); ++qIt) {
                const auto& o = *qIt;
                if (!o || o->quantity() <= 0 || o->isMarket())
                    throw std::logic_error("Invariant violated: invalid resting order");
                if (o->side() != expectedSide || o->price() != lvlPrice)
                    throw std::logic_error("Invariant violated: resting order side/price mismatch");
                auto idxIt = orderIndex_.find(o->id());
                if (idxIt == orderIndex_.end() ||
                    idxIt->second.bucket != expectedBucket ||
                    idxIt->second.queueIt != qIt) {
                    throw std::logic_error("Invariant violated: orderIndex_ out of sync with limit book");
                }
                ++indexedCount;
            }
        }
    };

    auto verifyStopSide = [&](const auto& book, Bucket expectedBucket, Side expectedSide) {
        for (const auto& [trigPrice, queue] : book) {
            if (queue.empty())
                throw std::logic_error("Invariant violated: empty stop trigger level");
            for (auto qIt = queue.begin(); qIt != queue.end(); ++qIt) {
                const auto& sl = *qIt;
                if (!sl || sl->quantity() <= 0 || sl->isTriggered())
                    throw std::logic_error("Invariant violated: invalid pending stop-loss");
                if (sl->side() != expectedSide || sl->triggerPrice() != trigPrice)
                    throw std::logic_error("Invariant violated: stop-loss side/trigger mismatch");
                auto idxIt = orderIndex_.find(sl->id());
                if (idxIt == orderIndex_.end() ||
                    idxIt->second.bucket != expectedBucket ||
                    idxIt->second.stopQueueIt != qIt) {
                    throw std::logic_error("Invariant violated: orderIndex_ out of sync with stop book");
                }
                ++indexedCount;
            }
        }
    };

    verifyLimitSide(bids_,      Bucket::BID,       Side::BUY);
    verifyLimitSide(asks_,      Bucket::ASK,       Side::SELL);
    verifyStopSide(stopBuys_,   Bucket::STOP_BUY,  Side::BUY);
    verifyStopSide(stopSells_,  Bucket::STOP_SELL, Side::SELL);

    if (indexedCount != orderIndex_.size()) {
        throw std::logic_error("Invariant violated: orphan entries in orderIndex_");
    }
}

// ======================= print =======================

void OrderBook::print() const {
    std::cout << "---- ORDER BOOK ----\n";
    std::cout << "SELLS (lowest first)\n";
    for (const auto& [price, queue] : asks_)
        for (const auto& o : queue)
            std::cout << "  " << *o << "\n";
    std::cout << "BUYS (highest first)\n";
    for (const auto& [price, queue] : bids_)
        for (const auto& o : queue)
            std::cout << "  " << *o << "\n";
    if (!stopSells_.empty() || !stopBuys_.empty()) {
        std::cout << "STOP-LOSS (pending)\n";
        for (const auto& [trig, queue] : stopSells_)
            for (const auto& o : queue) std::cout << "  " << *o << "\n";
        for (const auto& [trig, queue] : stopBuys_)
            for (const auto& o : queue) std::cout << "  " << *o << "\n";
    }
    std::cout << "--------------------\n";
}
