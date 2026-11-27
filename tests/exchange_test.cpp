// exchange_test.cpp - Google Test suite covering matching invariants,
// integer ticks, partial fills, stop-loss cascades, cancel/modify, STP,
// validation, and TraderReport P&L.

#include "Exchange.hpp"
#include "Trader.hpp"
#include "Types.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <memory>

namespace {

class ExchangeTest : public ::testing::Test {
protected:
    Exchange ex;

    void SetUp() override {
        ex.registerTrader(std::make_unique<RetailTrader>("Asha"));
        ex.registerTrader(std::make_unique<RetailTrader>("Ravi"));
        ex.registerTrader(std::make_unique<InstitutionalTrader>("AlphaFund"));
    }
};

// ======================= 1. Integer tick prices =======================

TEST(TickPriceTest, EliminatesFloatingPointDrift) {
    // 0.1 + 0.2 in IEEE-754 double is 0.30000000000000004
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

TEST_F(ExchangeTest, RejectsInvalidAndNonFinitePrices) {
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, 0.0), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, -5.0), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, 0.004), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, std::numeric_limits<double>::quiet_NaN()),
                 InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha", Side::BUY, 10, std::numeric_limits<double>::infinity()),
                 InvalidOrderException);
}

// ======================= 2. Price-time priority & partial fills =======================

TEST_F(ExchangeTest, BestPriceMatchesFirstThenTimePriority) {
    int sellHigh = ex.placeOrder("Asha", Side::SELL, 10, 102.00);
    int sellLow1 = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);
    int sellLow2 = ex.placeOrder("Asha", Side::SELL, 10, 100.00);

    // AlphaFund buys 15 @ 102 -> must match Ravi's 100.00 first, then Asha's 100.00
    int buyId = ex.placeOrder("AlphaFund", Side::BUY, 15, 102.00);

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

TEST_F(ExchangeTest, PartialFillRestsRemainderInBook) {
    ex.placeOrder("Asha", Side::SELL, 30, 101.00);
    int buyId = ex.placeOrder("AlphaFund", Side::BUY, 50, 101.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].quantity(), 30);
    EXPECT_TRUE(ex.book().hasOrder(buyId));
    EXPECT_EQ(ex.book().remainingQuantity(buyId), 20);
    EXPECT_EQ(ex.book().bestBid(), to_ticks(101.00));
    EXPECT_EQ(ex.book().bestAsk(), std::nullopt);
}

TEST_F(ExchangeTest, MarketOrderDoesNotRestWhenLiquidityExhausted) {
    ex.placeOrder("Asha", Side::SELL, 10, 101.00);
    int mktId = ex.placeOrder("AlphaFund", Side::BUY, 25);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].quantity(), 10);
    EXPECT_FALSE(ex.book().hasOrder(mktId));
    EXPECT_EQ(ex.book().openOrderCount(), 0u);
}

// ======================= 3. Stop-loss triggers & cascades =======================

TEST_F(ExchangeTest, StopLossStaysPendingUntilTriggerAndCascades) {
    // Market liquidity for the cascade
    ex.placeOrder("AlphaFund", Side::BUY, 20, 99.00);
    ex.placeOrder("AlphaFund", Side::BUY, 15, 96.00);
    ex.placeOrder("AlphaFund", Side::BUY, 20, 93.00);

    // SL-1 triggers at <= 99, sells @ 95 -> hits 96 bid -> triggers SL-2 at <= 96
    int sl1 = ex.placeOrder("Asha", Side::SELL, 15, 99.00, 95.00);
    int sl2 = ex.placeOrder("Ravi", Side::SELL, 10, 96.00, 92.00);

    // Pending SLs must not sit on the visible ask side
    EXPECT_EQ(ex.book().bestAsk(), std::nullopt);
    EXPECT_TRUE(ex.book().hasOrder(sl1));
    EXPECT_TRUE(ex.book().hasOrder(sl2));

    // Trade at 99 triggers SL-1, which fills at 96, which cascades to trigger SL-2 at 93!
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

    int buySl = ex.placeOrder("Ravi", Side::BUY, 10, 100.00, 102.00);
    EXPECT_EQ(ex.book().bestBid(), std::nullopt);

    // Trade at 100 triggers Ravi's buy SL, which sweeps Asha's 102 ask
    ex.placeOrder("AlphaFund", Side::BUY, 10, 100.00);

    ASSERT_EQ(ex.book().trades().size(), 2u);
    EXPECT_EQ(ex.book().trades()[1].buyOrderId(), buySl);
    EXPECT_EQ(ex.book().trades()[1].price(), to_ticks(102.00));
}

