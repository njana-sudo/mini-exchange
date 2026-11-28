// OrderJournal.cpp - binary append-only order journal reader/writer

#include "OrderJournal.hpp"

#include <cstring>
#include <stdexcept>

namespace {
constexpr char     JOURNAL_MAGIC[8] = {'M', 'E', 'X', 'L', 'O', 'G', '0', '1'};
constexpr uint32_t MAX_NAME_LEN     = 1024;

template <typename T>
void writePod(std::ofstream& out, const T& val) {
    out.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

template <typename T>
bool readPod(std::ifstream& in, T& val) {
    in.read(reinterpret_cast<char*>(&val), sizeof(T));
    return static_cast<bool>(in);
}

std::string readString(std::ifstream& in) {
    uint32_t len = 0;
    if (!readPod(in, len)) throw std::runtime_error("Corrupt journal: truncated string length");
    if (len > MAX_NAME_LEN) throw std::runtime_error("Corrupt journal: string exceeds limit");
    std::string s(len, '\0');
    if (len > 0) {
        in.read(&s[0], static_cast<std::streamsize>(len));
        if (!in) throw std::runtime_error("Corrupt journal: truncated string payload");
    }
    return s;
}
}  // namespace

void OrderJournal::writeString(const std::string& s) {
    if (s.size() > MAX_NAME_LEN) throw std::runtime_error("Journal string exceeds max length");
    uint32_t len = static_cast<uint32_t>(s.size());
    writePod(out_, len);
    if (len > 0) out_.write(s.data(), static_cast<std::streamsize>(len));
}

void OrderJournal::writeHeader() {
    out_.write(JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC));
}

void OrderJournal::open(const std::string& path, bool truncate, bool flushEachWrite) {
    close();
    flushEachWrite_ = flushEachWrite;

    if (!truncate) {
        std::ifstream existing(path, std::ios::binary | std::ios::ate);
        bool hasContent = existing.is_open() && existing.tellg() > 0;
        existing.close();

        out_.open(path, std::ios::binary | std::ios::app);
        if (!out_) throw std::runtime_error("Failed to open journal for append: " + path);
        if (!hasContent) writeHeader();
    } else {
        out_.open(path, std::ios::binary | std::ios::trunc);
        if (!out_) throw std::runtime_error("Failed to open journal: " + path);
        writeHeader();
    }
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::close() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

void OrderJournal::appendRegisterTrader(const std::string& name, TraderKind kind) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::REGISTER_TRADER));
    writePod(out_, static_cast<uint8_t>(kind));
    writeString(name);
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::appendSetStpMode(StpMode mode) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::SET_STP_MODE));
    writePod(out_, static_cast<uint8_t>(mode));
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::appendPlaceLimit(std::time_t ts, const std::string& trader, Side side,
                                    Quantity qty, Price priceTicks) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::PLACE_LIMIT));
    int64_t ts64 = static_cast<int64_t>(ts);
    writePod(out_, ts64);
    writeString(trader);
    writePod(out_, static_cast<uint8_t>(side));
    writePod(out_, qty);
    writePod(out_, priceTicks);
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::appendPlaceMarket(std::time_t ts, const std::string& trader, Side side, Quantity qty) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::PLACE_MARKET));
    int64_t ts64 = static_cast<int64_t>(ts);
    writePod(out_, ts64);
    writeString(trader);
    writePod(out_, static_cast<uint8_t>(side));
    writePod(out_, qty);
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::appendPlaceStop(std::time_t ts, const std::string& trader, Side side, Quantity qty,
                                   Price triggerTicks, Price limitTicks) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::PLACE_STOP));
    int64_t ts64 = static_cast<int64_t>(ts);
    writePod(out_, ts64);
    writeString(trader);
    writePod(out_, static_cast<uint8_t>(side));
    writePod(out_, qty);
    writePod(out_, triggerTicks);
    writePod(out_, limitTicks);
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::appendCancel(std::time_t ts, const std::string& trader, OrderId orderId) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::CANCEL));
    int64_t ts64 = static_cast<int64_t>(ts);
    writePod(out_, ts64);
    writeString(trader);
    writePod(out_, orderId);
    if (flushEachWrite_) out_.flush();
}

