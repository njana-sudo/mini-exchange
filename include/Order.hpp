#pragma once
// Order.hpp - abstract Order base class and its concrete subtypes:
//             LimitOrder, MarketOrder, StopLossOrder
//
// OOP tags: [ABSTRACTION] [ENCAPSULATION] [INHERITANCE] [POLYMORPHISM]
//           [STATIC] [FRIEND] [CONSTRUCTORS]

#include "Types.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// ======================= Order (abstract base) =======================

// [ABSTRACTION] Order cannot be instantiated — derived classes implement pure virtual functions.
class Order {
private:                           // [ENCAPSULATION] hidden from outside code
    static int nextId;             // [STATIC] shared counter across all instances
    int    id_;
    std::string trader_;
    Side   side_;
    int    quantity_;

public:
    // [CONSTRUCTORS] initializer-list constructor; throws on bad input
    Order(const std::string& trader, Side side, int qty);
    virtual ~Order() = default;    // [CONSTRUCTORS] virtual destructor for safe polymorphic deletion

    // [ENCAPSULATION] const getters
    int                id()       const { return id_; }
    const std::string& trader()   const { return trader_; }
    Side               side()     const { return side_; }
    int                quantity() const { return quantity_; }
    bool               isFilled() const { return quantity_ == 0; }
    void               fill(int qty)    { quantity_ -= qty; }

    // [ABSTRACTION] pure virtual interface — every subtype must implement these
    virtual bool        isMarket() const = 0;
    virtual double      price()    const = 0;
    virtual std::string type()     const = 0;
    virtual std::string info()     const { return ""; }  // optional extra display info

    // [FRIEND] lets operator<< access private members
    friend std::ostream& operator<<(std::ostream& os, const Order& o);
};

// [POLYMORPHISM] operator overloading; type() / info() resolved at runtime (virtual dispatch)
std::ostream& operator<<(std::ostream& os, const Order& o);

// ======================= LimitOrder =======================

// [INHERITANCE] LimitOrder "is an" Order: executes only at the specified price or better.
class LimitOrder : public Order {
private:
    double price_;

public:
    LimitOrder(const std::string& trader, Side side, int qty, double price);

    bool        isMarket() const override { return false; }  // [POLYMORPHISM]
    double      price()    const override { return price_; }
    std::string type()     const override { return "LIMIT"; }
};

// ======================= MarketOrder =======================

// [INHERITANCE] MarketOrder "is an" Order: executes at the best available price immediately.
class MarketOrder : public Order {
public:
    MarketOrder(const std::string& trader, Side side, int qty);

    bool        isMarket() const override { return true; }
    double      price()    const override { return 0.0; }
    std::string type()     const override { return "MARKET"; }
};

// ======================= StopLossOrder =======================

// [INHERITANCE] StopLossOrder "is an" Order: rests in a pending queue until the market price
// crosses the trigger, then activates as a limit order — identical to Zerodha's SL order.
class StopLossOrder : public Order {
private:
    double triggerPrice_;
    double limitPrice_;
    bool   triggered_ = false;

public:
    StopLossOrder(const std::string& trader, Side side, int qty,
                  double triggerPrice, double limitPrice);

    bool        isMarket()    const override { return false; }
    double      price()       const override { return limitPrice_; }
    std::string type()        const override { return triggered_ ? "SL-ACTIVE" : "SL-PENDING"; }
    std::string info()        const override;

    double triggerPrice() const { return triggerPrice_; }
    bool   isTriggered()  const { return triggered_; }
    void   activate()           { triggered_ = true; }

    // [POLYMORPHISM] sell SL triggers when price drops; buy SL triggers when price rises
    bool shouldTrigger(double lastPrice) const;
};
