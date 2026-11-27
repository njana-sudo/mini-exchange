#pragma once
// Order.hpp - abstract Order base class and its concrete subtypes:
//             LimitOrder, MarketOrder, StopLossOrder
//
// OOP tags: [ABSTRACTION] [ENCAPSULATION] [INHERITANCE] [POLYMORPHISM]
//           [STATIC] [FRIEND] [CONSTRUCTORS]
//
// All prices are stored and returned as integer ticks (Price = int64_t).
// See Types.hpp for the tick system rationale.

#include "Types.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// ======================= Order (abstract base) =======================

class Order {
private:
    static int nextId;
    int         id_;
    std::string trader_;
    Side        side_;
    int         quantity_;

public:
    Order(const std::string& trader, Side side, int qty);
    virtual ~Order() = default;

    int                id()       const { return id_; }
    const std::string& trader()   const { return trader_; }
    Side               side()     const { return side_; }
    int                quantity() const { return quantity_; }
    bool               isFilled() const { return quantity_ == 0; }
    void               fill(int qty)    { quantity_ -= qty; }
    void               setQuantity(int qty);

    // Returns price in ticks.  MarketOrder returns 0 (no price).
    virtual Price       price()    const = 0;
    virtual bool        isMarket() const = 0;
    virtual std::string type()     const = 0;
    virtual std::string info()     const { return ""; }
    virtual void        setPrice(Price newPrice);

    friend std::ostream& operator<<(std::ostream& os, const Order& o);
};

// Displays price as rupees (to_rupees conversion is display-only).
std::ostream& operator<<(std::ostream& os, const Order& o);

// ======================= LimitOrder =======================

class LimitOrder : public Order {
private:
    Price price_;           // stored in ticks

public:
    // User supplies price in rupees; constructor converts to ticks.
    LimitOrder(const std::string& trader, Side side, int qty, double price);

    Price       price()    const override { return price_; }
    bool        isMarket() const override { return false; }
    std::string type()     const override { return "LIMIT"; }
    void        setPrice(Price newPrice) override;
};

// ======================= MarketOrder =======================

class MarketOrder : public Order {
public:
    MarketOrder(const std::string& trader, Side side, int qty);

    Price       price()    const override { return 0; }
    bool        isMarket() const override { return true; }
    std::string type()     const override { return "MARKET"; }
};

// ======================= StopLossOrder =======================

class StopLossOrder : public Order {
private:
    Price triggerPrice_;    // in ticks
    Price limitPrice_;      // in ticks
    bool  triggered_ = false;

public:
    // User supplies prices in rupees; constructor converts both to ticks.
    StopLossOrder(const std::string& trader, Side side, int qty,
                  double triggerPrice, double limitPrice);

    Price       price()    const override { return limitPrice_; }
    bool        isMarket() const override { return false; }
    std::string type()     const override { return triggered_ ? "SL-ACTIVE" : "SL-PENDING"; }
    std::string info()     const override;
    void        setPrice(Price newPrice) override;

    Price triggerPrice() const { return triggerPrice_; }
    bool  isTriggered()  const { return triggered_; }
    void  activate()           { triggered_ = true; }

    // Comparison is now exact integer arithmetic — no floating-point ambiguity.
    bool shouldTrigger(Price lastPrice) const;
};
