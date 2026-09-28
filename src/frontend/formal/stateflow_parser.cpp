#include "fsm/frontend/formal/stateflow_parser.hpp"

#include <cctype>
#include <regex>
#include <utility>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using directive::DirectiveParser;
using directive::GuardExpressionParser;

bool StateflowParser::parse(std::string_view content, FsmIr& model, std::string& error_message) {
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
            directive::DirectiveParser::parse_model_directive(body, model);
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

std::shared_ptr<XmlNode> StateflowParser::find_element_recursive(const std::shared_ptr<XmlNode>& node,
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

void StateflowParser::parse_chart_elements(const std::shared_ptr<XmlNode>& node, FsmIr& model,
                                           const std::string& parent_state) {
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
            std::string jid = child->get_attr("SSID");
            if (jid.empty()) {
                jid = child->get_attr("id");
            }
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
            } else {
                std::string jname =
                    sanitize_identifier("Junction_" + (jid.empty() ? std::to_string(model.states.size() + 1) : jid));
                model.add_state(jname, parent_state, StateKind::Junction);
            }
        } else if (child->tag == "transition" || child->tag == "Transition") {
            parse_stateflow_transition(child, model, parent_state);
        }
    }
}

StateflowParser::StateflowLabelComponents StateflowParser::parse_stateflow_label(std::string_view raw_label) {
    StateflowLabelComponents res;
    std::string label = std::string(trim(raw_label));
    if (label.empty()) {
        return res;
    }

    // Check for temporal logic after(N, sec / msec)
    static const std::regex after_re(R"(after\s*\(\s*(\d+(?:\.\d+)?)\s*,\s*(sec|msec|seconds|milliseconds|s|ms)\s*\))",
                                     std::regex::optimize);
    std::smatch match;
    if (std::regex_search(label, match, after_re)) {
        double val = std::stod(match[1].str());
        std::string unit = match[2].str();
        uint64_t dur_ms = static_cast<uint64_t>(val);
        if (unit == "sec" || unit == "s" || unit == "seconds") {
            dur_ms = static_cast<uint64_t>(val * 1000.0);
        }
        res.time_trigger = TimeTrigger(TimeTriggerKind::After, dur_ms, TimeUnit::Milliseconds);
        res.event = "after_" + std::to_string(dur_ms) + "ms";
    }

    size_t idx = 0;
    const size_t len = label.size();

    // 1. Event part: up to '[' or '{' or '/' (if not already matched temporal)
    if (res.event.empty()) {
        size_t ev_end = 0;
        while (ev_end < len && label[ev_end] != '[' && label[ev_end] != '{' && label[ev_end] != '/') {
            ev_end++;
        }
        res.event = std::string(trim(label.substr(0, ev_end)));
        idx = ev_end;
    } else {
        size_t first_delim = label.find_first_of("[{/");
        if (first_delim != std::string::npos) {
            idx = first_delim;
        } else {
            idx = len;
        }
    }

    // 2. Scan brackets and braces with nesting support
    while (idx < len) {
        char c = label[idx];
        if (c == '[') {
            idx++;
            size_t depth = 1;
            size_t g_start = idx;
            while (idx < len && depth > 0) {
                if (label[idx] == '[') {
                    depth++;
                } else if (label[idx] == ']') {
                    depth--;
                }
                if (depth > 0) {
                    idx++;
                }
            }
            res.guard = std::string(trim(label.substr(g_start, idx - g_start)));
            if (idx < len && label[idx] == ']') {
                idx++;
            }
        } else if (c == '{') {
            idx++;
            size_t depth = 1;
            size_t ca_start = idx;
            while (idx < len && depth > 0) {
                if (label[idx] == '{') {
                    depth++;
                } else if (label[idx] == '}') {
                    depth--;
                }
                if (depth > 0) {
                    idx++;
                }
            }
            res.condition_action = std::string(trim(label.substr(ca_start, idx - ca_start)));
            if (idx < len && label[idx] == '}') {
                idx++;
            }
        } else if (c == '/') {
            idx++;
            while (idx < len && (std::isspace(static_cast<unsigned char>(label[idx])) != 0)) {
                idx++;
            }
            if (idx < len && label[idx] == '{') {
                idx++;
                size_t depth = 1;
                size_t ta_start = idx;
                while (idx < len && depth > 0) {
                    if (label[idx] == '{') {
                        depth++;
                    } else if (label[idx] == '}') {
                        depth--;
                    }
                    if (depth > 0) {
                        idx++;
                    }
                }
                res.transition_action = std::string(trim(label.substr(ta_start, idx - ta_start)));
                if (idx < len && label[idx] == '}') {
                    idx++;
                }
            } else {
                res.transition_action = std::string(trim(label.substr(idx)));
                idx = len;
            }
        } else {
            idx++;
        }
    }

    return res;
}

void StateflowParser::parse_stateflow_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
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

    auto comps = parse_stateflow_label(label);

    std::string src_name = sanitize_identifier(src.empty() ? scope : src);
    std::string dst_name = sanitize_identifier(dst.empty() ? src_name : dst);

    if (src_name.empty() || dst_name.empty()) {
        return;
    }

    TransitionEdge trans;
    trans.source = src_name;
    trans.target = dst_name;
    trans.event = sanitize_identifier(comps.event);
    if (comps.time_trigger.has_value()) {
        trans.trigger = *comps.time_trigger;
    }
    if (!comps.guard.empty()) {
        auto parsed = directive::GuardExpressionParser::parse(comps.guard);
        if (!parsed.cpp_type.empty()) {
            trans.guard = parsed.cpp_type;
            for (const auto& a : parsed.atomic_guards) {
                model.add_guard(a);
            }
        } else {
            trans.guard = sanitize_identifier(comps.guard);
            model.add_guard(trans.guard.value());
        }
    }
    if (!comps.condition_action.empty()) {
        trans.condition_action = ActionSignature(sanitize_identifier(comps.condition_action));
        model.add_action(trans.condition_action->name);
    }
    if (!comps.transition_action.empty()) {
        trans.transition_action = ActionSignature(sanitize_identifier(comps.transition_action));
        model.add_action(trans.transition_action->name);
    }

    model.add_transition(std::move(trans));
}

}  // namespace fsm::frontend::formal
