/**
 * @file ltl_parser.hpp
 * @brief Tokenizer and recursive-descent parser for temporal logic (LTL/CTL) properties.
 */

#pragma once

#include <optional>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::directive {

using ir::PropertyAstNode;

/**
 * @class LtlPropertyParser
 * @brief Tokenizer and Recursive-Descent Parser for Linear Temporal Logic (LTL)
 * and Computation Tree Logic (CTL) property formulas.
 */
class LtlPropertyParser {
  public:
    /**
     * @brief Parses a temporal logic formula string into an AST.
     * @param raw_formula String containing the formula (e.g. "G(ready -> F(done))").
     * @return Root PropertyAstNode if syntax is valid, std::nullopt otherwise.
     */
    static std::optional<PropertyAstNode> parse(std::string_view raw_formula);
};

}  // namespace fsm::frontend::directive
