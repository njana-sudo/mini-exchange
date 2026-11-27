#pragma once
// Types.hpp - shared enums, helpers, and exceptions used across the project

#include <cmath>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <string>

// ======================= Side =======================

enum class Side { BUY, SELL };

inline std::string toString(Side s) { return s == Side::BUY ? "BUY" : "SELL"; }

// ======================= Integer price (tick) system =======================
//
// Real exchanges never use floating-point prices.  Floating-point comparison
// as map keys causes silent correctness bugs (two prices that look equal may
// differ by an ULP and land in separate levels).
//
// We represent every price as an integer number of *ticks*.
//   1 tick  = ₹0.01  (1 paisa — the standard NSE equity tick)
//   ₹101.50 = 10150 ticks
//
// All internal arithmetic is done in ticks (int64_t).
// Conversion to/from human-readable rupees happens only at the boundary
// (user input in Exchange::placeOrder, display in operator<<).

using Price = int64_t;                      // ticks; always positive for a valid price

constexpr int64_t TICKS_PER_RUPEE = 100;   // 1 tick = ₹0.01

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

// Validate user-supplied rupee price and convert to positive integer ticks.
inline Price validateAndConvertPrice(double rupees, const char* label = "Price") {
    if (!std::isfinite(rupees))
        throw InvalidOrderException(std::string(label) + " must be finite");
    Price ticks = to_ticks(rupees);
    if (ticks <= 0)
        throw InvalidOrderException(std::string(label) + " must be positive");
    return ticks;
}

// ======================= Helpers =======================

// format a time_t as "DD-Mon-YYYY"
inline std::string formatDate(std::time_t t) {
    char buf[20];
    std::strftime(buf, sizeof(buf), "%d-%b-%Y", std::localtime(&t));
    return buf;
}
