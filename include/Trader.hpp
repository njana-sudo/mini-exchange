#pragma once
// Trader.hpp - abstract Trader base class and concrete subtypes.

#include "Types.hpp"

#include <string>
#include <utility>

class Trader {
protected:
    std::string name_;

public:
    explicit Trader(std::string name) : name_(std::move(name)) {}
    virtual ~Trader() = default;

    const std::string& name() const { return name_; }

    virtual Quantity    maxOrderSize() const = 0;
    virtual std::string category()     const = 0;
};

class RetailTrader : public Trader {
public:
    explicit RetailTrader(const std::string& name) : Trader(name) {}
    Quantity    maxOrderSize() const override { return 100; }
    std::string category()     const override { return "Retail"; }
};

class InstitutionalTrader : public Trader {
public:
    explicit InstitutionalTrader(const std::string& name) : Trader(name) {}
    Quantity    maxOrderSize() const override { return 10000; }
    std::string category()     const override { return "Institutional"; }
};
