#pragma once
// TraderLedger.hpp - O(1) continuous per-trader position, cost-basis, and realized P&L tracker.
// All accounting uses exact integer tick-shares (int64_t), avoiding cross-window P&L drift.

#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class TraderLedger {
public:
    struct FillRecord {
        size_t      tradeIndex;
        Side        side;
        int64_t     realizedDeltaTickShares;  // exact P&L realized by this fill
    };

private:
    Quantity                openQty_            = 0;  // >0 long, <0 short
    int64_t                 openCostTickShares_ = 0;  // unsigned magnitude of open cost/proceeds basis
    int64_t                 realizedTickShares_ = 0;  // cumulative realized P&L (ticks * shares)
    std::vector<FillRecord> fills_;

public:
    // Updates open position, cost basis, and cumulative realized P&L in O(1).
    // Returns the realized P&L delta (in tick-shares) for this fill.
    int64_t recordFill(size_t tradeIndex, Side side, Price fillPrice, Quantity qty);

    Quantity                       netPosition()        const { return openQty_; }
    int64_t                        openCostTickShares() const { return openCostTickShares_; }
    int64_t                        realizedTickShares() const { return realizedTickShares_; }
    double                         realizedPnL()        const;
    double                         avgEntryPrice()      const;
    const std::vector<FillRecord>& fills()              const { return fills_; }
};
