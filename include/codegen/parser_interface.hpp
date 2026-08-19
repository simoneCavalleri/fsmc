#pragma once

#include <string>
#include <string_view>

#include "fsm_model.hpp"

namespace fsm::codegen {

class IParser {
  public:
    virtual ~IParser() = default;
    virtual bool parse(std::string_view content, FsmModel& out_model, std::string& out_error) = 0;
};

// String prefix check
inline bool starts_with(std::string_view str, std::string_view prefix) noexcept {
#if __cplusplus >= 202002L
    return str.starts_with(prefix);
#else
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
#endif
}

// String suffix check
inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
#if __cplusplus >= 202002L
    return str.ends_with(suffix);
#else
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
#endif
}

// Utility function to trim string_view
inline std::string_view trim(std::string_view str) noexcept {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r' || str.front() == '\n')) {
        str.remove_prefix(1);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n')) {
        str.remove_suffix(1);
    }
    return str;
}

inline std::string sanitize_identifier(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (const char character : str) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_') {
            result.push_back(character);
        } else if (character == ' ' || character == '-' || character == '.') {
            result.push_back('_');
        }
    }
    if (!result.empty() && result.front() >= '0' && result.front() <= '9') {
        result = "_" + result;
    }
    return result;
}

}  // namespace fsm::codegen
