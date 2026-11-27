#pragma once
// Trade.hpp - immutable record of an executed match between a buyer and a seller.
//
// OOP tags: [ENCAPSULATION] [CONSTRUCTORS] [FRIEND]

#include "Types.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

// ======================= Trade =======================

class Trade {
private:
    int         buyOrderId_;
    int         sellOrderId_;
    std::string buyer_;
    std::string seller_;
    double      price_;
    int         quantity_;
    std::time_t timestamp_;

public:
    Trade();  // [CONSTRUCTORS] default — zero-initializes, stamps current time
    Trade(int buyId, int sellId,
          const std::string& buyer, const std::string& seller,
          double price, int qty,
          std::time_t ts = std::time(nullptr));  // [CONSTRUCTORS] parameterized

    // [ENCAPSULATION] const getters
    int                buyOrderId()  const { return buyOrderId_; }
    int                sellOrderId() const { return sellOrderId_; }
    const std::string& buyer()       const { return buyer_; }
    const std::string& seller()      const { return seller_; }
    double             price()       const { return price_; }
    int                quantity()    const { return quantity_; }
    std::time_t        timestamp()   const { return timestamp_; }

    // [FRIEND]
    friend std::ostream& operator<<(std::ostream& os, const Trade& t);
};

std::ostream& operator<<(std::ostream& os, const Trade& t);
