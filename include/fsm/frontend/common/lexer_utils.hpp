/**
 * @file lexer_utils.hpp
 * @brief Lexing utilities for brackets, quotes, and transition label decomposition.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::frontend {

/**
 * @brief Utility algorithms for tokenizing, bracket extraction, and transition label parsing.
 */
class LexerUtils {
  public:
    /**
     * @brief Extracts content enclosed between balanced opening and closing characters.
     * @param str Input string view.
     * @param open_char Opening bracket character (e.g. '[', '(', '{').
     * @param close_char Closing bracket character (e.g. ']', ')', '}').
     * @return Substring view of the enclosed content, or nullopt if unmatched.
     */
    static std::optional<std::string_view> extract_bracketed(std::string_view str, char open_char, char close_char);

    /**
     * @brief Extracts content enclosed in matching single or double quotation marks.
     * @param str Input string view.
     * @return Substring view of unquoted content, or nullopt if not quoted.
     */
    static std::optional<std::string_view> extract_quoted(std::string_view str);

    /**
     * @brief Parses a standard transition label formatted as: "event [guard] / action".
     * @param label Raw transition label text.
     * @return Tuple containing (event_name, optional<guard_str>, optional<action_str>).
     */
    static std::tuple<std::string, std::optional<std::string>, std::optional<std::string>> parse_transition_label(
        std::string_view label);
};

}  // namespace fsm::frontend
