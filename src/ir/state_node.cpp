#include "fsm/ir/state_node.hpp"

#include <cctype>
#include <iostream>

namespace fsm::ir {

StateTimeInvariant::StateTimeInvariant(std::string_view str) {
    parse_from_string(str);
}

StateTimeInvariant::StateTimeInvariant(const char* str) : StateTimeInvariant(std::string_view(str ? str : "")) {}

StateTimeInvariant::StateTimeInvariant(std::string str) : StateTimeInvariant(std::string_view(str)) {}

StateTimeInvariant::StateTimeInvariant(std::string clk, TimeInvariantOp oper, std::uint64_t dur, TimeUnit u)
    : clock(std::move(clk)), op(oper), duration(dur), unit(u) {
    raw_expression = to_string();
}

std::string StateTimeInvariant::to_string() const {
    if (!raw_expression.empty())
        return raw_expression;
    std::string res = clock;
    res += (op == TimeInvariantOp::LessEqual ? " <= " : " < ");
    res += std::to_string(duration);
    switch (unit) {
        case TimeUnit::Milliseconds:
            res += "ms";
            break;
        case TimeUnit::Seconds:
            res += "s";
            break;
        case TimeUnit::Microseconds:
            res += "us";
            break;
        case TimeUnit::Nanoseconds:
            res += "ns";
            break;
        case TimeUnit::Minutes:
            res += "min";
            break;
    }
    return res;
}

bool StateTimeInvariant::operator==(const StateTimeInvariant& other) const noexcept {
    if (!raw_expression.empty() && !other.raw_expression.empty()) {
        return raw_expression == other.raw_expression;
    }
    return clock == other.clock && op == other.op && duration == other.duration && unit == other.unit;
}

bool StateTimeInvariant::operator==(const char* str) const noexcept {
    return str != nullptr && (to_string() == str || raw_expression == str);
}

bool StateTimeInvariant::operator==(const std::string& str) const noexcept {
    return to_string() == str || raw_expression == str;
}

bool StateTimeInvariant::operator==(std::string_view str) const noexcept {
    return to_string() == str || raw_expression == str;
}

void StateTimeInvariant::parse_from_string(std::string_view str) {
    raw_expression = std::string(str);
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.front())) != 0)
        str.remove_prefix(1);
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())) != 0)
        str.remove_suffix(1);
    if (str.empty())
        return;

    if (str.find("<=") != std::string_view::npos) {
        op = TimeInvariantOp::LessEqual;
        size_t pos = str.find("<=");
        clock = std::string(str.substr(0, pos));
        while (!clock.empty() && std::isspace(static_cast<unsigned char>(clock.back())) != 0)
            clock.pop_back();
        while (!clock.empty() && std::isspace(static_cast<unsigned char>(clock.front())) != 0)
            clock.erase(0, 1);
        std::string_view dur_str = str.substr(pos + 2);
        parse_duration_and_unit(dur_str);
    } else if (str.find('<') != std::string_view::npos) {
        op = TimeInvariantOp::LessThan;
        size_t pos = str.find('<');
        clock = std::string(str.substr(0, pos));
        while (!clock.empty() && std::isspace(static_cast<unsigned char>(clock.back())) != 0)
            clock.pop_back();
        while (!clock.empty() && std::isspace(static_cast<unsigned char>(clock.front())) != 0)
            clock.erase(0, 1);
        std::string_view dur_str = str.substr(pos + 1);
        parse_duration_and_unit(dur_str);
    } else {
        clock = "stay_duration";
        op = TimeInvariantOp::LessEqual;
        parse_duration_and_unit(str);
    }
}

void StateTimeInvariant::parse_duration_and_unit(std::string_view dur_str) {
    while (!dur_str.empty() && std::isspace(static_cast<unsigned char>(dur_str.front())) != 0)
        dur_str.remove_prefix(1);
    while (!dur_str.empty() && std::isspace(static_cast<unsigned char>(dur_str.back())) != 0)
        dur_str.remove_suffix(1);
    if (!dur_str.empty() && dur_str.back() == ';')
        dur_str.remove_suffix(1);

    if (dur_str.ends_with("[ms]")) {
        unit = TimeUnit::Milliseconds;
        dur_str.remove_suffix(4);
    } else if (dur_str.ends_with("[s]")) {
        unit = TimeUnit::Seconds;
        dur_str.remove_suffix(3);
    } else if (dur_str.ends_with("[us]")) {
        unit = TimeUnit::Microseconds;
        dur_str.remove_suffix(4);
    } else if (dur_str.ends_with("[ns]")) {
        unit = TimeUnit::Nanoseconds;
        dur_str.remove_suffix(4);
    } else if (dur_str.ends_with("ms")) {
        unit = TimeUnit::Milliseconds;
        dur_str.remove_suffix(2);
    } else if (dur_str.ends_with("us")) {
        unit = TimeUnit::Microseconds;
        dur_str.remove_suffix(2);
    } else if (dur_str.ends_with("ns")) {
        unit = TimeUnit::Nanoseconds;
        dur_str.remove_suffix(2);
    } else if (dur_str.ends_with('s')) {
        unit = TimeUnit::Seconds;
        dur_str.remove_suffix(1);
    }

    while (!dur_str.empty() && std::isspace(static_cast<unsigned char>(dur_str.back())) != 0)
        dur_str.remove_suffix(1);
    try {
        if (!dur_str.empty()) {
            duration = std::stoull(std::string(dur_str));
        }
    } catch (...) {
    }
}

std::ostream& operator<<(std::ostream& os, const StateTimeInvariant& inv) {
    return os << inv.to_string();
}

std::string operator+(const std::string& lhs, const StateTimeInvariant& rhs) {
    return lhs + rhs.to_string();
}

std::string operator+(const char* lhs, const StateTimeInvariant& rhs) {
    return std::string(lhs) + rhs.to_string();
}

std::string operator+(const StateTimeInvariant& lhs, const std::string& rhs) {
    return lhs.to_string() + rhs;
}

std::string operator+(const StateTimeInvariant& lhs, const char* rhs) {
    return lhs.to_string() + rhs;
}

}  // namespace fsm::ir
