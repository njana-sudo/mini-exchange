// Order.cpp - implementations for Order, LimitOrder, MarketOrder, StopLossOrder

#include "Order.hpp"
#include "Types.hpp"

#include <iomanip>
#include <sstream>

// ======================= Order =======================

Order::Order(OrderId id, const std::string& trader, Side side, Quantity qty)
    : id_(id), trader_(trader), side_(side), quantity_(qty) {
    if (qty <= 0) throw InvalidOrderException("Quantity must be positive");
}

void Order::setQuantity(Quantity qty) {
    if (qty <= 0) throw InvalidOrderException("Quantity must be positive");
    quantity_ = qty;
}

void Order::setPrice(Price) {
    throw InvalidOrderException("Cannot modify price on " + type() + " order");
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

LimitOrder::LimitOrder(TicksTag, OrderId id, const std::string& trader,
                       Side side, Quantity qty, Price priceTicks)
    : Order(id, trader, side, qty), price_(priceTicks) {
    if (priceTicks <= 0) throw InvalidOrderException("Price must be positive");
}

LimitOrder::LimitOrder(OrderId id, const std::string& trader, Side side, Quantity qty, double price)
    : Order(id, trader, side, qty), price_(validateAndConvertPrice(price, "Price")) {}

std::shared_ptr<LimitOrder> LimitOrder::fromTicks(OrderId id, const std::string& trader,
                                                  Side side, Quantity qty, Price priceTicks) {
    return std::shared_ptr<LimitOrder>(new LimitOrder(TicksTag{}, id, trader, side, qty, priceTicks));
}

void LimitOrder::setPrice(Price newPrice) {
    if (newPrice <= 0) throw InvalidOrderException("Price must be positive");
    price_ = newPrice;
}

// ======================= MarketOrder =======================

MarketOrder::MarketOrder(OrderId id, const std::string& trader, Side side, Quantity qty)
    : Order(id, trader, side, qty) {}

// ======================= StopLossOrder =======================

StopLossOrder::StopLossOrder(TicksTag, OrderId id, const std::string& trader,
                             Side side, Quantity qty, Price triggerTicks, Price limitTicks)
    : Order(id, trader, side, qty), triggerPrice_(triggerTicks), limitPrice_(limitTicks) {
    if (triggerTicks <= 0) throw InvalidOrderException("Trigger price must be positive");
    if (limitTicks   <= 0) throw InvalidOrderException("Limit price must be positive");
}

StopLossOrder::StopLossOrder(OrderId id, const std::string& trader, Side side, Quantity qty,
                             double triggerPrice, double limitPrice)
    : Order(id, trader, side, qty),
      triggerPrice_(validateAndConvertPrice(triggerPrice, "Trigger price")),
      limitPrice_(validateAndConvertPrice(limitPrice, "Limit price")) {}

std::shared_ptr<StopLossOrder> StopLossOrder::fromTicks(OrderId id, const std::string& trader,
                                                        Side side, Quantity qty,
                                                        Price triggerTicks, Price limitTicks) {
    return std::shared_ptr<StopLossOrder>(
        new StopLossOrder(TicksTag{}, id, trader, side, qty, triggerTicks, limitTicks));
}

void StopLossOrder::setPrice(Price newPrice) {
    if (newPrice <= 0) throw InvalidOrderException("Limit price must be positive");
    limitPrice_ = newPrice;
}

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
