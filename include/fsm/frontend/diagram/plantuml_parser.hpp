/**
 * @file plantuml_parser.hpp
 * @brief Frontend parser for PlantUML state diagram specifications.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::diagram {

/**
 * @class PlantUmlParser
 * @brief Parses PlantUML state diagram textual syntax into canonical FsmIr.
 */
class PlantUmlParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "plantuml"; }

    /**
     * @brief Ingests PlantUML state diagram content into the FsmIr model.
     * @param content Raw PlantUML string.
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
