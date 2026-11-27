#pragma once
// Trade.hpp - immutable record of an executed match.
// price_ is stored in ticks (Price = int64_t).

#include "Types.hpp"

#include <ctime>
#include <iostream>
#include <string>

class Trade {
private:
    int         buyOrderId_;
    int         sellOrderId_;
    std::string buyer_;
    std::string seller_;
    Price       price_;         // execution price in ticks
    int         quantity_;
    std::time_t timestamp_;

public:
    Trade();
    Trade(int buyId, int sellId,
          const std::string& buyer, const std::string& seller,
          Price price, int qty,
          std::time_t ts = std::time(nullptr));

    int                buyOrderId()  const { return buyOrderId_; }
    int                sellOrderId() const { return sellOrderId_; }
    const std::string& buyer()       const { return buyer_; }
    const std::string& seller()      const { return seller_; }
    Price              price()       const { return price_; }   // ticks
    int                quantity()    const { return quantity_; }
    std::time_t        timestamp()   const { return timestamp_; }

    friend std::ostream& operator<<(std::ostream& os, const Trade& t);
};

std::ostream& operator<<(std::ostream& os, const Trade& t);
