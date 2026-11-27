#pragma once
// Types.hpp - shared enums, fixed-point price helpers, ID/quantity types,
//             clock abstraction, and exceptions.

#include <cmath>
#include <cstdint>
#include <ctime>
#include <functional>
#include <stdexcept>
#include <string>

// ======================= Core Types =======================

using OrderId  = uint64_t;
using Quantity = int64_t;
using Price    = int64_t;   // integer ticks; always positive for a valid price
using Clock    = std::function<std::time_t()>;

inline Clock systemClock() {
    return []() { return std::time(nullptr); };
}

// ======================= Enums =======================

enum class Side { BUY, SELL };

inline std::string toString(Side s) { return s == Side::BUY ? "BUY" : "SELL"; }

// Self-Trade Prevention (STP) policy when an incoming order crosses
// a resting order belonging to the same trader.
enum class StpMode {
    CANCEL_OLDEST,   // cancel the resting order and continue matching (Cancel Passive)
    CANCEL_NEWEST    // cancel the incoming remainder immediately (Cancel Active)
};

// ======================= Integer price (tick) system =======================
//
// Real exchanges never use floating-point prices as book keys.
// 1 tick = ₹0.01 (1 paisa).  ₹101.50 = 10150 ticks.

constexpr int64_t TICKS_PER_RUPEE = 100;
constexpr double  MAX_RUPEES      = 1e9;   // ₹10^9 = 10^11 ticks; safe from int64 overflow

// Convert user-supplied rupee amount to ticks (rounds to nearest tick).
inline Price to_ticks(double rupees) {
    return static_cast<Price>(std::llround(rupees * TICKS_PER_RUPEE));
}

// Convert ticks back to rupees for display only.
inline double to_rupees(Price ticks) {
    return static_cast<double>(ticks) / static_cast<double>(TICKS_PER_RUPEE);
}

// ======================= Exceptions =======================

// [EXCEPTIONS] [INHERITANCE] custom exception derived from a standard one
class InvalidOrderException : public std::runtime_error {
public:
    explicit InvalidOrderException(const std::string& msg) : std::runtime_error(msg) {}
};

// Validate user-supplied rupee price BEFORE llround to prevent signed overflow UB.
inline Price validateAndConvertPrice(double rupees, const char* label = "Price") {
    if (!std::isfinite(rupees))
        throw InvalidOrderException(std::string(label) + " must be finite");
    if (rupees <= 0.0)
        throw InvalidOrderException(std::string(label) + " must be positive");
    if (rupees > MAX_RUPEES)
        throw InvalidOrderException(std::string(label) + " exceeds maximum allowed");
    Price ticks = to_ticks(rupees);
    if (ticks <= 0)
        throw InvalidOrderException(std::string(label) + " must be positive");
    return ticks;
}

// ======================= Helpers =======================

// format a time_t as "DD-Mon-YYYY"
inline std::string formatDate(std::time_t t) {
    char buf[20];
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::strftime(buf, sizeof(buf), "%d-%b-%Y", &tmBuf);
    return buf;
}
