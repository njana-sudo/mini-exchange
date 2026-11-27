// exchange_test.cpp - Google Test suite covering matching invariants,
// integer ticks, partial fills, stop-loss triggers/ordering/cascades,
// cancel/modify, STP, validation, non-burning IDs, and deterministic clock reports.

#include "Exchange.hpp"
#include "Trader.hpp"
#include "Types.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace {

class ExchangeTest : public ::testing::Test {
protected:
    Exchange ex;

    void SetUp() override {
        ex.registerTrader(std::make_unique<RetailTrader>("Asha"));
        ex.registerTrader(std::make_unique<RetailTrader>("Ravi"));
        ex.registerTrader(std::make_unique<InstitutionalTrader>("AlphaFund"));
    }

    void TearDown() override {
        ex.book().checkInvariants();
    }
};

// ======================= 1. Integer tick prices & validation =======================

TEST(TickPriceTest, EliminatesFloatingPointDrift) {
    EXPECT_EQ(to_ticks(100.0 + 0.1 + 0.2), to_ticks(100.3));
    EXPECT_EQ(to_ticks(101.50), 10150);
    EXPECT_DOUBLE_EQ(to_rupees(10150), 101.50);
}

TEST_F(ExchangeTest, FloatDriftPricesMatchAtExactSameTick) {
    ex.placeOrder("Asha", Side::SELL, 10, 100.1 + 0.2);
    ex.placeOrder("Ravi", Side::BUY,  10, 100.3);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].price(), to_ticks(100.3));
    EXPECT_EQ(ex.book().openOrderCount(), 0u);
}

TEST_F(ExchangeTest, RejectsInvalidHugeAndNonFinitePrices) {
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, 0.0), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, -5.0), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, 0.004), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, 1e30), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, std::numeric_limits<double>::quiet_NaN()),
                 InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, std::numeric_limits<double>::infinity()),
                 InvalidOrderException);
}

TEST_F(ExchangeTest, RejectedOrdersDoNotBurnOrderIds) {
    OrderId id1 = ex.placeOrder("Asha", Side::BUY, 10, 100.00);
    EXPECT_EQ(id1, 1u);

    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 0,   100.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 10,  -5.00),  InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 10,   1e30),  InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 500, 100.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Ghost", Side::BUY, 10,  100.00), InvalidOrderException);

    OrderId id2 = ex.placeOrder("Ravi", Side::BUY, 10, 99.00);
    EXPECT_EQ(id2, 2u);
}

// ======================= 2. Price-time priority, sweeps & partial fills =======================

TEST_F(ExchangeTest, BestPriceMatchesFirstThenTimePriority) {
    OrderId sellHigh = ex.placeOrder("Asha", Side::SELL, 10, 102.00);
    OrderId sellLow1 = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);
    OrderId sellLow2 = ex.placeOrder("Asha", Side::SELL, 10, 100.00);

    OrderId buyId = ex.placeOrder("AlphaFund", Side::BUY, 15, 102.00);

    const auto& trades = ex.book().trades();
    ASSERT_EQ(trades.size(), 2u);

    EXPECT_EQ(trades[0].buyOrderId(),  buyId);
    EXPECT_EQ(trades[0].sellOrderId(), sellLow1);
    EXPECT_EQ(trades[0].price(),       to_ticks(100.00));
    EXPECT_EQ(trades[0].quantity(),    10);

    EXPECT_EQ(trades[1].buyOrderId(),  buyId);
    EXPECT_EQ(trades[1].sellOrderId(), sellLow2);
    EXPECT_EQ(trades[1].price(),       to_ticks(100.00));
    EXPECT_EQ(trades[1].quantity(),    5);

    EXPECT_FALSE(ex.book().hasOrder(sellLow1));
    EXPECT_TRUE(ex.book().hasOrder(sellLow2));
    EXPECT_EQ(ex.book().remainingQuantity(sellLow2), 5);
    EXPECT_TRUE(ex.book().hasOrder(sellHigh));
    EXPECT_EQ(ex.book().bestAsk(), to_ticks(100.00));
}

TEST_F(ExchangeTest, PartiallyFilledHeadOrderKeepsFrontPosition) {
    OrderId head   = ex.placeOrder("Asha", Side::SELL, 20, 100.00);
    OrderId second = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);

    ex.placeOrder("AlphaFund", Side::BUY, 10, 100.00);
    EXPECT_EQ(ex.book().remainingQuantity(head), 10);

    // Next buy of 15 must finish head's remaining 10 before touching second
    ex.placeOrder("AlphaFund", Side::BUY, 15, 100.00);
    const auto& trades = ex.book().trades();
    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[1].sellOrderId(), head);
    EXPECT_EQ(trades[1].quantity(),    10);
    EXPECT_EQ(trades[2].sellOrderId(), second);
    EXPECT_EQ(trades[2].quantity(),    5);
    EXPECT_EQ(ex.book().remainingQuantity(second), 5);
}

