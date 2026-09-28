/**
 * @file smv_parser.hpp
 * @brief Formal parser for nuXmv / NuSMV / SMV verification specifications.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

/**
 * @class SmvParser
 * @brief Formal parser for nuXmv / NuSMV / SMV Formal Verification Language.
 */
class SmvParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "smv"; }

    /**
     * @brief Parses an SMV module definition into the target FsmIr model.
     * @param content Raw SMV file text.
     * @param model Destination FsmIr model.
     * @param error_message Diagnostic error description on failure.
     * @return True if parsing succeeded, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override;

  private:
    static std::string trim(std::string_view str);
    static void parse_enum_states(const std::string& line, FsmIr& model);
    static void parse_enum_events(const std::string& line, FsmIr& model);
    static void parse_variable_decl(const std::string& line, FsmIr& model);
    static void parse_transition_case(const std::string& line, FsmIr& model);
    static void parse_state_metadata_directive(const std::string& body, FsmIr& model);
    static void parse_action_directive(const std::string& body, FsmIr& model, const std::string& type);
    static void parse_defer_directive(const std::string& body, FsmIr& model);
    static void parse_req_directive(const std::string& body, FsmIr& model);
    static void parse_trans_action_directive(const std::string& body, FsmIr& model);
    static std::string extract_word_or_quoted(const std::string& str, size_t pos);
};

}  // namespace fsm::frontend::formal
