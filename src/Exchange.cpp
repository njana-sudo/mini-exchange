// Exchange.cpp - top-level facade implementation

#include "Exchange.hpp"
#include "Types.hpp"

#include <iomanip>
#include <iostream>
#include <string>

// ======================= private helpers =======================

void Exchange::validate(const std::string& traderName, Quantity qty) const {
    auto it = traders_.find(traderName);
    if (it == traders_.end())
        throw InvalidOrderException("Unknown trader: " + traderName);
    if (qty <= 0)
        throw InvalidOrderException("Quantity must be positive");
    if (qty > it->second->maxOrderSize())
        throw InvalidOrderException(
            traderName + " (" + it->second->category() +
            ") exceeds max order size " +
            std::to_string(it->second->maxOrderSize()));
}

void Exchange::updateLedgersFrom(size_t startTradeIdx) {
    const auto& allTrades = book_.trades();
    for (size_t i = startTradeIdx; i < allTrades.size(); ++i) {
        const Trade& t = allTrades[i];
        ledgers_[t.buyer()].recordFill(i, Side::BUY,  t.price(), t.quantity());
        ledgers_[t.seller()].recordFill(i, Side::SELL, t.price(), t.quantity());
    }
}

OrderId Exchange::submitLimitInternal(const std::string& trader, Side side, Quantity qty,
                                      Price priceTicks, std::time_t ts) {
    validate(trader, qty);
    auto order = LimitOrder::fromTicks(nextOrderId_, trader, side, qty, priceTicks);
    ++nextOrderId_;
    if (journal_.isOpen()) journal_.appendPlaceLimit(ts, trader, side, qty, priceTicks);

    std::cout << "Placed  " << *order << "\n";
    size_t prevTrades = book_.trades().size();
    book_.submit(order, ts);
    updateLedgersFrom(prevTrades);
    return order->id();
}

OrderId Exchange::submitMarketInternal(const std::string& trader, Side side, Quantity qty,
                                       std::time_t ts) {
    validate(trader, qty);
    auto order = std::make_shared<MarketOrder>(nextOrderId_, trader, side, qty);
    ++nextOrderId_;
    if (journal_.isOpen()) journal_.appendPlaceMarket(ts, trader, side, qty);

    std::cout << "Placed  " << *order << "\n";
    size_t prevTrades = book_.trades().size();
    book_.submit(order, ts);
    updateLedgersFrom(prevTrades);
    return order->id();
}

OrderId Exchange::submitStopInternal(const std::string& trader, Side side, Quantity qty,
                                     Price triggerTicks, Price limitTicks, std::time_t ts) {
    validate(trader, qty);
    auto order = StopLossOrder::fromTicks(nextOrderId_, trader, side, qty, triggerTicks, limitTicks);
    if (book_.lastTradePrice().has_value() && order->shouldTrigger(*book_.lastTradePrice())) {
        throw InvalidOrderException(
            "Stop-loss trigger price is already crossed against last traded price");
    }
    ++nextOrderId_;
    if (journal_.isOpen()) journal_.appendPlaceStop(ts, trader, side, qty, triggerTicks, limitTicks);

    std::cout << "Placed  " << *order << "\n";
    size_t prevTrades = book_.trades().size();
    book_.submit(order, ts);
    updateLedgersFrom(prevTrades);
    return order->id();
}

void Exchange::modifyInternal(const std::string& trader, OrderId orderId,
                              Quantity newQty, Price newPriceTicks, std::time_t ts) {
    validate(trader, newQty);
    if (newPriceTicks <= 0) throw InvalidOrderException("Price must be positive");

    size_t prevTrades = book_.trades().size();
    book_.modify(orderId, trader, newQty, newPriceTicks, ts);
    updateLedgersFrom(prevTrades);

    if (journal_.isOpen()) journal_.appendModify(ts, trader, orderId, newQty, newPriceTicks);

    std::cout << "Modified  #" << orderId << " -> qty " << newQty
              << " @ " << std::fixed << std::setprecision(2) << to_rupees(newPriceTicks)
              << " by " << trader << "\n";
}

// ======================= public interface =======================

void Exchange::enableJournal(const std::string& path, bool truncate, bool flushEachWrite) {
    journal_.open(path, truncate, flushEachWrite);
}

void Exchange::registerTrader(std::unique_ptr<Trader> trader) {
    std::string name = trader->name();
    TraderKind  kind = trader->kind();
    traders_[name]   = std::move(trader);
    ledgers_[name];  // ensure every registered trader has a ledger
    if (journal_.isOpen()) journal_.appendRegisterTrader(name, kind);
}

void Exchange::setStpMode(StpMode mode) {
    book_.setStpMode(mode);
    if (journal_.isOpen()) journal_.appendSetStpMode(mode);
}

