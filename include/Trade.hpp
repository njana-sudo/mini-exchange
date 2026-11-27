#pragma once
// Trade.hpp - immutable record of an executed match.

#include "Types.hpp"

#include <ctime>
#include <iostream>
#include <string>

class Trade {
private:
    OrderId     buyOrderId_;
    OrderId     sellOrderId_;
    std::string buyer_;
    std::string seller_;
    Price       price_;
    Quantity    quantity_;
    std::time_t timestamp_;

public:
    Trade();
    Trade(OrderId buyId, OrderId sellId,
          const std::string& buyer, const std::string& seller,
          Price price, Quantity qty, std::time_t ts);

    OrderId            buyOrderId()  const { return buyOrderId_; }
    OrderId            sellOrderId() const { return sellOrderId_; }
    const std::string& buyer()       const { return buyer_; }
    const std::string& seller()      const { return seller_; }
    Price              price()       const { return price_; }
    Quantity           quantity()    const { return quantity_; }
    std::time_t        timestamp()   const { return timestamp_; }

    friend std::ostream& operator<<(std::ostream& os, const Trade& t);
};

std::ostream& operator<<(std::ostream& os, const Trade& t);