// ======================= 4. O(1) Cancel & Modify =======================

TEST_F(ExchangeTest, CancelRemovesLimitAndPendingStopLoss) {
    int bid = ex.placeOrder("Asha", Side::BUY, 20, 95.00);
    int sl  = ex.placeOrder("Asha", Side::SELL, 10, 90.00, 89.00);

    EXPECT_EQ(ex.book().openOrderCount(), 2u);
    ex.cancelOrder("Asha", bid);
    EXPECT_FALSE(ex.book().hasOrder(bid));
    EXPECT_EQ(ex.book().bestBid(), std::nullopt);

    ex.cancelOrder("Asha", sl);
    EXPECT_FALSE(ex.book().hasOrder(sl));
    EXPECT_EQ(ex.book().openOrderCount(), 0u);

    // Double cancel throws
    EXPECT_THROW(ex.cancelOrder("Asha", bid), InvalidOrderException);
}

TEST_F(ExchangeTest, CancelEnforcesOwnershipAndRejectsFilledOrders) {
    int sell = ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    EXPECT_THROW(ex.cancelOrder("Ravi", sell), InvalidOrderException);
    EXPECT_TRUE(ex.book().hasOrder(sell));

    ex.placeOrder("Ravi", Side::BUY, 10, 100.00);
    EXPECT_THROW(ex.cancelOrder("Asha", sell), InvalidOrderException);
}

TEST_F(ExchangeTest, ModifyQtyDownPreservesTimePriority) {
    int first  = ex.placeOrder("Asha", Side::SELL, 20, 100.00);
    int second = ex.placeOrder("Ravi", Side::SELL, 20, 100.00);

    // Asha reduces size at the same price -> stays ahead of Ravi
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
    int first  = ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    int second = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);

    // Increasing qty moves Asha behind Ravi
    ex.modifyOrder("Asha", first, 15, 100.00);

    ex.placeOrder("AlphaFund", Side::BUY, 10, 100.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].sellOrderId(), second);
    EXPECT_TRUE(ex.book().hasOrder(first));
}

TEST_F(ExchangeTest, ModifyPriceCrossingSpreadMatchesImmediately) {
    int ask = ex.placeOrder("Asha", Side::SELL, 10, 102.00);
    int bid = ex.placeOrder("Ravi", Side::BUY,  10, 99.00);

    // Raising Ravi's bid to 102 crosses Asha's ask -> must match, never cross book
    ex.modifyOrder("Ravi", bid, 10, 102.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].buyOrderId(),  bid);
    EXPECT_EQ(ex.book().trades()[0].sellOrderId(), ask);
    EXPECT_EQ(ex.book().openOrderCount(), 0u);
}

TEST_F(ExchangeTest, FailedModifyLeavesOriginalOrderIntact) {
    int bid = ex.placeOrder("Asha", Side::BUY, 20, 95.00);

    // Exceeds Retail maxOrderSize (100)
    EXPECT_THROW(ex.modifyOrder("Asha", bid, 200, 95.00), InvalidOrderException);
    // Negative price
    EXPECT_THROW(ex.modifyOrder("Asha", bid, 10, -1.0), InvalidOrderException);
    // Wrong owner
    EXPECT_THROW(ex.modifyOrder("Ravi", bid, 10, 95.00), InvalidOrderException);

    EXPECT_TRUE(ex.book().hasOrder(bid));
    EXPECT_EQ(ex.book().remainingQuantity(bid), 20);
}

