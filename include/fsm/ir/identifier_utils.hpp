/**
 * @file identifier_utils.hpp
 * @brief Shared IR and Backend Identifier Normalization, Trimming, and C++ Keyword Escaping Utilities.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace fsm::ir {

/**
 * @brief Trims leading and trailing ASCII whitespace from a string view.
 */
[[nodiscard]] inline std::string_view trim_whitespace(std::string_view str) noexcept {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r' || str.front() == '\n')) {
        str.remove_prefix(1);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n')) {
        str.remove_suffix(1);
    }
    return str;
}

/**
 * @brief Checks if a given identifier is a reserved C++ language keyword.
 */
[[nodiscard]] inline bool is_cpp_keyword(std::string_view token) noexcept {
    static constexpr std::string_view keywords[] = {"alignas",
                                                    "alignof",
                                                    "and",
                                                    "and_eq",
                                                    "asm",
                                                    "atomic_cancel",
                                                    "atomic_commit",
                                                    "atomic_noexcept",
                                                    "auto",
                                                    "bitand",
                                                    "bitor",
                                                    "bool",
                                                    "break",
                                                    "case",
                                                    "catch",
                                                    "char",
                                                    "char8_t",
                                                    "char16_t",
                                                    "char32_t",
                                                    "class",
                                                    "compl",
                                                    "concept",
                                                    "const",
                                                    "consteval",
                                                    "constexpr",
                                                    "constinit",
                                                    "const_cast",
                                                    "continue",
                                                    "co_await",
                                                    "co_return",
                                                    "co_yield",
                                                    "decltype",
                                                    "default",
                                                    "delete",
                                                    "do",
                                                    "double",
                                                    "dynamic_cast",
                                                    "else",
                                                    "enum",
                                                    "explicit",
                                                    "export",
                                                    "extern",
                                                    "false",
                                                    "float",
                                                    "for",
                                                    "friend",
                                                    "goto",
                                                    "if",
                                                    "inline",
                                                    "int",
                                                    "long",
                                                    "mutable",
                                                    "namespace",
                                                    "new",
                                                    "noexcept",
                                                    "not",
                                                    "not_eq",
                                                    "nullptr",
                                                    "operator",
                                                    "or",
                                                    "or_eq",
                                                    "private",
                                                    "protected",
                                                    "public",
                                                    "reflexpr",
                                                    "register",
                                                    "reinterpret_cast",
                                                    "requires",
                                                    "return",
                                                    "short",
                                                    "signed",
                                                    "sizeof",
                                                    "static",
                                                    "static_assert",
                                                    "static_cast",
                                                    "struct",
                                                    "switch",
                                                    "synchronized",
                                                    "template",
                                                    "this",
                                                    "thread_local",
                                                    "throw",
                                                    "true",
                                                    "try",
                                                    "typedef",
                                                    "typeid",
                                                    "typename",
                                                    "union",
                                                    "unsigned",
                                                    "using",
                                                    "virtual",
                                                    "void",
                                                    "volatile",
                                                    "wchar_t",
                                                    "while",
                                                    "xor",
                                                    "xor_eq"};
    for (std::string_view kw : keywords) {
        if (token == kw) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Escapes reserved C++ keywords by appending an underscore.
 */
[[nodiscard]] inline std::string escape_cpp_keyword(std::string_view token) {
    if (is_cpp_keyword(token)) {
        return std::string(token) + "_";
    }
    return std::string(token);
}

/**
 * @brief Converts arbitrary text into a valid C++ alphanumeric identifier.
 */
[[nodiscard]] inline std::string sanitize_identifier(std::string_view str) {
    std::string result;
    result.reserve(str.size() * 2);
    for (std::size_t i = 0; i < str.size(); ++i) {
        char character = str[i];
        if (character == '<') {
            if (i + 1 < str.size() && str[i + 1] == '=') {
                if (!result.empty() && result.back() != '_')
                    result.push_back('_');
                result.append("lte_");
                ++i;
            } else {
                if (!result.empty() && result.back() != '_')
                    result.push_back('_');
                result.append("lt_");
            }
        } else if (character == '>') {
            if (i + 1 < str.size() && str[i + 1] == '=') {
                if (!result.empty() && result.back() != '_')
                    result.push_back('_');
                result.append("gte_");
                ++i;
            } else {
                if (!result.empty() && result.back() != '_')
                    result.push_back('_');
                result.append("gt_");
            }
        } else if (character == '=' && i + 1 < str.size() && str[i + 1] == '=') {
            if (!result.empty() && result.back() != '_')
                result.push_back('_');
            result.append("eq_");
            ++i;
        } else if (character == '!' && i + 1 < str.size() && str[i + 1] == '=') {
            if (!result.empty() && result.back() != '_')
                result.push_back('_');
            result.append("neq_");
            ++i;
        } else if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '_') {
            result.push_back(character);
        } else if (character == ' ' || character == '-' || character == '.') {
            if (result.empty() || result.back() != '_') {
                result.push_back('_');
            }
        }
    }
    while (result.size() > 1 && result.back() == '_') {
        result.pop_back();
    }
    if (!result.empty() && result.front() >= '0' && result.front() <= '9') {
        result = "_" + result;
    }
    return escape_cpp_keyword(result);
}

}  // namespace fsm::ir
