// Exchange.cpp - top-level facade implementation

#include "Exchange.hpp"
#include "Types.hpp"

#include <iostream>
#include <string>

// ======================= private helpers =======================

void Exchange::validate(const std::string& traderName, int qty) const {
    auto it = traders_.find(traderName);
    if (it == traders_.end())
        throw InvalidOrderException("Unknown trader: " + traderName);
    if (qty > it->second->maxOrderSize())
        throw InvalidOrderException(
            traderName + " (" + it->second->category() +
            ") exceeds max order size " +
            std::to_string(it->second->maxOrderSize()));
}

// ======================= public interface =======================

void Exchange::registerTrader(std::unique_ptr<Trader> trader) {
    std::string name = trader->name();
    traders_[name]   = std::move(trader);
}

void Exchange::placeOrder(const std::string& trader, Side side, int qty, double price) {
    validate(trader, qty);
    auto order = std::make_shared<LimitOrder>(trader, side, qty, price);
    std::cout << "Placed  " << *order << "\n";
    book_.submit(order);
}

void Exchange::placeOrder(const std::string& trader, Side side, int qty) {
    validate(trader, qty);
    auto order = std::make_shared<MarketOrder>(trader, side, qty);
    std::cout << "Placed  " << *order << "\n";
    book_.submit(order);
}

void Exchange::placeOrder(const std::string& trader, Side side, int qty,
                           double triggerPrice, double limitPrice) {
    validate(trader, qty);
    auto order = std::make_shared<StopLossOrder>(trader, side, qty, triggerPrice, limitPrice);
    std::cout << "Placed  " << *order << "\n";
    book_.submit(order);
}

void Exchange::traderReport(const std::string& name, int months) const {
    if (traders_.find(name) == traders_.end()) {
        std::cout << "Unknown trader: " << name << "\n";
        return;
    }

    std::time_t now     = std::time(nullptr);
    std::time_t cutoff  = (months > 0)
        ? now - static_cast<std::time_t>(months) * 30 * 24 * 3600
        : 0;

    TraderReport report(name, months);
    for (const auto& trade : book_.trades()) {
        if (trade.timestamp() < cutoff) continue;
        if (trade.buyer()  == name)
            report.addBuy (trade.buyOrderId(),  trade.price(), trade.quantity(), trade.timestamp());
        if (trade.seller() == name)
            report.addSell(trade.sellOrderId(), trade.price(), trade.quantity(), trade.timestamp());
    }
    std::cout << report;
}

void Exchange::listTraders() const {
    std::cout << "Registered traders\n";
    for (const auto& [name, trader] : traders_)
        std::cout << "  " << name << " - " << trader->category()
                  << " (max " << trader->maxOrderSize() << ")\n";
}
