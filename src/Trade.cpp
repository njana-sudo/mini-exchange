// Trade.cpp - implementation of Trade

#include "Trade.hpp"
#include "Types.hpp"

#include <iomanip>

Trade::Trade()
    : buyOrderId_(0), sellOrderId_(0), price_(0), quantity_(0), timestamp_(0) {}

Trade::Trade(OrderId buyId, OrderId sellId,
             const std::string& buyer, const std::string& seller,
             Price price, Quantity qty, std::time_t ts)
    : buyOrderId_(buyId), sellOrderId_(sellId),
      buyer_(buyer), seller_(seller),
      price_(price), quantity_(qty), timestamp_(ts) {}

std::ostream& operator<<(std::ostream& os, const Trade& t) {
    return os << "TRADE buy#" << t.buyOrderId_ << " sell#" << t.sellOrderId_
              << " " << t.quantity_
              << " @ " << std::fixed << std::setprecision(2) << to_rupees(t.price_)
              << "  [" << formatDate(t.timestamp_) << "]";
}
