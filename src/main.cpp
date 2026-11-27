// main.cpp - demo / entry point for the Mini Stock Exchange

#include "Exchange.hpp"
#include "Trader.hpp"
#include "Types.hpp"

#include <iostream>
#include <memory>
#include <vector>

// [POLYMORPHISM] template helper: prints any vector of items that support operator<<
template <typename T>
void printAll(const std::string& title, const std::vector<T>& items) {
    std::cout << title << " (" << items.size() << ")\n";
    for (const auto& item : items) std::cout << "  " << item << "\n";
}

int main() {
    Exchange exchange;
    exchange.registerTrader(std::make_unique<RetailTrader>("Asha"));
    exchange.registerTrader(std::make_unique<RetailTrader>("Ravi"));
    exchange.registerTrader(std::make_unique<InstitutionalTrader>("AlphaFund"));
    exchange.listTraders();
    std::cout << "\n";

    // Build up resting limit orders
    int firstSellId = exchange.placeOrder("Asha",      Side::SELL, 50, 101.00);
    exchange.placeOrder("Ravi",      Side::SELL, 30, 102.00);
    exchange.placeOrder("AlphaFund", Side::BUY,  40,  99.00);

    // Asha protects her position with a stop-loss:
    //   "If price drops to 98, sell 20 shares at limit 97.50"
    exchange.placeOrder("Asha", Side::SELL, 20, 98.00, 97.50);
    exchange.book().print();

    // A buy that crosses the spread -> trades at 101 and 102 (SL still pending)
    std::cout << "\n";
    exchange.placeOrder("AlphaFund", Side::BUY, 70, 102.00);
    exchange.book().print();

    // AlphaFund adds another bid at 98
    exchange.placeOrder("AlphaFund", Side::BUY, 30, 98.00);

    // Ravi sells aggressively -> price drops to 98 -> Asha's SL triggers!
    std::cout << "\n--- Ravi sells aggressively, price drops ---\n";
    exchange.placeOrder("Ravi", Side::SELL, 60, 97.00);
    exchange.book().print();

    // Market order sweeps the remaining SL liquidity
    std::cout << "\n";
    exchange.placeOrder("AlphaFund", Side::BUY, 10);
    exchange.book().print();

    // AlphaFund takes profit by selling 30 to Ravi at 103
    std::cout << "\n";
    exchange.placeOrder("Ravi",      Side::BUY,  30, 103.00);
    exchange.placeOrder("AlphaFund", Side::SELL, 30, 103.00);

    // Cancel, Modify & Self-Trade Prevention demo
    std::cout << "\n--- Cancel, Modify & STP demo ---\n";
    int ashaBid = exchange.placeOrder("Asha", Side::BUY, 25, 95.00);
    int raviBid = exchange.placeOrder("Ravi", Side::BUY, 15, 95.00);
    int ashaSl  = exchange.placeOrder("Asha", Side::SELL, 10, 90.00, 89.50);
    exchange.modifyOrder("Asha", ashaBid, 10, 95.00);   // size cut: keeps priority
    exchange.cancelOrder("Ravi", raviBid);               // O(1) cancel resting limit
    exchange.cancelOrder("Asha", ashaSl);                // O(1) cancel pending SL

    // Demonstrate CANCEL_NEWEST STP mode
    exchange.setStpMode(StpMode::CANCEL_NEWEST);
    int raviAsk = exchange.placeOrder("Ravi", Side::SELL, 10, 105.00);
    exchange.placeOrder("Ravi", Side::BUY, 10, 105.00);  // incoming cancelled by STP
    exchange.cancelOrder("Ravi", raviAsk);
    exchange.setStpMode(StpMode::CANCEL_OLDEST);
    exchange.book().print();

    // Error handling: retail size limit, negative price, and invalid cancel/modify
    std::cout << "\n";
    try {
        exchange.placeOrder("Asha", Side::BUY, 500, 100.00);
    } catch (const InvalidOrderException& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }
    try {
        exchange.placeOrder("Ravi", Side::SELL, 10, -5.0);
    } catch (const InvalidOrderException& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }
    try {
        exchange.cancelOrder("Ravi", ashaBid);           // wrong owner!
    } catch (const InvalidOrderException& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }
    try {
        exchange.cancelOrder("Asha", firstSellId);       // already filled!
    } catch (const InvalidOrderException& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }

    // Clean up Asha's remaining resting bid before end of demo
    exchange.cancelOrder("Asha", ashaBid);

    std::cout << "\n";
    printAll("Trades executed", exchange.book().trades());

    // Trader reports: buy/sell history and realized P&L for the last 1 month
    exchange.traderReport("AlphaFund", 1);
    exchange.traderReport("Asha",      1);
    exchange.traderReport("Ravi",      1);

    return 0;
}