TEST_F(ExchangeTest, IncomingSellCrossesRestingBidAtBidPrice) {
    OrderId bid  = ex.placeOrder("AlphaFund", Side::BUY,  20, 105.00);
    OrderId sell = ex.placeOrder("Asha",      Side::SELL, 15, 101.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    const Trade& t = ex.book().trades()[0];
    EXPECT_EQ(t.buyOrderId(),  bid);
    EXPECT_EQ(t.sellOrderId(), sell);
    EXPECT_EQ(t.buyer(),       "AlphaFund");
    EXPECT_EQ(t.seller(),      "Asha");
    EXPECT_EQ(t.price(),       to_ticks(105.00));
    EXPECT_EQ(t.quantity(),    15);
    EXPECT_EQ(ex.book().remainingQuantity(bid), 5);
}

TEST_F(ExchangeTest, SweepsMultiplePriceLevelsLeavingRemainder) {
    ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    ex.placeOrder("Ravi", Side::SELL, 10, 101.00);
    ex.placeOrder("Asha", Side::SELL, 10, 102.00);
    OrderId fourth = ex.placeOrder("Ravi", Side::SELL, 10, 103.00);

    ex.placeOrder("AlphaFund", Side::BUY, 35, 103.00);

    const auto& trades = ex.book().trades();
    ASSERT_EQ(trades.size(), 4u);
    EXPECT_EQ(trades[0].price(), to_ticks(100.00));
    EXPECT_EQ(trades[1].price(), to_ticks(101.00));
    EXPECT_EQ(trades[2].price(), to_ticks(102.00));
    EXPECT_EQ(trades[3].price(), to_ticks(103.00));
    EXPECT_EQ(trades[3].quantity(), 5);
    EXPECT_EQ(ex.book().remainingQuantity(fourth), 5);
    EXPECT_EQ(ex.book().bestAsk(), to_ticks(103.00));
}

TEST_F(ExchangeTest, PartialFillRestsRemainderInBook) {
    ex.placeOrder("Asha", Side::SELL, 30, 101.00);
    OrderId buyId = ex.placeOrder("AlphaFund", Side::BUY, 50, 101.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].quantity(), 30);
    EXPECT_TRUE(ex.book().hasOrder(buyId));
    EXPECT_EQ(ex.book().remainingQuantity(buyId), 20);
    EXPECT_EQ(ex.book().bestBid(), to_ticks(101.00));
    EXPECT_EQ(ex.book().bestAsk(), std::nullopt);
}

TEST_F(ExchangeTest, MarketOrderDoesNotRestWhenLiquidityExhausted) {
    ex.placeOrder("Asha", Side::SELL, 10, 101.00);
    OrderId mktId = ex.placeOrder("AlphaFund", Side::BUY, 25);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].quantity(), 10);
    EXPECT_FALSE(ex.book().hasOrder(mktId));
    EXPECT_EQ(ex.book().openOrderCount(), 0u);
}

// ======================= 3. Stop-loss triggers, ordering & cascades =======================

