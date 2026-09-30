/**
 * @file guard_parser.hpp
 * @brief Parser and normalizer for transition guard boolean expressions.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fsm::ir {
struct GuardModel;
}

namespace fsm::frontend::directive {

/**
 * @struct AtomicGuardInfo
 * @brief Decomposed atomic condition identifier with its underlying expression.
 */
struct AtomicGuardInfo {
    std::string name;
    std::string expression;
};

/**
 * @struct ParsedGuardResult
 * @brief Analysis payload for a parsed transition guard condition.
 */
struct ParsedGuardResult {
    std::string cpp_type;                               ///< Target C++ boolean/predicate type signature
    std::vector<std::string> atomic_guards;             ///< Decomposed atomic condition identifiers
    std::vector<AtomicGuardInfo> atomic_guard_details;  ///< Decomposed atomic guards with expressions
};

/**
 * @class GuardExpressionParser
 * @brief Parses and transforms guard condition syntax for diagrams and formal code generation.
 */
class GuardExpressionParser {
  public:
    /**
     * @brief Converts a raw guard expression into standardized diagram notation.
     */
    static std::string to_diagram_string(std::string_view raw_expr);

    /**
     * @brief Converts a guard expression into standardized notation, resolving atomic leaf
     * identifiers against registered GuardModel raw expressions.
     */
    static std::string to_diagram_string(std::string_view raw_expr, const std::vector<ir::GuardModel>& guards);

    /**
     * @brief Decomposes a raw boolean guard expression into its atomic constituents.
     */
    static ParsedGuardResult parse(std::string_view raw_expr);
};

}  // namespace fsm::frontend::directive
