#pragma once

#include <cctype>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

/**
 * @brief Simulink Stateflow Ingestion Preview Parser (RFC).
 *
 * Ingests MathWorks Simulink Stateflow models exported to XML or JSON formats.
 * Parses Stateflow chart hierarchies, states, and transition labels
 * with format: `Event [Guard] / { Action }` and temporal logic `after(N, sec)`.
 */
class StateflowParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "stateflow"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::string xml_err;
        auto root = SimpleXmlParser::parse(content, xml_err);
        if (!root) {
            error_message = "Stateflow Parser: Failed to parse XML structure: " + xml_err;
            return false;
        }

        // Find Stateflow root or Chart element
        std::shared_ptr<XmlNode> chart_node;
        if (root->tag == "Stateflow" || root->tag == "chart" || root->tag == "machine") {
            chart_node = find_element_recursive(root, "chart");
            if (!chart_node) {
                chart_node = root;
            }
        } else {
            chart_node = find_element_recursive(root, "chart");
        }

        if (!chart_node) {
            error_message = "Stateflow Parser: Root <Stateflow> or <chart> element not found.";
            return false;
        }

        std::string chart_name = chart_node->get_attr("name");
        if (!chart_name.empty()) {
            model.name = sanitize_identifier(chart_name);
        } else {
            model.name = "StateflowChart";
        }

        std::string chart_initial = chart_node->get_attr("initial");
        if (!chart_initial.empty()) {
            model.initial_state = sanitize_identifier(chart_initial);
        }

        // Parse @fsm directives from XML comments
        {
            std::string raw_str{content};
            std::regex comment_re(R"(<!--\s*@fsm:([^\r\n-]+?)\s*-->)");
            auto begin = std::sregex_iterator(raw_str.begin(), raw_str.end(), comment_re);
            auto end = std::sregex_iterator();
            for (auto i = begin; i != end; ++i) {
                std::smatch match = *i;
                std::string body = match[1].str();
                DirectiveParser::parse_model_directive(body, model);
            }
        }

        // Parse Stateflow elements
        parse_chart_elements(chart_node, model, "");

        if (model.states.empty()) {
            error_message = "Stateflow Parser: No states found in Stateflow chart.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
    static std::shared_ptr<XmlNode> find_element_recursive(const std::shared_ptr<XmlNode>& node,
                                                           std::string_view tag_name) {
        if (!node)
            return nullptr;
        if (node->tag == tag_name)
            return node;
        for (const auto& child : node->children) {
            if (auto found = find_element_recursive(child, tag_name)) {
                return found;
            }
        }
        return nullptr;
    }

    void parse_chart_elements(const std::shared_ptr<XmlNode>& node, FsmIr& model, const std::string& parent_state) {
        for (const auto& child : node->children) {
            if (child->tag == "state" || child->tag == "State") {
                std::string st_name = child->get_attr("name");
                std::string ssid = child->get_attr("SSID");
                if (st_name.empty()) {
                    st_name = child->get_attr("id");
                }
                if (st_name.empty() && !ssid.empty()) {
                    st_name = "State_" + ssid;
                }
                if (st_name.empty()) {
                    st_name = "State_" + std::to_string(model.states.size() + 1);
                }

                st_name = sanitize_identifier(st_name);
                model.add_state(st_name, parent_state);

                // Check for Stateflow decomposition (parallel/AND vs exclusive/OR)
                std::string decomp = child->get_attr("decomposition");
                if (decomp == "PARALLEL_AND" || decomp == "AND") {
                    if (auto* s = model.find_state_mut(st_name)) {
                        s->kind = StateKind::Parallel;
                    }
                }

                std::string during_act = child->get_attr("during");
                if (during_act.empty()) {
                    during_act = child->get_attr("do_activity");
                }
                if (!during_act.empty()) {
                    if (auto* s = model.find_state_mut(st_name)) {
                        s->do_activity = sanitize_identifier(during_act);
                    }
                }

                // Recursively parse child states and transitions
                parse_chart_elements(child, model, st_name);
            } else if (child->tag == "junction" || child->tag == "Junction") {
                std::string jtype = child->get_attr("type");
                if (jtype == "HISTORY" || jtype == "history") {
                    if (auto* s = model.find_state_mut(parent_state)) {
                        s->has_history = true;
                        s->has_deep_history = false;
                    }
                } else if (jtype == "HISTORY_DEEP" || jtype == "deep_history") {
                    if (auto* s = model.find_state_mut(parent_state)) {
                        s->has_history = true;
                        s->has_deep_history = true;
                    }
                }
            } else if (child->tag == "transition" || child->tag == "Transition") {
                parse_stateflow_transition(child, model, parent_state);
            }
        }
    }

    void parse_stateflow_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                    const std::string& scope) {
        std::string src = trans_node->get_attr("src");
        std::string dst = trans_node->get_attr("dst");
        if (src.empty() && !dst.empty()) {
            if (scope.empty() && model.initial_state.empty()) {
                model.initial_state = sanitize_identifier(dst);
            }
            return;
        }

        std::string label = trans_node->get_attr("labelString");
        if (label.empty()) {
            label = trans_node->get_attr("label");
        }

        // Stateflow transition syntax: Event [Guard] { ConditionAction } / { TransitionAction }
        std::string event;
        std::string guard;
        std::string action;
        std::optional<TimeTrigger> time_trigger;

        // Check for temporal logic after(N, sec / msec)
        static const std::regex after_re(
            R"(after\s*\(\s*(\d+(?:\.\d+)?)\s*,\s*(sec|msec|sec|seconds|milliseconds|s|ms)\s*\))",
            std::regex::optimize);
        std::smatch match;
        if (std::regex_search(label, match, after_re)) {
            double val = std::stod(match[1].str());
            std::string unit = match[2].str();
            uint64_t dur_ms = static_cast<uint64_t>(val);
            if (unit == "sec" || unit == "s" || unit == "seconds") {
                dur_ms = static_cast<uint64_t>(val * 1000.0);
            }
            time_trigger = TimeTrigger(TimeTriggerKind::After, dur_ms, TimeUnit::Milliseconds);
            event = "after_" + std::to_string(dur_ms) + "ms";
        }

        // Extract Guard [ ... ]
        auto lbracket = label.find('[');
        auto rbracket = label.find(']');
        if (lbracket != std::string::npos && rbracket != std::string::npos && rbracket > lbracket) {
            guard = trim(label.substr(lbracket + 1, rbracket - lbracket - 1));
        }

        // Extract Action / { ... }
        auto slash = label.find('/');
        if (slash != std::string::npos) {
            action = trim(label.substr(slash + 1));
            // strip surrounding braces if present
            if (!action.empty() && action.front() == '{' && action.back() == '}') {
                action = trim(action.substr(1, action.size() - 2));
            }
        }

        // Extract Event if not temporal
        if (event.empty()) {
            std::string before_guard = label.substr(0, (lbracket != std::string::npos) ? lbracket : slash);
            event = trim(before_guard);
        }

        std::string src_name = sanitize_identifier(src.empty() ? scope : src);
        std::string dst_name = sanitize_identifier(dst.empty() ? src_name : dst);

        if (src_name.empty() || dst_name.empty()) {
            return;
        }

        TransitionEdge trans;
        trans.source = src_name;
        trans.target = dst_name;
        trans.event = sanitize_identifier(event);
        if (time_trigger.has_value()) {
            trans.trigger = *time_trigger;
        }
        if (!guard.empty()) {
            auto parsed = GuardExpressionParser::parse(guard);
            if (!parsed.cpp_type.empty()) {
                trans.guard = parsed.cpp_type;
                for (const auto& a : parsed.atomic_guards) {
                    model.add_guard(a);
                }
            } else {
                trans.guard = sanitize_identifier(guard);
                model.add_guard(trans.guard.value());
            }
        }
        if (!action.empty()) {
            trans.action = sanitize_identifier(action);
            model.add_action(trans.action.value());
        }

        model.add_transition(std::move(trans));
    }
};

}  // namespace fsm::frontend::formal

namespace fsm::frontend {
using formal::StateflowParser;
}  // namespace fsm::frontend
