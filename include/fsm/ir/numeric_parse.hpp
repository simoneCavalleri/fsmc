/**
 * @file numeric_parse.hpp
 * @brief Zero-exception, locale-independent numeric parsing helpers backed by std::from_chars.
 */

#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace fsm::ir {

/**
 * @brief Trims leading/trailing ASCII whitespace from a string_view.
 */
[[nodiscard]] inline std::string_view trim_numeric_sv(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

/**
 * @brief Parses a complete signed 64-bit integer from a string_view using std::from_chars (no exceptions, no locale).
 */
[[nodiscard]] inline std::optional<int64_t> parse_int64(std::string_view sv, int base = 10) noexcept {
    sv = trim_numeric_sv(sv);
    if (sv.empty()) {
        return std::nullopt;
    }
    if (sv.front() == '+') {
        sv.remove_prefix(1);
        if (sv.empty()) {
            return std::nullopt;
        }
    }
    int64_t val = 0;
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(begin, end, val, base);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return val;
}

/**
 * @brief Parses a complete finite double from a string_view without exceptions or locale dependence.
 */
[[nodiscard]] inline std::optional<double> parse_double(std::string_view sv) noexcept {
    sv = trim_numeric_sv(sv);
    if (sv.empty()) {
        return std::nullopt;
    }
    if (sv.front() == '+') {
        sv.remove_prefix(1);
        if (sv.empty()) {
            return std::nullopt;
        }
    }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    double val = 0.0;
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(begin, end, val);
    if (ec != std::errc{} || ptr != end || !std::isfinite(val)) {
        return std::nullopt;
    }
    return val;
#else
    std::string tmp(sv);
    char* end_ptr = nullptr;
    double val = std::strtod(tmp.c_str(), &end_ptr);
    if (end_ptr != tmp.c_str() + tmp.size() || !std::isfinite(val)) {
        return std::nullopt;
    }
    return val;
#endif
}

}  // namespace fsm::ir
