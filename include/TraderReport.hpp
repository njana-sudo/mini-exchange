#pragma once
// TraderReport.hpp - cold-path analytics.
// All internal arithmetic is done in ticks (int64_t).
// Results are exposed in rupees (double) only for display.

#include "Types.hpp"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

class TraderReport {
private:
    std::string name_;
    int         months_;

    struct Entry {
        int         orderId;
        Price       price;      // ticks
        int         quantity;
        std::time_t timestamp;
        // Value in ticks×shares; divide by TICKS_PER_RUPEE for rupee value.
        int64_t value() const { return static_cast<int64_t>(price) * quantity; }
    };

    std::vector<Entry> buys_;
    std::vector<Entry> sells_;

public:
    TraderReport(const std::string& name, int months);

    void addBuy (int orderId, Price price, int qty, std::time_t ts);
    void addSell(int orderId, Price price, int qty, std::time_t ts);

    bool    empty()          const { return buys_.empty() && sells_.empty(); }

    int     totalBought()    const;
    int64_t totalBuyCost()   const;   // ticks×shares
    int     totalSold()      const;
    int64_t totalSellRevenue() const; // ticks×shares

    // Display-only conversions to rupees:
    double avgBuyPrice()    const;    // ₹ per share
    double avgSellPrice()   const;    // ₹ per share
    int    netPosition()    const;    // shares (positive = long, negative = short)
    double realizedPnL()    const;    // ₹ (avg-cost method)

    friend std::ostream& operator<<(std::ostream& os, const TraderReport& r);
};

std::ostream& operator<<(std::ostream& os, const TraderReport& r);