TEST_F(ExchangeTest, StopLossDoesNotTriggerOnWrongSideAndRejectsAlreadyCrossed) {
    // Establish last traded price = 100.00
    ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    ex.placeOrder("Ravi", Side::BUY,  10, 100.00);
    EXPECT_EQ(ex.book().lastTradePrice(), to_ticks(100.00));

    // Already-crossed sell SL (trigger 105 >= LTP 100) and buy SL (trigger 95 <= LTP 100) reject
    EXPECT_THROW(ex.placeOrder("Asha", Side::SELL, 10, 105.00, 104.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Ravi", Side::BUY,  10,  95.00,  96.00), InvalidOrderException);

    // Valid sell SL below LTP (trigger 98) stays pending when an unrelated bid at 50 rests
    // or when a trade executes above trigger (at 99)
    OrderId sellSl = ex.placeOrder("Asha", Side::SELL, 10, 98.00, 97.50);
    ex.placeOrder("AlphaFund", Side::BUY, 10, 50.00);
    EXPECT_TRUE(ex.book().hasOrder(sellSl));
    EXPECT_EQ(ex.book().bestAsk(), std::nullopt);

    ex.placeOrder("AlphaFund", Side::BUY, 10, 99.00);
    ex.placeOrder("Ravi", Side::SELL, 10, 99.00);
    EXPECT_TRUE(ex.book().hasOrder(sellSl));
    EXPECT_EQ(ex.book().bestAsk(), std::nullopt);
}

TEST_F(ExchangeTest, StopLossFiresInTriggerPriceThenTimeOrder) {
    // Liquidity waiting below
    ex.placeOrder("AlphaFund", Side::BUY, 10, 95.00);
    ex.placeOrder("AlphaFund", Side::BUY, 10, 94.00);
    ex.placeOrder("AlphaFund", Side::BUY, 10, 93.00);

    // Lower trigger placed first, higher triggers placed later -> 98 must fire before 96, FIFO within 98
    OrderId sl96  = ex.placeOrder("Asha", Side::SELL, 10, 96.00, 92.00);
    OrderId sl98a = ex.placeOrder("Ravi", Side::SELL, 10, 98.00, 92.00);
    OrderId sl98b = ex.placeOrder("Asha", Side::SELL, 10, 98.00, 92.00);

    // Trade at 95 crosses all three
    ex.placeOrder("Ravi", Side::SELL, 10, 95.00);

    const auto& trades = ex.book().trades();
    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[1].sellOrderId(), sl98a);
    EXPECT_EQ(trades[2].sellOrderId(), sl98b);
    // sl96 activates as resting ask at 92.00 after 94 & 93 bids were consumed
    EXPECT_TRUE(ex.book().hasOrder(sl96));
    EXPECT_EQ(ex.book().bestAsk(), to_ticks(92.00));
}

TEST_F(ExchangeTest, StopLossStaysPendingUntilTriggerAndCascades) {
    ex.placeOrder("AlphaFund", Side::BUY, 20, 99.00);
    ex.placeOrder("AlphaFund", Side::BUY, 15, 96.00);
    ex.placeOrder("AlphaFund", Side::BUY, 20, 93.00);

    OrderId sl1 = ex.placeOrder("Asha", Side::SELL, 15, 99.00, 95.00);
    OrderId sl2 = ex.placeOrder("Ravi", Side::SELL, 10, 96.00, 92.00);

    EXPECT_EQ(ex.book().bestAsk(), std::nullopt);
    EXPECT_TRUE(ex.book().hasOrder(sl1));
    EXPECT_TRUE(ex.book().hasOrder(sl2));

    ex.placeOrder("Ravi", Side::SELL, 20, 99.00);

    const auto& trades = ex.book().trades();
    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].price(), to_ticks(99.00));
    EXPECT_EQ(trades[1].sellOrderId(), sl1);
    EXPECT_EQ(trades[1].price(), to_ticks(96.00));
    EXPECT_EQ(trades[2].sellOrderId(), sl2);
    EXPECT_EQ(trades[2].price(), to_ticks(93.00));

    EXPECT_FALSE(ex.book().hasOrder(sl1));
    EXPECT_FALSE(ex.book().hasOrder(sl2));
}

TEST_F(ExchangeTest, BuyStopLossTriggersOnPriceRise) {
    ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    ex.placeOrder("Asha", Side::SELL, 10, 102.00);

    OrderId buySl = ex.placeOrder("Ravi", Side::BUY, 10, 100.00, 102.00);
    EXPECT_EQ(ex.book().bestBid(), std::nullopt);

    ex.placeOrder("AlphaFund", Side::BUY, 10, 100.00);

    ASSERT_EQ(ex.book().trades().size(), 2u);
    EXPECT_EQ(ex.book().trades()[1].buyOrderId(), buySl);
    EXPECT_EQ(ex.book().trades()[1].price(), to_ticks(102.00));
}

// ======================= 4. O(1) Cancel & Modify =======================

TEST_F(ExchangeTest, CancelRemovesLimitAndPendingStopLoss) {
    OrderId bid = ex.placeOrder("Asha", Side::BUY, 20, 95.00);
    OrderId sl  = ex.placeOrder("Asha", Side::SELL, 10, 90.00, 89.00);

    EXPECT_EQ(ex.book().openOrderCount(), 2u);
    ex.cancelOrder("Asha", bid);
    EXPECT_FALSE(ex.book().hasOrder(bid));
    EXPECT_EQ(ex.book().bestBid(), std::nullopt);

    ex.cancelOrder("Asha", sl);
    EXPECT_FALSE(ex.book().hasOrder(sl));
    EXPECT_EQ(ex.book().openOrderCount(), 0u);

    EXPECT_THROW(ex.cancelOrder("Asha", bid), InvalidOrderException);
}

