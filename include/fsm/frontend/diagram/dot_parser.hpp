/**
 * @file dot_parser.hpp
 * @brief Parser frontend for Graphviz DOT state diagram specifications.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::diagram {

/**
 * @class DotParser
 * @brief Ingests Graphviz DOT digraph representations into canonical FsmIr.
 */
class DotParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "dot"; }

    /**
     * @brief Parses a DOT diagram into the provided FsmIr model.
     * @param content Raw DOT diagram string.
     * @param model Destination FsmIr to populate.
     * @param error_message Output diagnostic message upon parsing failure.
     * @return True if parsed successfully, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override;

  private:
    static std::string trim_line(std::string_view line_sv);
    static void parse_label(const std::string& label, std::string& out_event, std::string& out_guard,
                            std::string& out_action);
};

}  // namespace fsm::frontend::diagram
