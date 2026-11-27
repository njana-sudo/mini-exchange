// Order.cpp - implementations for Order, LimitOrder, MarketOrder, StopLossOrder

#include "Order.hpp"
#include "Types.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
Price validateAndConvertPrice(double rupees, const char* label) {
    if (!std::isfinite(rupees))
        throw InvalidOrderException(std::string(label) + " must be finite");
    Price ticks = to_ticks(rupees);
    if (ticks <= 0)
        throw InvalidOrderException(std::string(label) + " must be positive");
    return ticks;
}
}  // namespace

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
        os << " @ " << std::fixed << std::setprecision(2) << to_rupees(o.price());
    std::string extra = o.info();
    if (!extra.empty()) os << extra;
    return os;
}

// ======================= LimitOrder =======================

LimitOrder::LimitOrder(const std::string& trader, Side side, int qty, double price)
    : Order(trader, side, qty), price_(validateAndConvertPrice(price, "Price")) {}

// ======================= MarketOrder =======================

MarketOrder::MarketOrder(const std::string& trader, Side side, int qty)
    : Order(trader, side, qty) {}

// ======================= StopLossOrder =======================

StopLossOrder::StopLossOrder(const std::string& trader, Side side, int qty,
                             double triggerPrice, double limitPrice)
    : Order(trader, side, qty),
      triggerPrice_(validateAndConvertPrice(triggerPrice, "Trigger price")),
      limitPrice_(validateAndConvertPrice(limitPrice, "Limit price")) {}

std::string StopLossOrder::info() const {
    std::ostringstream oss;
    oss << " (trigger " << std::fixed << std::setprecision(2) << to_rupees(triggerPrice_) << ")";
    return oss.str();
}

bool StopLossOrder::shouldTrigger(Price lastPrice) const {
    if (triggered_) return false;
    return (side() == Side::SELL) ? lastPrice <= triggerPrice_
                                  : lastPrice >= triggerPrice_;
}
