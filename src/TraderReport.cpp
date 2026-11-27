// TraderReport.cpp - cold-path analytics implementation

#include "TraderReport.hpp"
#include "Types.hpp"

#include <algorithm>
#include <iomanip>
#include <string>

TraderReport::TraderReport(const std::string& name, int months)
    : name_(name), months_(months) {}

void TraderReport::addBuy(int orderId, Price price, int qty, std::time_t ts) {
    buys_.push_back({orderId, price, qty, ts});
}
void TraderReport::addSell(int orderId, Price price, int qty, std::time_t ts) {
    sells_.push_back({orderId, price, qty, ts});
}

int TraderReport::totalBought() const {
    int t = 0; for (const auto& e : buys_)  t += e.quantity; return t;
}
int64_t TraderReport::totalBuyCost() const {
    int64_t t = 0; for (const auto& e : buys_)  t += e.value(); return t;
}
int TraderReport::totalSold() const {
    int t = 0; for (const auto& e : sells_) t += e.quantity; return t;
}
int64_t TraderReport::totalSellRevenue() const {
    int64_t t = 0; for (const auto& e : sells_) t += e.value(); return t;
}
double TraderReport::avgBuyPrice()  const {
    return totalBought() > 0
        ? static_cast<double>(totalBuyCost()) / (static_cast<double>(totalBought()) * TICKS_PER_RUPEE)
        : 0.0;
}
double TraderReport::avgSellPrice() const {
    return totalSold() > 0
        ? static_cast<double>(totalSellRevenue()) / (static_cast<double>(totalSold()) * TICKS_PER_RUPEE)
        : 0.0;
}
int TraderReport::netPosition() const { return totalBought() - totalSold(); }

double TraderReport::realizedPnL() const {
    int matched = std::min(totalBought(), totalSold());
    if (matched == 0) return 0.0;
    return matched * (avgSellPrice() - avgBuyPrice());
}

std::ostream& operator<<(std::ostream& os, const TraderReport& r) {
    std::string period = r.months_ > 0
        ? "last " + std::to_string(r.months_) + " month(s)"
        : "all time";
    os << "\n====== Trade Report: " << r.name_ << " (" << period << ") ======\n";

    if (r.empty()) {
        os << "  No trades found.\n"
           << "================================================\n";
        return os;
    }

    os << std::fixed << std::setprecision(2);

    if (!r.buys_.empty()) {
        os << "PURCHASES\n";
        for (const auto& e : r.buys_)
            os << "  order #" << e.orderId << "  " << e.quantity
               << " shares @ " << to_rupees(e.price) << " = " << to_rupees(e.value())
               << "  [" << formatDate(e.timestamp) << "]\n";
        os << "  Total: " << r.totalBought() << " shares"
           << " | Cost: "      << to_rupees(r.totalBuyCost())
           << " | Avg price: " << r.avgBuyPrice() << "\n\n";
    }

    if (!r.sells_.empty()) {
        os << "SALES\n";
        for (const auto& e : r.sells_)
            os << "  order #" << e.orderId << "  " << e.quantity
               << " shares @ " << to_rupees(e.price) << " = " << to_rupees(e.value())
               << "  [" << formatDate(e.timestamp) << "]\n";
        os << "  Total: " << r.totalSold() << " shares"
           << " | Revenue: "   << to_rupees(r.totalSellRevenue())
           << " | Avg price: " << r.avgSellPrice() << "\n\n";
    }

    os << "Net position: ";
    int net = r.netPosition();
    if      (net > 0) os << "+" << net << " shares (long)";
    else if (net < 0) os <<        net << " shares (short)";
    else              os << "0 (flat)";

    os << "\nRealized P&L: ";
    double pnl = r.realizedPnL();
    if (pnl >= 0) os << "+";
    os << pnl;
    int matched = std::min(r.totalBought(), r.totalSold());
    if (matched > 0)
        os << "  (on " << matched << " matched shares, avg-cost method)";

    os << "\n================================================\n";
    return os;
}
