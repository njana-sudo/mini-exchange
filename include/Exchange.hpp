#pragma once
// Exchange.hpp - top-level facade owning OrderBook, Traders, continuous TraderLedgers,
//                per-exchange OrderId generator, and optional append-only OrderJournal.

#include "OrderBook.hpp"
#include "OrderJournal.hpp"
#include "Trader.hpp"
#include "TraderLedger.hpp"
#include "TraderReport.hpp"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

class Exchange {
private:
    Clock                                          clock_;
    OrderBook                                      book_;
    std::map<std::string, std::unique_ptr<Trader>> traders_;
    std::unordered_map<std::string, TraderLedger>  ledgers_;
    OrderJournal                                   journal_;
    OrderId                                        nextOrderId_ = 1;

    void validate(const std::string& traderName, Quantity qty) const;
    void updateLedgersFrom(size_t startTradeIdx);

    OrderId submitLimitInternal(const std::string& trader, Side side, Quantity qty,
                                Price priceTicks, std::time_t ts);
    OrderId submitMarketInternal(const std::string& trader, Side side, Quantity qty,
                                 std::time_t ts);
    OrderId submitStopInternal(const std::string& trader, Side side, Quantity qty,
                               Price triggerTicks, Price limitTicks, std::time_t ts);
    void    modifyInternal(const std::string& trader, OrderId orderId,
                           Quantity newQty, Price newPriceTicks, std::time_t ts);

public:
    explicit Exchange(StpMode stpMode = StpMode::CANCEL_OLDEST,
                      Clock clock = systemClock())
        : clock_(clock), book_(stpMode, clock) {}

    void enableJournal(const std::string& path, bool truncate = true, bool flushEachWrite = true);
    void closeJournal() { journal_.close(); }

    void    registerTrader(std::unique_ptr<Trader> trader);
    void    setStpMode(StpMode mode);
    StpMode stpMode() const { return book_.stpMode(); }

    OrderId placeOrder(const std::string& trader, Side side, Quantity qty, double price);
    OrderId placeOrder(const std::string& trader, Side side, Quantity qty);
    OrderId placeOrder(const std::string& trader, Side side, Quantity qty,
                       double triggerPrice, double limitPrice);

    void cancelOrder(const std::string& trader, OrderId orderId);
    void modifyOrder(const std::string& trader, OrderId orderId, Quantity newQty, double newPrice);

    const TraderLedger& ledger(const std::string& name) const;
    TraderReport        buildTraderReport(const std::string& name, int months = 0) const;
    void                traderReport(const std::string& name, int months = 0) const;

    void listTraders() const;

    const OrderBook& book() const { return book_; }

    // Reconstructs an identical Exchange by replaying an append-only journal.
    static Exchange replayFromJournal(const std::string& path, Clock fallbackClock = systemClock());
};
