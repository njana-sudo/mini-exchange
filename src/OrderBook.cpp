// OrderBook.cpp - hot-path matching engine implementation

#include "OrderBook.hpp"

#include <algorithm>
#include <iostream>

// ======================= match (private template) =======================
// Template bodies must be in a header OR in the .cpp that instantiates them.
// We instantiate both sides explicitly here, so the definition lives in this TU.

template <typename Book>
void OrderBook::match(const std::shared_ptr<Order>& incoming, Book& opposite) {
    while (!incoming->isFilled() && !opposite.empty()) {
        auto   level      = opposite.begin();
        double levelPrice = level->first;

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

        if (resting->isFilled()) queue.pop_front();
        if (queue.empty())       opposite.erase(level);
    }
}

// explicit instantiations so the linker can find them from other .cpp files
template void OrderBook::match(const std::shared_ptr<Order>&,
    std::map<double, std::deque<std::shared_ptr<Order>>, std::greater<double>>&);
template void OrderBook::match(const std::shared_ptr<Order>&,
    std::map<double, std::deque<std::shared_ptr<Order>>>&);

// ======================= checkStopOrders =======================

void OrderBook::checkStopOrders() {
    if (trades_.empty() || stopOrders_.empty()) return;

    bool activated;
    do {
        activated        = false;
        double lastPrice = trades_.back().price();
        auto   it        = stopOrders_.begin();

        while (it != stopOrders_.end()) {
            if ((*it)->shouldTrigger(lastPrice)) {
                auto sl = *it;
                sl->activate();
                std::cout << "  >>> SL triggered: " << *sl << "\n";
                it = stopOrders_.erase(it);

                std::shared_ptr<Order> order = sl;
                if (order->side() == Side::BUY) match(order, asks_);
                else                            match(order, bids_);

                if (!order->isFilled()) {
                    if (order->side() == Side::BUY) bids_[order->price()].push_back(order);
                    else                            asks_[order->price()].push_back(order);
                }
                activated = true;   // cascade: new trades may trigger more SLs
            } else {
                ++it;
            }
        }
    } while (activated);
}

// ======================= submit =======================

void OrderBook::submit(const std::shared_ptr<Order>& order) {
    // [DYNAMIC_CAST] park untriggered SL orders in the pending queue
    auto sl = std::dynamic_pointer_cast<StopLossOrder>(order);
    if (sl && !sl->isTriggered()) {
        stopOrders_.push_back(sl);
        return;
    }

    if (order->side() == Side::BUY) match(order, asks_);
    else                            match(order, bids_);

    if (!order->isFilled()) {
        if (order->isMarket()) {
            std::cout << "  (market order #" << order->id()
                      << " partly unfilled: " << order->quantity()
                      << " left, no liquidity)\n";
        } else if (order->side() == Side::BUY) {
            bids_[order->price()].push_back(order);
        } else {
            asks_[order->price()].push_back(order);
        }
    }

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
