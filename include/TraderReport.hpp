#pragma once
// TraderReport.hpp - cold-path analytics.
// Uses int64_t ticks×shares internally; converts to rupees only for display.

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
        OrderId     orderId;
        Price       price;
        Quantity    quantity;
        std::time_t timestamp;
        int64_t     value() const { return static_cast<int64_t>(price) * quantity; }
    };

    std::vector<Entry> buys_;
    std::vector<Entry> sells_;

public:
    TraderReport(const std::string& name, int months);

    void addBuy (OrderId orderId, Price price, Quantity qty, std::time_t ts);
    void addSell(OrderId orderId, Price price, Quantity qty, std::time_t ts);

    bool     empty()            const { return buys_.empty() && sells_.empty(); }
    Quantity totalBought()      const;
    int64_t  totalBuyCost()     const;
    Quantity totalSold()        const;
    int64_t  totalSellRevenue() const;

    double   avgBuyPrice()      const;
    double   avgSellPrice()     const;
    Quantity netPosition()      const;
    double   realizedPnL()      const;

    friend std::ostream& operator<<(std::ostream& os, const TraderReport& r);
};

std::ostream& operator<<(std::ostream& os, const TraderReport& r);
