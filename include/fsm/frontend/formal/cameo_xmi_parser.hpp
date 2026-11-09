/**
 * @file cameo_xmi_parser.hpp
 * @brief Formal parser for Cameo / MagicDraw OMG XMI 2.x models.
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/common/xml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

using XmlNode = ::fsm::frontend::XmlNode;
using SimpleXmlParser = ::fsm::frontend::SimpleXmlParser;

// ============================================================================
// Cameo / MagicDraw OMG XMI 2.x Parser
// ============================================================================

/**
 * @class CameoXmiParser
 * @brief Ingests Cameo Systems Modeler / MagicDraw OMG UML 2.5 XMI statecharts into FsmIr.
 */
class CameoXmiParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "cameo"; }

    /**
     * @brief Parses XMI text content into the destination FsmIr model.
     * @param content Raw XML/XMI document text.
     * @param model Output FsmIr model.
     * @param error_message Diagnostic error description on failure.
     * @return True if parsing succeeded, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override;

  private:
    static void find_signals(const std::shared_ptr<XmlNode>& node, std::vector<std::shared_ptr<XmlNode>>& out_signals);
};

using CameoParser = CameoXmiParser;

}  // namespace fsm::frontend::formal
