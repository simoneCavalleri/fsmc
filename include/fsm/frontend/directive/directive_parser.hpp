/**
 * @file directive_parser.hpp
 * @brief Parser and formatter for inline embedded @fsm: compiler directives.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::directive {

using namespace fsm::ir;

/**
 * @class DirectiveParser
 * @brief Handles embedded @fsm: metadata directives within diagram notes or formal comments.
 *
 * Supported directives include:
 * - `@fsm:state <name> [entry: <action>] [exit: <action>]`
 * - `@fsm:defer <event>`
 * - `@fsm:signal <name>(type)`
 * - `@fsm:property <id>: <formula>`
 * - `@fsm:port <dir> <name>: <type>`
 * - `@fsm:var <name>: <type> = <init>`
 * - `@fsm:enum <name> { ... }`
 * - `@fsm:struct <name> { ... }`
 */
class DirectiveParser {
  public:
    /**
     * @brief Checks whether a line of text contains an @fsm: compiler directive.
     */
    static bool is_directive(std::string_view line);

    /**
     * @brief Extracts the payload body following the @fsm: prefix.
     */
    static std::string extract_directive_body(std::string_view line);

    /**
     * @brief Parses state-level annotations (entry/exit actions, invariants).
     */
    static bool parse_state_directive(std::string_view body, StateNode& state);

    /**
     * @brief Parses state-level annotations targeting a state in the model by context stack.
     */
    static bool parse_state_directive(std::string_view body, FsmIr& model,
                                      const std::vector<std::string>& parent_stack);

    /**
     * @brief Parses a deferred event specification on a state node.
     */
    static bool parse_defer_directive(std::string_view body, StateNode& state);

    /**
     * @brief Parses a typed signal definition directive.
     */
    static std::optional<SignalDefinition> parse_signal_directive(std::string_view body);

    /**
     * @brief Parses a formal verification property directive (LTL/CTL).
     */
    static std::optional<FormalProperty> parse_property_directive(std::string_view body);

    /**
     * @brief Parses an interface port declaration directive.
     */
    static std::optional<PortDefinition> parse_port_directive(std::string_view body);

    /**
     * @brief Parses an extended state variable definition directive.
     */
    static std::optional<VariableDefinition> parse_variable_directive(std::string_view body);

    /**
     * @brief Parses an enumeration declaration directive.
     */
    static std::optional<EnumDefinition> parse_enum_directive(std::string_view body);

    /**
     * @brief Parses a composite struct definition directive.
     */
    static std::optional<StructDefinition> parse_struct_directive(std::string_view body);

    /**
     * @brief Formats an EnumDefinition into canonical @fsm:enum directive text.
     */
    static std::string format_enum_directive(const EnumDefinition& en);

    /**
     * @brief Formats a TypeDefinition enum into canonical directive text.
     */
    static std::string format_enum_directive(const TypeDefinition& en);

    /**
     * @brief Formats a StructDefinition into canonical @fsm:struct directive text.
     */
    static std::string format_struct_directive(const StructDefinition& st);

    /**
     * @brief Formats a TypeDefinition struct into canonical directive text.
     */
    static std::string format_struct_directive(const TypeDefinition& st);

    /**
     * @brief Parses model-level directives (package name, target options).
     */
    static bool parse_model_directive(std::string_view body, FsmIr& model);

    /**
     * @brief Parses transition-level directives (priority, action signatures).
     */
    static bool parse_trans_directive(std::string_view body, TransitionEdge& trans);
};

}  // namespace fsm::frontend::directive
