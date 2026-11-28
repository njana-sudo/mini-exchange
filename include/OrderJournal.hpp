#pragma once
// OrderJournal.hpp - append-only binary command log for deterministic replay.

#include "Types.hpp"

#include <cstdint>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

enum class LogOp : uint8_t {
    REGISTER_TRADER = 1,
    SET_STP_MODE    = 2,
    PLACE_LIMIT     = 3,
    PLACE_MARKET    = 4,
    PLACE_STOP      = 5,
    CANCEL          = 6,
    MODIFY          = 7
};

enum class TraderKind : uint8_t {
    RETAIL        = 1,
    INSTITUTIONAL = 2
};

struct JournalRecord {
    LogOp       op           = LogOp::PLACE_LIMIT;
    std::time_t timestamp    = 0;
    std::string trader;
    Side        side         = Side::BUY;
    Quantity    quantity     = 0;
    Price       priceTicks   = 0;
    Price       triggerTicks = 0;
    OrderId     orderId      = 0;
    TraderKind  traderKind   = TraderKind::RETAIL;
    StpMode     stpMode      = StpMode::CANCEL_OLDEST;
};

class OrderJournal {
private:
    std::ofstream out_;
    bool          flushEachWrite_ = true;

    void writeString(const std::string& s);
    void writeHeader();

public:
    OrderJournal() = default;

    void open(const std::string& path, bool truncate = true, bool flushEachWrite = true);
    void close();
    bool isOpen() const { return out_.is_open(); }

    void appendRegisterTrader(const std::string& name, TraderKind kind);
    void appendSetStpMode(StpMode mode);
    void appendPlaceLimit(std::time_t ts, const std::string& trader, Side side, Quantity qty, Price priceTicks);
    void appendPlaceMarket(std::time_t ts, const std::string& trader, Side side, Quantity qty);
    void appendPlaceStop(std::time_t ts, const std::string& trader, Side side, Quantity qty,
                         Price triggerTicks, Price limitTicks);
    void appendCancel(std::time_t ts, const std::string& trader, OrderId orderId);
    void appendModify(std::time_t ts, const std::string& trader, OrderId orderId,
                      Quantity newQty, Price newPriceTicks);

    // Reads all records from a journal file; throws std::runtime_error on bad magic or corruption.
    static std::vector<JournalRecord> readAll(const std::string& path);
};