OrderId Exchange::placeOrder(const std::string& trader, Side side, Quantity qty, double price) {
    Price ticks = validateAndConvertPrice(price, "Price");
    return submitLimitInternal(trader, side, qty, ticks, clock_());
}

OrderId Exchange::placeOrder(const std::string& trader, Side side, Quantity qty) {
    return submitMarketInternal(trader, side, qty, clock_());
}

OrderId Exchange::placeOrder(const std::string& trader, Side side, Quantity qty,
                             double triggerPrice, double limitPrice) {
    Price trigTicks = validateAndConvertPrice(triggerPrice, "Trigger price");
    Price limTicks  = validateAndConvertPrice(limitPrice,   "Limit price");
    return submitStopInternal(trader, side, qty, trigTicks, limTicks, clock_());
}

void Exchange::cancelOrder(const std::string& trader, OrderId orderId) {
    if (traders_.find(trader) == traders_.end())
        throw InvalidOrderException("Unknown trader: " + trader);
    book_.cancel(orderId, trader);
    if (journal_.isOpen()) journal_.appendCancel(clock_(), trader, orderId);
    std::cout << "Cancelled #" << orderId << " by " << trader << "\n";
}

void Exchange::modifyOrder(const std::string& trader, OrderId orderId, Quantity newQty, double newPrice) {
    Price ticks = validateAndConvertPrice(newPrice, "Price");
    modifyInternal(trader, orderId, newQty, ticks, clock_());
}

const TraderLedger& Exchange::ledger(const std::string& name) const {
    auto it = ledgers_.find(name);
    if (it == ledgers_.end())
        throw InvalidOrderException("Unknown trader: " + name);
    return it->second;
}

TraderReport Exchange::buildTraderReport(const std::string& name, int months) const {
    auto ledgerIt = ledgers_.find(name);
    if (ledgerIt == ledgers_.end())
        throw InvalidOrderException("Unknown trader: " + name);

    std::time_t now    = clock_();
    std::time_t cutoff = (months > 0)
        ? now - static_cast<std::time_t>(months) * 30 * 24 * 3600
        : 0;

    TraderReport       report(name, months);
    const auto&        allTrades       = book_.trades();
    int64_t            windowPnLTicks  = 0;

    // Scans only this trader's indexed fills (O(T_trader)), not the entire exchange trade log
    for (const auto& fill : ledgerIt->second.fills()) {
        const Trade& trade = allTrades[fill.tradeIndex];
        if (trade.timestamp() < cutoff) continue;

        windowPnLTicks += fill.realizedDeltaTickShares;
        if (fill.side == Side::BUY) {
            report.addBuy(trade.buyOrderId(), trade.price(), trade.quantity(), trade.timestamp());
        } else {
            report.addSell(trade.sellOrderId(), trade.price(), trade.quantity(), trade.timestamp());
        }
    }
    report.setContinuousRealizedTickShares(windowPnLTicks);
    return report;
}

void Exchange::traderReport(const std::string& name, int months) const {
    if (traders_.find(name) == traders_.end()) {
        std::cout << "Unknown trader: " << name << "\n";
        return;
    }
    std::cout << buildTraderReport(name, months);
}

void Exchange::listTraders() const {
    std::cout << "Registered traders\n";
    for (const auto& [name, trader] : traders_)
        std::cout << "  " << name << " - " << trader->category()
                  << " (max " << trader->maxOrderSize() << ")\n";
}

Exchange Exchange::replayFromJournal(const std::string& path, Clock fallbackClock) {
    auto records = OrderJournal::readAll(path);
    Exchange ex(StpMode::CANCEL_OLDEST, fallbackClock);

    for (const auto& rec : records) {
        switch (rec.op) {
            case LogOp::REGISTER_TRADER:
                if (rec.traderKind == TraderKind::INSTITUTIONAL) {
                    ex.registerTrader(std::make_unique<InstitutionalTrader>(rec.trader));
                } else {
                    ex.registerTrader(std::make_unique<RetailTrader>(rec.trader));
                }
                break;
            case LogOp::SET_STP_MODE:
                ex.setStpMode(rec.stpMode);
                break;
            case LogOp::PLACE_LIMIT:
                ex.submitLimitInternal(rec.trader, rec.side, rec.quantity, rec.priceTicks, rec.timestamp);
                break;
            case LogOp::PLACE_MARKET:
                ex.submitMarketInternal(rec.trader, rec.side, rec.quantity, rec.timestamp);
                break;
            case LogOp::PLACE_STOP:
                ex.submitStopInternal(rec.trader, rec.side, rec.quantity,
                                      rec.triggerTicks, rec.priceTicks, rec.timestamp);
                break;
            case LogOp::CANCEL:
                ex.cancelOrder(rec.trader, rec.orderId);
                break;
            case LogOp::MODIFY:
                ex.modifyInternal(rec.trader, rec.orderId, rec.quantity, rec.priceTicks, rec.timestamp);
                break;
        }
    }
    return ex;
}
