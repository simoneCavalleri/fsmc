/**
 * @file diagram_action_parser.hpp
 * @brief Parser for composite and multiline action blocks in diagram frontends (PlantUML, Mermaid).
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/action.hpp"

namespace fsm::frontend::diagram {

/**
 * @class DiagramActionParser
 * @brief Utility for parsing single-line and multiline action specifications into ActionSignature.
 */
class DiagramActionParser {
  public:
    /**
     * @brief Parses an action string (braced block or semicolon-separated sequence) into an ActionSignature.
     * @param raw_act Raw action text (e.g. "{ out.alarm = true; reg.count = 0; }" or "act1(); act2();")
     * @param fallback_name Name to use if no explicit single function name is parsed.
     * @return Fully populated ActionSignature with assignments and instruction calls.
     */
    static ir::ActionSignature parse_action_block(std::string_view raw_act,
                                                  const std::string& fallback_name = "action");

    /**
     * @brief Counts brace imbalance in a line, ignoring quotes and comments.
     * @param line Text line.
     * @return Positive value if more '{' than '}', negative if more '}' than '{', or 0.
     */
    static int brace_imbalance(std::string_view line);

    /**
     * @brief Finds the action delimiter '/' in a transition label, ignoring slashes inside brackets, parens, braces, or
     * quotes.
     * @param text The transition label to inspect.
     * @return Position of the delimiter slash, or std::string_view::npos if not found.
     */
    static std::size_t find_action_slash(std::string_view text);
};

}  // namespace fsm::frontend::diagram