TEST_F(ExchangeTest, CancelEnforcesOwnershipAndRejectsFilledOrders) {
    OrderId sell = ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    EXPECT_THROW(ex.cancelOrder("Ravi", sell), InvalidOrderException);
    EXPECT_TRUE(ex.book().hasOrder(sell));

    ex.placeOrder("Ravi", Side::BUY, 10, 100.00);
    EXPECT_THROW(ex.cancelOrder("Asha", sell), InvalidOrderException);
}

TEST_F(ExchangeTest, CannotModifyPendingStopLoss) {
    OrderId sl = ex.placeOrder("Asha", Side::SELL, 10, 90.00, 89.00);
    EXPECT_THROW(ex.modifyOrder("Asha", sl, 5, 88.00), InvalidOrderException);
    EXPECT_TRUE(ex.book().hasOrder(sl));
    EXPECT_EQ(ex.book().remainingQuantity(sl), 10);
}

TEST_F(ExchangeTest, ModifyQtyDownPreservesTimePriority) {
    OrderId first  = ex.placeOrder("Asha", Side::SELL, 20, 100.00);
    OrderId second = ex.placeOrder("Ravi", Side::SELL, 20, 100.00);

    ex.modifyOrder("Asha", first, 10, 100.00);
    EXPECT_EQ(ex.book().remainingQuantity(first), 10);

    ex.placeOrder("AlphaFund", Side::BUY, 15, 100.00);

    const auto& trades = ex.book().trades();
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].sellOrderId(), first);
    EXPECT_EQ(trades[0].quantity(),    10);
    EXPECT_EQ(trades[1].sellOrderId(), second);
    EXPECT_EQ(trades[1].quantity(),    5);
}

TEST_F(ExchangeTest, ModifyQtyUpLosesTimePriority) {
    OrderId first  = ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    OrderId second = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);

    ex.modifyOrder("Asha", first, 15, 100.00);

    ex.placeOrder("AlphaFund", Side::BUY, 10, 100.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].sellOrderId(), second);
    EXPECT_TRUE(ex.book().hasOrder(first));
}

TEST_F(ExchangeTest, ModifyPriceCrossingSpreadMatchesImmediately) {
    OrderId ask = ex.placeOrder("Asha", Side::SELL, 10, 102.00);
    OrderId bid = ex.placeOrder("Ravi", Side::BUY,  10, 99.00);

    ex.modifyOrder("Ravi", bid, 10, 102.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].buyOrderId(),  bid);
    EXPECT_EQ(ex.book().trades()[0].sellOrderId(), ask);
    EXPECT_EQ(ex.book().openOrderCount(), 0u);
}

TEST_F(ExchangeTest, FailedModifyLeavesOriginalOrderIntact) {
    OrderId bid = ex.placeOrder("Asha", Side::BUY, 20, 95.00);

    EXPECT_THROW(ex.modifyOrder("Asha", bid, 200, 95.00), InvalidOrderException);
    EXPECT_THROW(ex.modifyOrder("Asha", bid, 10, -1.0),  InvalidOrderException);
    EXPECT_THROW(ex.modifyOrder("Ravi", bid, 10, 95.00), InvalidOrderException);

    EXPECT_TRUE(ex.book().hasOrder(bid));
    EXPECT_EQ(ex.book().remainingQuantity(bid), 20);
}

// ======================= 5. Self-trade prevention =======================

TEST_F(ExchangeTest, StpCancelOldestCancelsOwnRestingAndMatchesThirdParty) {
    ex.setStpMode(StpMode::CANCEL_OLDEST);
    OrderId ownAsk   = ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    OrderId otherAsk = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);

    OrderId buyId = ex.placeOrder("Asha", Side::BUY, 10, 100.00);

    EXPECT_FALSE(ex.book().hasOrder(ownAsk));
    EXPECT_FALSE(ex.book().hasOrder(otherAsk));
    EXPECT_FALSE(ex.book().hasOrder(buyId));

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].buyer(),  "Asha");
    EXPECT_EQ(ex.book().trades()[0].seller(), "Ravi");
}

TEST_F(ExchangeTest, StpCancelNewestPreventsSelfTradeAndCrossedBook) {
    ex.setStpMode(StpMode::CANCEL_NEWEST);
    OrderId otherAsk = ex.placeOrder("Ravi", Side::SELL, 10, 99.00);
    OrderId ownAsk   = ex.placeOrder("Asha", Side::SELL, 10, 100.00);

    OrderId buyId = ex.placeOrder("Asha", Side::BUY, 25, 100.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].seller(),   "Ravi");
    EXPECT_EQ(ex.book().trades()[0].quantity(), 10);

    EXPECT_FALSE(ex.book().hasOrder(otherAsk));
    EXPECT_TRUE(ex.book().hasOrder(ownAsk));
    EXPECT_FALSE(ex.book().hasOrder(buyId));
    EXPECT_EQ(ex.book().bestBid(), std::nullopt);
    EXPECT_EQ(ex.book().bestAsk(), to_ticks(100.00));
}

