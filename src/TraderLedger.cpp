// TraderLedger.cpp - O(1) continuous average-cost position & P&L accounting

#include "TraderLedger.hpp"

#include <algorithm>
#include <cstdlib>

namespace {
// Exact floor division (a * b) / c without int64_t intermediate overflow.
int64_t proportionalShare(int64_t total, Quantity part, Quantity whole) {
    return (total / whole) * part + ((total % whole) * part) / whole;
}
}  // namespace

int64_t TraderLedger::recordFill(size_t tradeIndex, Side side, Price fillPrice, Quantity qty) {
    Quantity signedDelta = (side == Side::BUY) ? qty : -qty;
    int64_t  pnlDelta    = 0;

    if (openQty_ == 0 || (openQty_ > 0 && signedDelta > 0) || (openQty_ < 0 && signedDelta < 0)) {
        // Opening or adding to an existing position in the same direction
        openQty_            += signedDelta;
        openCostTickShares_ += static_cast<int64_t>(fillPrice) * qty;
    } else {
        // Closing (partially, fully, or flipping) an existing opposite position
        Quantity absOpen  = (openQty_ > 0) ? openQty_ : -openQty_;
        Quantity closeQty = std::min(absOpen, qty);

        int64_t retiredCost = proportionalShare(openCostTickShares_, closeQty, absOpen);
        int64_t fillValue   = static_cast<int64_t>(fillPrice) * closeQty;

        // Long close: revenue - cost; Short close: entry proceeds - cover cost
        pnlDelta = (openQty_ > 0) ? (fillValue - retiredCost)
                                  : (retiredCost - fillValue);

        realizedTickShares_ += pnlDelta;
        openCostTickShares_ -= retiredCost;
        openQty_            += signedDelta;

        // If trade quantity exceeded prior open position, open the flipped remainder
        Quantity remaining = qty - closeQty;
        if (remaining > 0) {
            openCostTickShares_ = static_cast<int64_t>(fillPrice) * remaining;
        }
    }

    fills_.push_back({tradeIndex, side, pnlDelta});
    return pnlDelta;
}

double TraderLedger::realizedPnL() const {
    return static_cast<double>(realizedTickShares_) / static_cast<double>(TICKS_PER_RUPEE);
}

double TraderLedger::avgEntryPrice() const {
    if (openQty_ == 0) return 0.0;
    Quantity absQty = (openQty_ > 0) ? openQty_ : -openQty_;
    return static_cast<double>(openCostTickShares_) /
           (static_cast<double>(absQty) * static_cast<double>(TICKS_PER_RUPEE));
}
