#pragma once
// Trader.hpp - abstract Trader base class and concrete subtypes:
//              RetailTrader, InstitutionalTrader
//
// OOP tags: [ABSTRACTION] [INHERITANCE] [POLYMORPHISM] [CONSTRUCTORS]

#include <string>
#include <utility>

// ======================= Trader (abstract base) =======================

// [ABSTRACTION] Trader cannot be instantiated — subtypes define category-specific limits.
class Trader {
protected:                         // accessible to derived classes
    std::string name_;

public:
    explicit Trader(std::string name) : name_(std::move(name)) {}
    virtual ~Trader() = default;

    const std::string& name() const { return name_; }

    // [ABSTRACTION] pure virtual — each trader category defines its own rules
    virtual int         maxOrderSize() const = 0;
    virtual std::string category()     const = 0;
};

// ======================= RetailTrader =======================

// [INHERITANCE] RetailTrader "is a" Trader: smaller position limits, typical retail investor.
class RetailTrader : public Trader {
public:
    explicit RetailTrader(const std::string& name) : Trader(name) {}
    int         maxOrderSize() const override { return 100; }       // [POLYMORPHISM]
    std::string category()     const override { return "Retail"; }
};

// ======================= InstitutionalTrader =======================

// [INHERITANCE] InstitutionalTrader "is a" Trader: large position limits, e.g. a hedge fund.
class InstitutionalTrader : public Trader {
public:
    explicit InstitutionalTrader(const std::string& name) : Trader(name) {}
    int         maxOrderSize() const override { return 10000; }
    std::string category()     const override { return "Institutional"; }
};
