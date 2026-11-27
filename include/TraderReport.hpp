#pragma once
// TraderReport.hpp - cold-path analytics: buy/sell history and realized P&L for one trader
//                    over a configurable time window.
//
// OOP tags: [ENCAPSULATION] [COMPOSITION] [NESTED_STRUCT] [FRIEND]

#include <algorithm>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

// ======================= TraderReport =======================

// [ENCAPSULATION] [COMPOSITION] aggregates executed trades for one trader and computes P&L.
class TraderReport {
private:
    std::string name_;
    int         months_;   // 0 = all time

    // [NESTED_STRUCT] groups the fields of a single trade line-item together
    struct Entry {
        int         orderId;
        double      price;
        int         quantity;
        std::time_t timestamp;
        double value() const { return price * quantity; }
    };

    std::vector<Entry> buys_;
    std::vector<Entry> sells_;

public:
    TraderReport(const std::string& name, int months);

    void addBuy (int orderId, double price, int qty, std::time_t ts);
    void addSell(int orderId, double price, int qty, std::time_t ts);

    bool   empty()          const { return buys_.empty() && sells_.empty(); }
    int    totalBought()    const;
    double totalBuyCost()   const;
    int    totalSold()      const;
    double totalSellRevenue() const;
    double avgBuyPrice()    const;
    double avgSellPrice()   const;
    int    netPosition()    const;
    double realizedPnL()    const;   // average-cost method (same as Zerodha Console)

    // [FRIEND]
    friend std::ostream& operator<<(std::ostream& os, const TraderReport& r);
};

std::ostream& operator<<(std::ostream& os, const TraderReport& r);