void OrderJournal::appendModify(std::time_t ts, const std::string& trader, OrderId orderId,
                                  Quantity newQty, Price newPriceTicks) {
    if (!out_.is_open()) return;
    writePod(out_, static_cast<uint8_t>(LogOp::MODIFY));
    int64_t ts64 = static_cast<int64_t>(ts);
    writePod(out_, ts64);
    writeString(trader);
    writePod(out_, orderId);
    writePod(out_, newQty);
    writePod(out_, newPriceTicks);
    if (flushEachWrite_) out_.flush();
}

std::vector<JournalRecord> OrderJournal::readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to open journal for replay: " + path);

    char magic[8]{};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0) {
        throw std::runtime_error("Invalid or corrupt journal header");
    }

    std::vector<JournalRecord> records;
    while (true) {
        uint8_t opRaw = 0;
        if (!readPod(in, opRaw)) break;  // clean EOF

        JournalRecord rec;
        rec.op = static_cast<LogOp>(opRaw);

        switch (rec.op) {
            case LogOp::REGISTER_TRADER: {
                uint8_t kindRaw = 0;
                if (!readPod(in, kindRaw)) throw std::runtime_error("Corrupt journal: truncated REGISTER_TRADER");
                rec.traderKind = static_cast<TraderKind>(kindRaw);
                rec.trader     = readString(in);
                break;
            }
            case LogOp::SET_STP_MODE: {
                uint8_t modeRaw = 0;
                if (!readPod(in, modeRaw)) throw std::runtime_error("Corrupt journal: truncated SET_STP_MODE");
                rec.stpMode = static_cast<StpMode>(modeRaw);
                break;
            }
            case LogOp::PLACE_LIMIT: {
                int64_t ts64    = 0;
                uint8_t sideRaw = 0;
                if (!readPod(in, ts64)) throw std::runtime_error("Corrupt journal: truncated timestamp");
                rec.timestamp = static_cast<std::time_t>(ts64);
                rec.trader    = readString(in);
                if (!readPod(in, sideRaw) || !readPod(in, rec.quantity) || !readPod(in, rec.priceTicks))
                    throw std::runtime_error("Corrupt journal: truncated PLACE_LIMIT");
                rec.side = static_cast<Side>(sideRaw);
                break;
            }
            case LogOp::PLACE_MARKET: {
                int64_t ts64    = 0;
                uint8_t sideRaw = 0;
                if (!readPod(in, ts64)) throw std::runtime_error("Corrupt journal: truncated timestamp");
                rec.timestamp = static_cast<std::time_t>(ts64);
                rec.trader    = readString(in);
                if (!readPod(in, sideRaw) || !readPod(in, rec.quantity))
                    throw std::runtime_error("Corrupt journal: truncated PLACE_MARKET");
                rec.side = static_cast<Side>(sideRaw);
                break;
            }
            case LogOp::PLACE_STOP: {
                int64_t ts64    = 0;
                uint8_t sideRaw = 0;
                if (!readPod(in, ts64)) throw std::runtime_error("Corrupt journal: truncated timestamp");
                rec.timestamp = static_cast<std::time_t>(ts64);
                rec.trader    = readString(in);
                if (!readPod(in, sideRaw) || !readPod(in, rec.quantity) ||
                    !readPod(in, rec.triggerTicks) || !readPod(in, rec.priceTicks))
                    throw std::runtime_error("Corrupt journal: truncated PLACE_STOP");
                rec.side = static_cast<Side>(sideRaw);
                break;
            }
            case LogOp::CANCEL: {
                int64_t ts64 = 0;
                if (!readPod(in, ts64)) throw std::runtime_error("Corrupt journal: truncated timestamp");
                rec.timestamp = static_cast<std::time_t>(ts64);
                rec.trader    = readString(in);
                if (!readPod(in, rec.orderId))
                    throw std::runtime_error("Corrupt journal: truncated CANCEL");
                break;
            }
            case LogOp::MODIFY: {
                int64_t ts64 = 0;
                if (!readPod(in, ts64)) throw std::runtime_error("Corrupt journal: truncated timestamp");
                rec.timestamp = static_cast<std::time_t>(ts64);
                rec.trader    = readString(in);
                if (!readPod(in, rec.orderId) || !readPod(in, rec.quantity) || !readPod(in, rec.priceTicks))
                    throw std::runtime_error("Corrupt journal: truncated MODIFY");
                break;
            }
            default:
                throw std::runtime_error("Corrupt journal: unknown LogOp");
        }

        records.push_back(std::move(rec));
    }

    return records;
}
