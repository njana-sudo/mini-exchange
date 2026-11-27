// OrderBook.cpp - hot-path matching engine implementation

#include "OrderBook.hpp"
#include "Types.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>

// ======================= match (private template) =======================

template <typename Book>
void OrderBook::match(const std::shared_ptr<Order>& incoming, Book& opposite) {
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
        int   qty     = std::min(incoming->quantity(), resting->quantity());
        incoming->fill(qty);
        resting ->fill(qty);

        const Order& buy  = (incoming->side() == Side::BUY)  ? *incoming : *resting;
        const Order& sell = (incoming->side() == Side::SELL) ? *incoming : *resting;
        trades_.emplace_back(buy.id(), sell.id(),
                             buy.trader(), sell.trader(),
                             levelPrice, qty);

        if (resting->isFilled()) {
            orderIndex_.erase(resting->id());
            queue.pop_front();
        }
        if (queue.empty()) opposite.erase(level);
    }
}

// explicit instantiations for both bid and ask maps
template void OrderBook::match(const std::shared_ptr<Order>&,
    std::map<Price, OrderBook::OrderQueue, std::greater<Price>>&);
template void OrderBook::match(const std::shared_ptr<Order>&,
    std::map<Price, OrderBook::OrderQueue>&);

// ======================= parkOrder / executeOrder =======================

void OrderBook::parkOrder(const std::shared_ptr<Order>& order) {
    Price p = order->price();
    if (order->side() == Side::BUY) {
        auto& q = bids_[p];
        q.push_back(order);
        orderIndex_[order->id()] = {false, Side::BUY, p, std::prev(q.end()), {}};
    } else {
        auto& q = asks_[p];
        q.push_back(order);
        orderIndex_[order->id()] = {false, Side::SELL, p, std::prev(q.end()), {}};
    }
}

void OrderBook::executeOrder(const std::shared_ptr<Order>& order) {
    if (order->side() == Side::BUY) match(order, asks_);
    else                            match(order, bids_);

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

void OrderBook::checkStopOrders() {
    if (trades_.empty() || stopOrders_.empty()) return;

    bool activated;
    do {
        activated       = false;
        Price lastPrice = trades_.back().price();
        auto  it        = stopOrders_.begin();

        while (it != stopOrders_.end()) {
            if ((*it)->shouldTrigger(lastPrice)) {
                auto sl = *it;
                sl->activate();
                std::cout << "  >>> SL triggered: " << *sl << "\n";
                orderIndex_.erase(sl->id());
                it = stopOrders_.erase(it);

                executeOrder(sl);
                activated = true;   // cascade: new trades may trigger more SLs
            } else {
                ++it;
            }
        }
    } while (activated);
}

// ======================= removeOrder =======================

std::shared_ptr<Order> OrderBook::removeOrder(int orderId, const std::string& trader) {
    auto idxIt = orderIndex_.find(orderId);
    if (idxIt == orderIndex_.end())
        throw InvalidOrderException("Order #" + std::to_string(orderId) +
                                    " not found (already filled or cancelled)");

    const OrderLocation& loc = idxIt->second;
    std::shared_ptr<Order> order = loc.isStopLoss
        ? std::static_pointer_cast<Order>(*loc.stopIt)
        : *loc.bookIt;

    if (order->trader() != trader)
        throw InvalidOrderException(trader + " cannot modify/cancel order #" +
                                    std::to_string(orderId) + " owned by " + order->trader());

    if (loc.isStopLoss) {
        stopOrders_.erase(loc.stopIt);
    } else if (loc.side == Side::BUY) {
        auto levelIt = bids_.find(loc.price);
        levelIt->second.erase(loc.bookIt);
        if (levelIt->second.empty()) bids_.erase(levelIt);
    } else {
        auto levelIt = asks_.find(loc.price);
        levelIt->second.erase(loc.bookIt);
        if (levelIt->second.empty()) asks_.erase(levelIt);
    }

    orderIndex_.erase(idxIt);
    return order;
}

// ======================= submit / cancel / modify =======================

void OrderBook::submit(const std::shared_ptr<Order>& order) {
    // [DYNAMIC_CAST] park untriggered SL orders in the pending queue
    auto sl = std::dynamic_pointer_cast<StopLossOrder>(order);
    if (sl && !sl->isTriggered()) {
        stopOrders_.push_back(sl);
        orderIndex_[order->id()] = {true, order->side(), 0, {}, std::prev(stopOrders_.end())};
        return;
    }

    executeOrder(order);
    checkStopOrders();
}

void OrderBook::cancel(int orderId, const std::string& trader) {
    removeOrder(orderId, trader);
}

void OrderBook::modify(int orderId, const std::string& trader, int newQty, Price newPrice) {
    if (newQty <= 0)   throw InvalidOrderException("Quantity must be positive");
    if (newPrice <= 0) throw InvalidOrderException("Price must be positive");

    auto idxIt = orderIndex_.find(orderId);
    if (idxIt == orderIndex_.end())
        throw InvalidOrderException("Order #" + std::to_string(orderId) +
                                    " not found (already filled or cancelled)");

    const OrderLocation& loc = idxIt->second;
    if (loc.isStopLoss) {
        if ((*loc.stopIt)->trader() != trader)
            throw InvalidOrderException(trader + " cannot modify order #" +
                                        std::to_string(orderId) + " owned by " +
                                        (*loc.stopIt)->trader());
        throw InvalidOrderException("Cannot modify pending stop-loss order #" +
                                    std::to_string(orderId) + "; cancel and re-place");
    }

    auto order = *loc.bookIt;
    if (order->trader() != trader)
        throw InvalidOrderException(trader + " cannot modify order #" +
                                    std::to_string(orderId) + " owned by " + order->trader());

    // Size reduction at the same price preserves FIFO time priority in O(1)
    if (newPrice == loc.price && newQty <= order->quantity()) {
        order->setQuantity(newQty);
        return;
    }

    // Price change or quantity increase loses time priority and re-enters matching
    removeOrder(orderId, trader);
    order->setQuantity(newQty);
    order->setPrice(newPrice);
    executeOrder(order);
    checkStopOrders();
}

// ======================= print =======================

void OrderBook::print() const {
    std::cout << "---- ORDER BOOK ----\n";
    std::cout << "SELLS (lowest first)\n";
    for (const auto& [price, queue] : asks_)
        for (const auto& o : queue)
            std::cout << "  " << *o << "\n";   // [POLYMORPHISM] prints correct subtype
    std::cout << "BUYS (highest first)\n";
    for (const auto& [price, queue] : bids_)
        for (const auto& o : queue)
            std::cout << "  " << *o << "\n";
    if (!stopOrders_.empty()) {
        std::cout << "STOP-LOSS (pending)\n";
        for (const auto& o : stopOrders_)
            std::cout << "  " << *o << "\n";
    }
    std::cout << "--------------------\n";
}
