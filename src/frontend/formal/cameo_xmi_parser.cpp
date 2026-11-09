#include "fsm/frontend/formal/cameo_xmi_parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>
#include <utility>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_graph_resolver.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using directive::DirectiveParser;
using directive::GuardExpressionParser;

bool CameoXmiParser::parse(std::string_view content, FsmIr& model, std::string& error_message) {
    std::string xml_err;
    auto root = SimpleXmlParser::parse(content, xml_err);
    if (!root) {
        error_message = "Cameo XMI Parser: Failed to parse XML structure: " + xml_err;
        return false;
    }

    // Parse @fsm directives from comments
    {
        std::string raw_str{content};
        std::regex comment_re(R"(<!--\s*@fsm:([^\r\n-]+?)\s*-->)");
        auto begin = std::sregex_iterator(raw_str.begin(), raw_str.end(), comment_re);
        auto end = std::sregex_iterator();
        for (auto i = begin; i != end; ++i) {
            std::smatch match = *i;
            std::string body = match[1].str();
            directive::DirectiveParser::parse_model_directive(body, model);
        }
    }

    // Delegate to Two-Pass Relational Graph Resolver
    if (!CameoXmiGraphResolver::resolve(root, model, error_message)) {
        return false;
    }

    // Find and register all declared Signals
    std::vector<std::shared_ptr<XmlNode>> signal_nodes;
    find_signals(root, signal_nodes);
    for (const auto& sig_node : signal_nodes) {
        std::string sig_name = sig_node->get_attr("name");
        if (!sig_name.empty()) {
            SignalDefinition sig_def;
            sig_def.name = sanitize_identifier(sig_name);
            model.add_signal(std::move(sig_def));
        }
    }

    return true;
}

void CameoXmiParser::find_signals(const std::shared_ptr<XmlNode>& node,
                                  std::vector<std::shared_ptr<XmlNode>>& out_signals) {
    if (!node)
        return;
    std::vector<std::shared_ptr<XmlNode>> work_list;
    work_list.push_back(node);
    while (!work_list.empty()) {
        auto curr = work_list.back();
        work_list.pop_back();
        std::string type_attr = curr->get_attr("type");
        std::string xmi_type = curr->get_attr("xmi:type");
        if (curr->tag == "Signal" || ends_with(curr->tag, ":Signal") || type_attr == "uml:Signal" ||
            xmi_type == "uml:Signal" || type_attr == "Signal" || xmi_type == "Signal") {
            out_signals.push_back(curr);
        }
        for (const auto& child : curr->children) {
            work_list.push_back(child);
        }
    }
}

}  // namespace fsm::frontend::formal