// ======================= 6. Validation & deterministic TraderReport =======================

TEST_F(ExchangeTest, EnforcesTraderRegistrationAndOrderLimits) {
    EXPECT_THROW(ex.placeOrder("Ghost", Side::BUY, 10,  100.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 0,   100.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 101, 100.00), InvalidOrderException);
    EXPECT_NO_THROW(ex.placeOrder("AlphaFund", Side::BUY, 500, 100.00));
}

TEST_F(ExchangeTest, TraderReportComputesAccuratePnL) {
    ex.placeOrder("Asha",      Side::SELL, 50, 100.00);
    ex.placeOrder("AlphaFund", Side::BUY,  50, 100.00);
    ex.placeOrder("Ravi",      Side::BUY,  20, 110.00);
    ex.placeOrder("AlphaFund", Side::SELL, 20, 110.00);

    TraderReport r = ex.buildTraderReport("AlphaFund", 1);
    EXPECT_EQ(r.totalBought(), 50);
    EXPECT_EQ(r.totalSold(),   20);
    EXPECT_EQ(r.netPosition(), 30);
    EXPECT_DOUBLE_EQ(r.avgBuyPrice(),  100.00);
    EXPECT_DOUBLE_EQ(r.avgSellPrice(), 110.00);
    EXPECT_DOUBLE_EQ(r.realizedPnL(),  200.00);
}

TEST(TraderReportWindowTest, ExcludesTradesOlderThanRequestedMonths) {
    std::time_t fakeNow = 1700000000;
    Exchange timedEx(StpMode::CANCEL_OLDEST, [&fakeNow]() { return fakeNow; });
    timedEx.registerTrader(std::make_unique<RetailTrader>("Asha"));
    timedEx.registerTrader(std::make_unique<InstitutionalTrader>("AlphaFund"));

    // Day 0: AlphaFund buys 10 @ 200.00
    timedEx.placeOrder("Asha",      Side::SELL, 10, 200.00);
    timedEx.placeOrder("AlphaFund", Side::BUY,  10, 200.00);

    // Advance clock by 65 days (> 2 months)
    fakeNow += static_cast<std::time_t>(65) * 24 * 3600;

    // Day 65: AlphaFund buys 10 @ 100.00
    timedEx.placeOrder("Asha",      Side::SELL, 10, 100.00);
    timedEx.placeOrder("AlphaFund", Side::BUY,  10, 100.00);

    TraderReport oneMonth = timedEx.buildTraderReport("AlphaFund", 1);
    EXPECT_EQ(oneMonth.totalBought(), 10);
    EXPECT_DOUBLE_EQ(oneMonth.avgBuyPrice(), 100.00);

    TraderReport allTime = timedEx.buildTraderReport("AlphaFund", 0);
    EXPECT_EQ(allTime.totalBought(), 20);
    EXPECT_DOUBLE_EQ(allTime.avgBuyPrice(), 150.00);
}

// ======================= 7. Randomized differential test vs naive O(N) book =======================

struct NaiveOrder {
    OrderId     id;
    std::string trader;
    Side        side;
    Quantity    qty;
    Price       price;
    uint64_t    seq;
};

struct NaiveStop {
    OrderId     id;
    std::string trader;
    Side        side;
    Quantity    qty;
    Price       trigger;
    Price       limit;
    uint64_t    seq;
};

struct NaiveTrade {
    OrderId     buyId;
    OrderId     sellId;
    std::string buyer;
    std::string seller;
    Price       price;
    Quantity    qty;
};

// Simple O(N) linear-scan reference model with zero maps/lists/iterators.
class NaiveExchange {
public:
    StpMode                 stpMode;
    OrderId                 nextId = 1;
    uint64_t                seq    = 1;
    std::vector<NaiveOrder> resting;
    std::vector<NaiveStop>  stops;
    std::vector<NaiveTrade> trades;
    std::optional<Price>    lastTradePrice;

    explicit NaiveExchange(StpMode mode) : stpMode(mode) {}

    static Quantity maxOrderSizeFor(const std::string& trader) {
        return (trader == "AlphaFund") ? 10000 : 100;
    }

