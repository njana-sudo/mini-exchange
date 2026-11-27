#pragma once
// Types.hpp - shared enums, helpers, and exceptions used across the project

#include <ctime>
#include <stdexcept>
#include <string>

// ======================= Side =======================

enum class Side { BUY, SELL };

inline std::string toString(Side s) { return s == Side::BUY ? "BUY" : "SELL"; }

// ======================= Helpers =======================

// format a time_t as "DD-Mon-YYYY"
inline std::string formatDate(std::time_t t) {
    char buf[20];
    std::strftime(buf, sizeof(buf), "%d-%b-%Y", std::localtime(&t));
    return buf;
}

// ======================= Exceptions =======================

// [EXCEPTIONS] [INHERITANCE] custom exception derived from a standard one
class InvalidOrderException : public std::runtime_error {
public:
    explicit InvalidOrderException(const std::string& msg) : std::runtime_error(msg) {}
};
