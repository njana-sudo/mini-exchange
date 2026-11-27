// Order.cpp - implementations for Order, LimitOrder, MarketOrder, StopLossOrder

#include "Order.hpp"
#include "Types.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

// ======================= Order =======================

int Order::nextId = 1;

Order::Order(const std::string& trader, Side side, int qty)
    : id_(nextId++), trader_(trader), side_(side), quantity_(qty) {
    if (qty <= 0) throw InvalidOrderException("Quantity must be positive");
}

std::ostream& operator<<(std::ostream& os, const Order& o) {
    os << "#" << o.id_ << " " << o.type() << " " << toString(o.side_)
       << " " << o.quantity_ << " by " << o.trader_;
    if (!o.isMarket())
        os << " @ " << std::fixed << std::setprecision(2) << o.price();
    std::string extra = o.info();
    if (!extra.empty()) os << extra;
    return os;
}

// ======================= LimitOrder =======================

LimitOrder::LimitOrder(const std::string& trader, Side side, int qty, double price)
    : Order(trader, side, qty), price_(price) {
    if (price <= 0) throw InvalidOrderException("Price must be positive");
}

// ======================= MarketOrder =======================

MarketOrder::MarketOrder(const std::string& trader, Side side, int qty)
    : Order(trader, side, qty) {}

// ======================= StopLossOrder =======================

StopLossOrder::StopLossOrder(const std::string& trader, Side side, int qty,
                             double triggerPrice, double limitPrice)
    : Order(trader, side, qty), triggerPrice_(triggerPrice), limitPrice_(limitPrice) {
    if (triggerPrice <= 0) throw InvalidOrderException("Trigger price must be positive");
    if (limitPrice  <= 0) throw InvalidOrderException("Limit price must be positive");
}

std::string StopLossOrder::info() const {
    std::ostringstream oss;
    oss << " (trigger " << std::fixed << std::setprecision(2) << triggerPrice_ << ")";
    return oss.str();
}

bool StopLossOrder::shouldTrigger(double lastPrice) const {
    if (triggered_) return false;
    return (side() == Side::SELL) ? lastPrice <= triggerPrice_
                                  : lastPrice >= triggerPrice_;
}
