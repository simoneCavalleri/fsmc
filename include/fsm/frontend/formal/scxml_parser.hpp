/**
 * @file scxml_parser.hpp
 * @brief W3C SCXML (State Chart XML) frontend parser.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/common/xml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

using XmlNode = ::fsm::frontend::XmlNode;
using SimpleXmlParser = ::fsm::frontend::SimpleXmlParser;

/**
 * @class ScxmlParser
 * @brief Formal frontend for W3C State Chart XML (SCXML) specifications.
 */
class ScxmlParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "scxml"; }

    /**
     * @brief Parses W3C SCXML text into FsmIr.
     * @param content Raw XML content.
     * @param model Output FsmIr model.
     * @param error_message Diagnostic error description on failure.
     * @return True if parse succeeded, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override;

  private:
    void parse_scxml_children(const std::shared_ptr<XmlNode>& parent_node, FsmIr& model,
                              const std::string& current_parent_state);
    static void parse_scxml_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                       const std::string& current_state);
};

}  // namespace fsm::frontend::formal