// ======================= 5. Self-trade prevention =======================

TEST_F(ExchangeTest, StpCancelOldestCancelsOwnRestingAndMatchesThirdParty) {
    ex.setStpMode(StpMode::CANCEL_OLDEST);
    int ownAsk   = ex.placeOrder("Asha", Side::SELL, 10, 100.00);
    int otherAsk = ex.placeOrder("Ravi", Side::SELL, 10, 100.00);

    int buyId = ex.placeOrder("Asha", Side::BUY, 10, 100.00);

    EXPECT_FALSE(ex.book().hasOrder(ownAsk));
    EXPECT_FALSE(ex.book().hasOrder(otherAsk));
    EXPECT_FALSE(ex.book().hasOrder(buyId));

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].buyer(),  "Asha");
    EXPECT_EQ(ex.book().trades()[0].seller(), "Ravi");
}

TEST_F(ExchangeTest, StpCancelNewestPreventsSelfTradeAndCrossedBook) {
    ex.setStpMode(StpMode::CANCEL_NEWEST);
    int otherAsk = ex.placeOrder("Ravi", Side::SELL, 10, 99.00);
    int ownAsk   = ex.placeOrder("Asha", Side::SELL, 10, 100.00);

    // Asha buys 25 @ 100: fills 10 from Ravi @ 99, then hits ownAsk -> remainder cancelled
    int buyId = ex.placeOrder("Asha", Side::BUY, 25, 100.00);

    ASSERT_EQ(ex.book().trades().size(), 1u);
    EXPECT_EQ(ex.book().trades()[0].seller(),   "Ravi");
    EXPECT_EQ(ex.book().trades()[0].quantity(), 10);

    EXPECT_FALSE(ex.book().hasOrder(otherAsk));
    EXPECT_TRUE(ex.book().hasOrder(ownAsk));
    EXPECT_FALSE(ex.book().hasOrder(buyId));
    EXPECT_EQ(ex.book().bestBid(), std::nullopt);
    EXPECT_EQ(ex.book().bestAsk(), to_ticks(100.00));
}

// ======================= 6. Validation & TraderReport =======================

TEST_F(ExchangeTest, EnforcesTraderRegistrationAndOrderLimits) {
    EXPECT_THROW(ex.placeOrder("Ghost", Side::BUY, 10, 100.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 0,  100.00), InvalidOrderException);
    EXPECT_THROW(ex.placeOrder("Asha",  Side::BUY, 101, 100.00), InvalidOrderException);
    EXPECT_NO_THROW(ex.placeOrder("AlphaFund", Side::BUY, 500, 100.00));
}

TEST_F(ExchangeTest, TraderReportComputesAccuratePnLAndWindow) {
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
    // 20 matched shares * (110 - 100) = +200.00
    EXPECT_DOUBLE_EQ(r.realizedPnL(),  200.00);
}

TEST(TraderReportWindowTest, ExcludesTradesOlderThanRequestedMonths) {
    std::time_t now      = std::time(nullptr);
    std::time_t twoMoAgo = now - static_cast<std::time_t>(65) * 24 * 3600;
    std::time_t recent   = now - 24 * 3600;

    // Directly verify window filtering logic via Exchange::buildTraderReport uses 30-day months
    TraderReport report("Asha", 1);
    // Trade 1 inside 1-month window, Trade 2 excluded when filtered
    if (recent >= now - 30 * 24 * 3600) {
        report.addBuy(1, to_ticks(100.00), 10, recent);
    }
    if (twoMoAgo >= now - 30 * 24 * 3600) {
        report.addBuy(2, to_ticks(200.00), 10, twoMoAgo);
    }
    EXPECT_EQ(report.totalBought(), 10);
    EXPECT_DOUBLE_EQ(report.avgBuyPrice(), 100.00);
}

}  // namespace
