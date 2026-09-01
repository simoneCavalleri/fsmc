#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::codegen {

class LexerUtils {
  public:
    // Extract content within enclosing brackets (e.g. '[guard]' -> 'guard')
    static std::optional<std::string_view> extract_bracketed(std::string_view str, char open_char, char close_char) {
        auto start = str.find(open_char);
        if (start == std::string_view::npos) {
            return std::nullopt;
        }
        size_t depth = 1;
        size_t i = start + 1;
        for (; i < str.size(); ++i) {
            if (str[i] == open_char) {
                ++depth;
            } else if (str[i] == close_char) {
                --depth;
                if (depth == 0) {
                    return str.substr(start + 1, i - start - 1);
                }
            }
        }
        return std::nullopt;
    }

    // Extract double or single quoted string content (e.g. '"name"' -> 'name')
    static std::optional<std::string_view> extract_quoted(std::string_view str) {
        str = trim(str);
        if (str.size() >= 2 &&
            ((str.front() == '"' && str.back() == '"') || (str.front() == '\'' && str.back() == '\''))) {
            return str.substr(1, str.size() - 2);
        }
        return std::nullopt;
    }

    // Split transition label formatted as: "event [guard] / action"
    static std::tuple<std::string, std::optional<std::string>, std::optional<std::string>> parse_transition_label(
        std::string_view label) {
        std::string event_name;
        std::optional<std::string> guard_str;
        std::optional<std::string> action_str;

        std::string rem = std::string(trim(label));

        // 1. Check for guard [...]
        auto guard_opt = extract_bracketed(rem, '[', ']');
        if (guard_opt.has_value()) {
            guard_str = std::string(trim(*guard_opt));
            auto open_idx = rem.find('[');
            auto close_idx = rem.find(']', open_idx);
            std::string before = rem.substr(0, open_idx);
            std::string after =
                (close_idx != std::string::npos && close_idx + 1 < rem.size()) ? rem.substr(close_idx + 1) : "";
            rem = std::string(trim(before + after));
        }

        // 2. Check for action /...
        auto slash_idx = rem.find('/');
        if (slash_idx != std::string::npos) {
            action_str = std::string(trim(rem.substr(slash_idx + 1)));
            event_name = std::string(trim(rem.substr(0, slash_idx)));
        } else {
            event_name = std::string(trim(rem));
        }

        return {event_name, guard_str, action_str};
    }
};

}  // namespace fsm::codegen
