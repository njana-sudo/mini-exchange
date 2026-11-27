// Exchange.cpp - top-level facade implementation

#include "Exchange.hpp"
#include "Types.hpp"

#include <iomanip>
#include <iostream>
#include <string>

// ======================= private helpers =======================

void Exchange::validate(const std::string& traderName, Quantity qty) const {
    auto it = traders_.find(traderName);
    if (it == traders_.end())
        throw InvalidOrderException("Unknown trader: " + traderName);
    if (qty <= 0)
        throw InvalidOrderException("Quantity must be positive");
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

OrderId Exchange::placeOrder(const std::string& trader, Side side, Quantity qty, double price) {
    validate(trader, qty);
    auto order = std::make_shared<LimitOrder>(nextOrderId_, trader, side, qty, price);
    ++nextOrderId_;
    std::cout << "Placed  " << *order << "\n";
    book_.submit(order);
    return order->id();
}

OrderId Exchange::placeOrder(const std::string& trader, Side side, Quantity qty) {
    validate(trader, qty);
    auto order = std::make_shared<MarketOrder>(nextOrderId_, trader, side, qty);
    ++nextOrderId_;
    std::cout << "Placed  " << *order << "\n";
    book_.submit(order);
    return order->id();
}

OrderId Exchange::placeOrder(const std::string& trader, Side side, Quantity qty,
                             double triggerPrice, double limitPrice) {
    validate(trader, qty);
    auto order = std::make_shared<StopLossOrder>(nextOrderId_, trader, side, qty, triggerPrice, limitPrice);
    if (book_.lastTradePrice().has_value() && order->shouldTrigger(*book_.lastTradePrice())) {
        throw InvalidOrderException(
            "Stop-loss trigger price is already crossed against last traded price");
    }
    ++nextOrderId_;
    std::cout << "Placed  " << *order << "\n";
    book_.submit(order);
    return order->id();
}

void Exchange::cancelOrder(const std::string& trader, OrderId orderId) {
    if (traders_.find(trader) == traders_.end())
        throw InvalidOrderException("Unknown trader: " + trader);
    book_.cancel(orderId, trader);
    std::cout << "Cancelled #" << orderId << " by " << trader << "\n";
}

void Exchange::modifyOrder(const std::string& trader, OrderId orderId, Quantity newQty, double newPrice) {
    validate(trader, newQty);
    Price ticks = validateAndConvertPrice(newPrice, "Price");
    book_.modify(orderId, trader, newQty, ticks);
    std::cout << "Modified  #" << orderId << " -> qty " << newQty
              << " @ " << std::fixed << std::setprecision(2) << to_rupees(ticks)
              << " by " << trader << "\n";
}

TraderReport Exchange::buildTraderReport(const std::string& name, int months) const {
    if (traders_.find(name) == traders_.end())
        throw InvalidOrderException("Unknown trader: " + name);

    std::time_t now    = clock_();
    std::time_t cutoff = (months > 0)
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
    return report;
}

void Exchange::traderReport(const std::string& name, int months) const {
    if (traders_.find(name) == traders_.end()) {
        std::cout << "Unknown trader: " << name << "\n";
        return;
    }
    std::cout << buildTraderReport(name, months);
}

void Exchange::listTraders() const {
    std::cout << "Registered traders\n";
    for (const auto& [name, trader] : traders_)
        std::cout << "  " << name << " - " << trader->category()
                  << " (max " << trader->maxOrderSize() << ")\n";
}