    bool matchIncoming(OrderId id, const std::string& trader, Side side,
                       Quantity& qty, bool isMarket, Price limitPrice) {
        while (qty > 0) {
            int bestIdx = -1;
            for (int i = 0; i < static_cast<int>(resting.size()); ++i) {
                const auto& r = resting[static_cast<size_t>(i)];
                if (r.side == side) continue;
                if (!isMarket) {
                    bool crosses = (side == Side::BUY) ? (limitPrice >= r.price)
                                                       : (limitPrice <= r.price);
                    if (!crosses) continue;
                }
                if (bestIdx == -1) {
                    bestIdx = i;
                } else {
                    const auto& b = resting[static_cast<size_t>(bestIdx)];
                    bool betterPrice = (side == Side::BUY) ? (r.price < b.price)
                                                           : (r.price > b.price);
                    if (betterPrice || (r.price == b.price && r.seq < b.seq)) {
                        bestIdx = i;
                    }
                }
            }
            if (bestIdx == -1) break;

            auto& opp = resting[static_cast<size_t>(bestIdx)];
            if (opp.trader == trader) {
                if (stpMode == StpMode::CANCEL_OLDEST) {
                    resting.erase(resting.begin() + bestIdx);
                    continue;
                }
                return true;  // CANCEL_NEWEST
            }

            Quantity fill = std::min(qty, opp.qty);
            qty     -= fill;
            opp.qty -= fill;

            OrderId     buyId  = (side == Side::BUY) ? id : opp.id;
            OrderId     sellId = (side == Side::SELL) ? id : opp.id;
            std::string buyer  = (side == Side::BUY) ? trader : opp.trader;
            std::string seller = (side == Side::SELL) ? trader : opp.trader;
            trades.push_back({buyId, sellId, buyer, seller, opp.price, fill});
            lastTradePrice = opp.price;

            if (opp.qty == 0) {
                resting.erase(resting.begin() + bestIdx);
            }
        }
        return false;
    }

    void checkStops() {
        if (!lastTradePrice.has_value()) return;
        while (true) {
            Price lp = *lastTradePrice;
            int bestIdx = -1;
            for (int i = 0; i < static_cast<int>(stops.size()); ++i) {
                const auto& s = stops[static_cast<size_t>(i)];
                if (s.side != Side::SELL || s.trigger < lp) continue;
                if (bestIdx == -1) {
                    bestIdx = i;
                } else {
                    const auto& b = stops[static_cast<size_t>(bestIdx)];
                    if (s.trigger > b.trigger || (s.trigger == b.trigger && s.seq < b.seq))
                        bestIdx = i;
                }
            }
            if (bestIdx == -1) {
                for (int i = 0; i < static_cast<int>(stops.size()); ++i) {
                    const auto& s = stops[static_cast<size_t>(i)];
                    if (s.side != Side::BUY || s.trigger > lp) continue;
                    if (bestIdx == -1) {
                        bestIdx = i;
                    } else {
                        const auto& b = stops[static_cast<size_t>(bestIdx)];
                        if (s.trigger < b.trigger || (s.trigger == b.trigger && s.seq < b.seq))
                            bestIdx = i;
                    }
                }
            }
            if (bestIdx == -1) break;

            NaiveStop s = stops[static_cast<size_t>(bestIdx)];
            stops.erase(stops.begin() + bestIdx);
            bool stpCancelled = matchIncoming(s.id, s.trader, s.side, s.qty, false, s.limit);
            if (!stpCancelled && s.qty > 0) {
                resting.push_back({s.id, s.trader, s.side, s.qty, s.limit, seq++});
            }
        }
    }

    OrderId placeLimit(const std::string& trader, Side side, Quantity qty, double rupees) {
        if (qty <= 0 || qty > maxOrderSizeFor(trader)) throw InvalidOrderException("bad qty");
        Price p = validateAndConvertPrice(rupees);
        OrderId id = nextId++;
        size_t before = trades.size();
        bool stp = matchIncoming(id, trader, side, qty, false, p);
        if (!stp && qty > 0) resting.push_back({id, trader, side, qty, p, seq++});
        if (trades.size() > before) checkStops();
        return id;
    }

    OrderId placeMarket(const std::string& trader, Side side, Quantity qty) {
        if (qty <= 0 || qty > maxOrderSizeFor(trader)) throw InvalidOrderException("bad qty");
        OrderId id = nextId++;
        size_t before = trades.size();
        matchIncoming(id, trader, side, qty, true, 0);
        if (trades.size() > before) checkStops();
        return id;
    }

