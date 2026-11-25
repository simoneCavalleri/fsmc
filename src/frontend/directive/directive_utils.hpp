#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace fsm::frontend::directive::detail {

inline std::string trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n'#%/");
    if (start == std::string::npos)
        return "";
    auto end = s.find_last_not_of(" \t\r\n;");
    return std::string(s.substr(start, end - start + 1));
}

inline std::string trim_ws(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

inline std::string extract_quoted_or_word(const std::string& str, std::size_t start) {
    if (start >= str.size())
        return "";
    while (start < str.size() && (str[start] == ' ' || str[start] == '\t')) {
        ++start;
    }
    if (start >= str.size())
        return "";
    if (str[start] == '"') {
        auto end = str.find('"', start + 1);
        if (end != std::string::npos) {
            return str.substr(start + 1, end - start - 1);
        }
        return str.substr(start + 1);
    }
    auto end = str.find_first_of(" \t\r\n];,", start);
    if (end != std::string::npos) {
        return str.substr(start, end - start);
    }
    return str.substr(start);
}

inline std::vector<std::string> extract_array_items(const std::string& str, std::size_t start) {
    std::vector<std::string> items;
    auto open_bracket = str.find('[', start);
    auto close_bracket = str.find(']', open_bracket);
    if (open_bracket == std::string::npos || close_bracket == std::string::npos) {
        return items;
    }
    std::string inner = str.substr(open_bracket + 1, close_bracket - open_bracket - 1);
    std::istringstream iss(inner);
    std::string token;
    while (std::getline(iss, token, ',')) {
        std::string t = trim(token);
        if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
            t = t.substr(1, t.size() - 2);
        }
        if (!t.empty()) {
            items.push_back(std::move(t));
        }
    }
    return items;
}

}  // namespace fsm::frontend::directive::detail
