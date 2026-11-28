#pragma once
// Order.hpp - abstract Order base class and its concrete subtypes:
//             LimitOrder, MarketOrder, StopLossOrder
//
// All prices are stored as Price (int64_t ticks).
// Order IDs (OrderId = uint64_t) are assigned by the Exchange/OrderBook
// only after validation succeeds, so rejected orders never burn IDs.

#include "Types.hpp"

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

// ======================= Order (abstract base) =======================

class Order {
private:
    OrderId     id_;
    std::string trader_;
    Side        side_;
    Quantity    quantity_;

public:
    Order(OrderId id, const std::string& trader, Side side, Quantity qty);
    virtual ~Order() = default;

    OrderId            id()       const { return id_; }
    const std::string& trader()   const { return trader_; }
    Side               side()     const { return side_; }
    Quantity           quantity() const { return quantity_; }
    bool               isFilled() const { return quantity_ == 0; }
    void               fill(Quantity qty) { quantity_ -= qty; }
    void               setQuantity(Quantity qty);

    virtual Price       price()    const = 0;
    virtual bool        isMarket() const = 0;
    virtual std::string type()     const = 0;
    virtual std::string info()     const { return ""; }
    virtual void        setPrice(Price newPrice);

    friend std::ostream& operator<<(std::ostream& os, const Order& o);
};

std::ostream& operator<<(std::ostream& os, const Order& o);

// ======================= LimitOrder =======================

class LimitOrder : public Order {
private:
    Price price_;

    struct TicksTag {};
    LimitOrder(TicksTag, OrderId id, const std::string& trader, Side side, Quantity qty, Price priceTicks);

public:
    LimitOrder(OrderId id, const std::string& trader, Side side, Quantity qty, double price);
    static std::shared_ptr<LimitOrder> fromTicks(OrderId id, const std::string& trader,
                                                 Side side, Quantity qty, Price priceTicks);

    Price       price()    const override { return price_; }
    bool        isMarket() const override { return false; }
    std::string type()     const override { return "LIMIT"; }
    void        setPrice(Price newPrice) override;
};

// ======================= MarketOrder =======================

class MarketOrder : public Order {
public:
    MarketOrder(OrderId id, const std::string& trader, Side side, Quantity qty);

    Price       price()    const override { return 0; }
    bool        isMarket() const override { return true; }
    std::string type()     const override { return "MARKET"; }
};

// ======================= StopLossOrder =======================

class StopLossOrder : public Order {
private:
    Price triggerPrice_;
    Price limitPrice_;
    bool  triggered_ = false;

    struct TicksTag {};
    StopLossOrder(TicksTag, OrderId id, const std::string& trader, Side side, Quantity qty,
                  Price triggerTicks, Price limitTicks);

public:
    StopLossOrder(OrderId id, const std::string& trader, Side side, Quantity qty,
                  double triggerPrice, double limitPrice);
    static std::shared_ptr<StopLossOrder> fromTicks(OrderId id, const std::string& trader,
                                                    Side side, Quantity qty,
                                                    Price triggerTicks, Price limitTicks);

    Price       price()    const override { return limitPrice_; }
    bool        isMarket() const override { return false; }
    std::string type()     const override { return triggered_ ? "SL-ACTIVE" : "SL-PENDING"; }
    std::string info()     const override;
    void        setPrice(Price newPrice) override;

    Price triggerPrice() const { return triggerPrice_; }
    bool  isTriggered()  const { return triggered_; }
    void  activate()           { triggered_ = true; }

    bool shouldTrigger(Price lastPrice) const;
};