    OrderId placeStop(const std::string& trader, Side side, Quantity qty,
                      double trigRupees, double limRupees) {
        if (qty <= 0 || qty > maxOrderSizeFor(trader)) throw InvalidOrderException("bad qty");
        Price trig = validateAndConvertPrice(trigRupees);
        Price lim  = validateAndConvertPrice(limRupees);
        if (lastTradePrice.has_value()) {
            bool crossed = (side == Side::SELL) ? (*lastTradePrice <= trig)
                                                : (*lastTradePrice >= trig);
            if (crossed) throw InvalidOrderException("crossed stop");
        }
        OrderId id = nextId++;
        stops.push_back({id, trader, side, qty, trig, lim, seq++});
        return id;
    }

    void cancel(const std::string& trader, OrderId id) {
        for (size_t i = 0; i < resting.size(); ++i) {
            if (resting[i].id == id) {
                if (resting[i].trader != trader) throw InvalidOrderException("owner");
                resting.erase(resting.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        for (size_t i = 0; i < stops.size(); ++i) {
            if (stops[i].id == id) {
                if (stops[i].trader != trader) throw InvalidOrderException("owner");
                stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        throw InvalidOrderException("not found");
    }

    void modify(const std::string& trader, OrderId id, Quantity newQty, double newRupees) {
        if (newQty <= 0 || newQty > maxOrderSizeFor(trader)) throw InvalidOrderException("bad qty");
        Price newPrice = validateAndConvertPrice(newRupees);
        for (size_t i = 0; i < stops.size(); ++i) {
            if (stops[i].id == id) throw InvalidOrderException("pending stop");
        }
        for (size_t i = 0; i < resting.size(); ++i) {
            if (resting[i].id == id) {
                if (resting[i].trader != trader) throw InvalidOrderException("owner");
                if (newPrice == resting[i].price && newQty <= resting[i].qty) {
                    resting[i].qty = newQty;
                    return;
                }
                NaiveOrder ord = resting[i];
                resting.erase(resting.begin() + static_cast<std::ptrdiff_t>(i));
                ord.qty   = newQty;
                ord.price = newPrice;
                size_t before = trades.size();
                bool stp = matchIncoming(ord.id, ord.trader, ord.side, ord.qty, false, ord.price);
                if (!stp && ord.qty > 0) {
                    ord.seq = seq++;
                    resting.push_back(ord);
                }
                if (trades.size() > before) checkStops();
                return;
            }
        }
        throw InvalidOrderException("not found");
    }

    size_t openOrderCount() const { return resting.size() + stops.size(); }

    std::optional<Price> bestBid() const {
        std::optional<Price> best;
        for (const auto& o : resting)
            if (o.side == Side::BUY && (!best || o.price > *best)) best = o.price;
        return best;
    }

    std::optional<Price> bestAsk() const {
        std::optional<Price> best;
        for (const auto& o : resting)
            if (o.side == Side::SELL && (!best || o.price < *best)) best = o.price;
        return best;
    }
};

void runDifferentialTrace(uint32_t seed, StpMode mode, int steps) {
    // Silence demo stdout during randomized fuzzing; restore via RAII on exit
    struct CoutSilencer {
        std::streambuf* prev;
        CoutSilencer() : prev(std::cout.rdbuf(nullptr)) {}
        ~CoutSilencer() { std::cout.rdbuf(prev); }
    } silencer;

    std::time_t fakeNow = 1700000000;
    Exchange      ex(mode, [&fakeNow]() { return fakeNow; });
    NaiveExchange ref(mode);

    ex.registerTrader(std::make_unique<RetailTrader>("Asha"));
    ex.registerTrader(std::make_unique<RetailTrader>("Ravi"));
    ex.registerTrader(std::make_unique<InstitutionalTrader>("AlphaFund"));

    const std::vector<std::string> traders = {"Asha", "Ravi", "AlphaFund"};
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> opDist(0, 9);
    std::uniform_int_distribution<int> traderDist(0, 2);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 30);
    std::uniform_int_distribution<int> priceTicksDist(90, 110);  // whole rupees 90..110

    std::vector<OrderId> seenIds;

    for (int step = 0; step < steps; ++step) {
        ++fakeNow;
        int op = opDist(rng);
        const std::string& trader = traders[static_cast<size_t>(traderDist(rng))];
        Side side = (sideDist(rng) == 0) ? Side::BUY : Side::SELL;
        Quantity qty = qtyDist(rng);
        double price = static_cast<double>(priceTicksDist(rng));

        bool exThrew  = false;
        bool refThrew = false;

        if (op <= 3) {  // 40% limit
            OrderId eid = 0, rid = 0;
            try { eid = ex.placeOrder(trader, side, qty, price); } catch (const InvalidOrderException&) { exThrew = true; }
            try { rid = ref.placeLimit(trader, side, qty, price); } catch (const InvalidOrderException&) { refThrew = true; }
            ASSERT_EQ(exThrew, refThrew) << "seed=" << seed << " step=" << step;
            if (!exThrew) {
                ASSERT_EQ(eid, rid);
                seenIds.push_back(eid);
            }
        } else if (op == 4) {  // 10% market
            OrderId eid = 0, rid = 0;
            try { eid = ex.placeOrder(trader, side, qty); } catch (const InvalidOrderException&) { exThrew = true; }
            try { rid = ref.placeMarket(trader, side, qty); } catch (const InvalidOrderException&) { refThrew = true; }
            ASSERT_EQ(exThrew, refThrew) << "seed=" << seed << " step=" << step;
            if (!exThrew) {
                ASSERT_EQ(eid, rid);
                seenIds.push_back(eid);
            }
        } else if (op <= 6) {  // 20% stop-loss (valid + intentionally crossed triggers)
            double trig  = static_cast<double>(priceTicksDist(rng));
            double limit = static_cast<double>(priceTicksDist(rng));
            OrderId eid = 0, rid = 0;
            try { eid = ex.placeOrder(trader, side, qty, trig, limit); } catch (const InvalidOrderException&) { exThrew = true; }
            try { rid = ref.placeStop(trader, side, qty, trig, limit); } catch (const InvalidOrderException&) { refThrew = true; }
            ASSERT_EQ(exThrew, refThrew) << "seed=" << seed << " step=" << step;
            if (!exThrew) {
                ASSERT_EQ(eid, rid);
                seenIds.push_back(eid);
            }
        } else if (op == 7 && !seenIds.empty()) {  // 10% cancel (valid or stale/wrong-owner)
            std::uniform_int_distribution<size_t> idDist(0, seenIds.size() - 1);
            OrderId target = seenIds[idDist(rng)];
            try { ex.cancelOrder(trader, target); } catch (const InvalidOrderException&) { exThrew = true; }
            try { ref.cancel(trader, target); } catch (const InvalidOrderException&) { refThrew = true; }
            ASSERT_EQ(exThrew, refThrew) << "seed=" << seed << " step=" << step;
        } else if (!seenIds.empty()) {  // 20% modify
            std::uniform_int_distribution<size_t> idDist(0, seenIds.size() - 1);
            OrderId target = seenIds[idDist(rng)];
            try { ex.modifyOrder(trader, target, qty, price); } catch (const InvalidOrderException&) { exThrew = true; }
            try { ref.modify(trader, target, qty, price); } catch (const InvalidOrderException&) { refThrew = true; }
            ASSERT_EQ(exThrew, refThrew) << "seed=" << seed << " step=" << step;
        }

        // Verify structural invariants and exact state equivalence after EVERY step
        ex.book().checkInvariants();
        ASSERT_EQ(ex.book().openOrderCount(), ref.openOrderCount()) << "step=" << step;
        ASSERT_EQ(ex.book().bestBid(),        ref.bestBid())        << "step=" << step;
        ASSERT_EQ(ex.book().bestAsk(),        ref.bestAsk())        << "step=" << step;
        ASSERT_EQ(ex.book().lastTradePrice(), ref.lastTradePrice)   << "step=" << step;

        const auto& exTrades = ex.book().trades();
        ASSERT_EQ(exTrades.size(), ref.trades.size()) << "step=" << step;
        for (size_t i = 0; i < exTrades.size(); ++i) {
            ASSERT_EQ(exTrades[i].buyOrderId(),  ref.trades[i].buyId)  << "trade " << i;
            ASSERT_EQ(exTrades[i].sellOrderId(), ref.trades[i].sellId) << "trade " << i;
            ASSERT_EQ(exTrades[i].buyer(),       ref.trades[i].buyer)  << "trade " << i;
            ASSERT_EQ(exTrades[i].seller(),      ref.trades[i].seller) << "trade " << i;
            ASSERT_EQ(exTrades[i].price(),       ref.trades[i].price)  << "trade " << i;
            ASSERT_EQ(exTrades[i].quantity(),    ref.trades[i].qty)    << "trade " << i;
        }
    }
}

TEST(DifferentialTest, MatchesNaiveBookInCancelOldestMode) {
    runDifferentialTrace(42u,   StpMode::CANCEL_OLDEST, 1000);
    runDifferentialTrace(1337u, StpMode::CANCEL_OLDEST, 1000);
}

TEST(DifferentialTest, MatchesNaiveBookInCancelNewestMode) {
    runDifferentialTrace(7u,    StpMode::CANCEL_NEWEST, 1000);
    runDifferentialTrace(2026u, StpMode::CANCEL_NEWEST, 1000);
}

}  // namespace
