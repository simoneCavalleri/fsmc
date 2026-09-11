/**
 * @file mermaid_parser.hpp
 * @brief Frontend parser for Mermaid.js stateDiagram-v2 definitions.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::diagram {

/**
 * @class MermaidParser
 * @brief Parses Mermaid stateDiagram-v2 textual syntax into canonical FsmIr.
 */
class MermaidParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "mermaid"; }

    /**
     * @brief Ingests Mermaid stateDiagram content into the FsmIr model.
     * @param content Raw Mermaid stateDiagram string.
     * @param out_model Destination FsmIr to populate.
     * @param out_error Diagnostic error message upon failure.
     * @return True if parsing succeeded, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& out_model, std::string& out_error) override;

  private:
    static std::string parse_composite_state_header(std::string_view line, FsmIr& model,
                                                    const std::vector<std::string>& parent_stack);

    static void parse_choice_definition(std::string_view line, FsmIr& model);

    static void parse_state_definition(std::string_view line, FsmIr& model,
                                       const std::vector<std::string>& parent_stack);

    static void parse_deferred_event(std::string_view line, FsmIr& model, const std::vector<std::string>& parent_stack);

    static void parse_internal_transition(std::string_view line, FsmIr& model,
                                          const std::vector<std::string>& parent_stack);

    static bool parse_transition_line(std::string_view line, FsmIr& model, std::string& out_error, size_t line_num,
                                      const std::vector<std::string>& parent_stack);
};

}  // namespace fsm::frontend::diagram
