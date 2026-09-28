/**
 * @file stateflow_parser.hpp
 * @brief Formal frontend parser for MATLAB / Simulink Stateflow models.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/common/xml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

using ir::TimeTrigger;
using XmlNode = ::fsm::frontend::XmlNode;
using SimpleXmlParser = ::fsm::frontend::SimpleXmlParser;

/**
 * @class StateflowParser
 * @brief Simulink Stateflow Ingestion Preview Parser (RFC).
 */
class StateflowParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "stateflow"; }

    /**
     * @brief Parses Stateflow XML export into FsmIr.
     * @param content Raw XML content.
     * @param model Output FsmIr model.
     * @param error_message Diagnostic error description on failure.
     * @return True if parsing succeeded, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override;

  private:
    struct StateflowLabelComponents {
        std::string event;
        std::string guard;
        std::string condition_action;
        std::string transition_action;
        std::optional<TimeTrigger> time_trigger;
    };

    static std::shared_ptr<XmlNode> find_element_recursive(const std::shared_ptr<XmlNode>& node,
                                                           std::string_view tag_name);
    void parse_chart_elements(const std::shared_ptr<XmlNode>& node, FsmIr& model, const std::string& parent_state);
    static StateflowLabelComponents parse_stateflow_label(std::string_view raw_label);
    void parse_stateflow_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model, const std::string& scope);
};

}  // namespace fsm::frontend::formal
