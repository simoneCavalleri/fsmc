#pragma once

#include <cctype>
#include <map>
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

class ScxmlParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "scxml"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::string xml_err;
        auto root = SimpleXmlParser::parse(content, xml_err);
        if (!root) {
            error_message = "SCXML Parser: Failed to parse XML structure: " + xml_err;
            return false;
        }

        // Locate <scxml> root
        std::shared_ptr<XmlNode> scxml_node;
        if (root->tag == "scxml" || ends_with(root->tag, ":scxml")) {
            scxml_node = root;
        } else {
            for (const auto& child : root->children) {
                if (child->tag == "scxml" || ends_with(child->tag, ":scxml")) {
                    scxml_node = child;
                    break;
                }
            }
        }

        if (!scxml_node) {
            error_message = "SCXML Parser: Root <scxml> element not found.";
            return false;
        }

        std::string scxml_name = scxml_node->get_attr("name");
        if (!scxml_name.empty()) {
            model.name = sanitize_identifier(scxml_name);
        }

        std::string root_init = scxml_node->get_attr("initial");
        if (!root_init.empty()) {
            model.initial_state = sanitize_identifier(root_init);
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
                DirectiveParser::parse_model_directive(body, model);
            }
        }

        parse_scxml_children(scxml_node, model, "");

        if (model.states.empty()) {
            error_message = "SCXML Parser: No states found in <scxml> document.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
    void parse_scxml_children(const std::shared_ptr<XmlNode>& parent_node, FsmIr& model,
                              const std::string& current_parent_state) {
        // Pass 1: Register all state definitions at this level in document order
        for (const auto& child : parent_node->children) {
            const std::string tag = child->tag;
            if (tag == "state" || ends_with(tag, ":state") || tag == "parallel" || ends_with(tag, ":parallel") ||
                tag == "final" || ends_with(tag, ":final")) {
                std::string state_id = child->get_attr("id");
                if (state_id.empty()) {
                    state_id = "State_" + std::to_string(model.states.size() + 1);
                }
                const std::string state_name = sanitize_identifier(state_id);
                StateKind kind = StateKind::Atomic;
                if (tag == "parallel" || ends_with(tag, ":parallel")) {
                    kind = StateKind::Parallel;
                } else if (tag == "final" || ends_with(tag, ":final")) {
                    kind = StateKind::Final;
                }
                model.add_state(state_name, current_parent_state, kind);

                std::string sub_initial = child->get_attr("initial");
                if (!current_parent_state.empty()) {
                    auto* parent = model.find_state_mut(current_parent_state);
                    if (parent != nullptr) {
                        parent->is_composite = true;
                        if (parent->kind == StateKind::Parallel) {
                            OrthogonalRegion reg;
                            reg.id = state_name;
                            reg.name = state_name;
                            reg.initial_state_id = sub_initial.empty() ? state_name : sanitize_identifier(sub_initial);
                            reg.state_ids.push_back(state_name);
                            parent->orthogonal_regions.push_back(std::move(reg));
                        } else if (parent->initial_sub_state.empty()) {
                            parent->initial_sub_state = state_name;
                        }
                    }
                }

                auto* curr_state = model.find_state_mut(state_name);
                if (curr_state != nullptr) {
                    if (!sub_initial.empty()) {
                        curr_state->is_composite = true;
                        curr_state->initial_sub_state = sanitize_identifier(sub_initial);
                    }
                    std::string req = child->get_attr("satisfies");
                    if (req.empty()) {
                        req = child->get_attr("req");
                    }
                    if (!req.empty()) {
                        curr_state->traceability_reqs.push_back(req);
                    }
                }
            }
        }

        // Pass 2: Process nested contents, datamodel, onentry, onexit, history, defer, and transitions
        for (const auto& child : parent_node->children) {
            const std::string tag = child->tag;

            if (tag == "datamodel" || ends_with(tag, ":datamodel")) {
                for (const auto& data_node : child->children) {
                    if (data_node->tag == "data" || ends_with(data_node->tag, ":data")) {
                        std::string var_id = data_node->get_attr("id");
                        std::string expr = data_node->get_attr("expr");
                        std::string type_str = data_node->get_attr("type");
                        if (!var_id.empty()) {
                            std::string is_port = data_node->get_attr("port");
                            std::string dir_str = data_node->get_attr("dir");
                            if (is_port == "true" || !dir_str.empty()) {
                                PortDefinition port;
                                port.name = sanitize_identifier(var_id);
                                port.type = type_str.empty() ? "Real" : type_str;
                                if (dir_str == "out") {
                                    port.direction = PortDirection::Out;
                                } else if (dir_str == "inout") {
                                    port.direction = PortDirection::InOut;
                                } else {
                                    port.direction = PortDirection::In;
                                }
                                std::string min_s = data_node->get_attr("min");
                                if (!min_s.empty()) {
                                    try {
                                        port.min_value = std::stod(min_s);
                                    } catch (...) {
                                    }
                                }
                                std::string max_s = data_node->get_attr("max");
                                if (!max_s.empty()) {
                                    try {
                                        port.max_value = std::stod(max_s);
                                    } catch (...) {
                                    }
                                }
                                std::string constraint = data_node->get_attr("constraint");
                                if (!constraint.empty()) {
                                    port.constraint = constraint;
                                }
                                model.ports.push_back(std::move(port));
                            } else {
                                VariableDefinition var;
                                var.name = sanitize_identifier(var_id);
                                var.initial_value = expr;
                                var.type = type_str.empty() ? "uint32_t" : type_str;
                                model.add_variable(std::move(var));
                            }
                        }
                    }
                }
            } else if (tag == "onentry" || ends_with(tag, ":onentry")) {
                ActionSignature act;
                for (const auto& entry_child : child->children) {
                    if (entry_child->tag == "assign") {
                        std::string loc = entry_child->get_attr("location");
                        std::string expr = entry_child->get_attr("expr");
                        if (!loc.empty()) {
                            act.assignments.push_back({loc, expr});
                        }
                    } else if (entry_child->tag == "send" || entry_child->tag == "raise") {
                        std::string evt = entry_child->get_attr("event");
                        if (!evt.empty()) {
                            act.name = sanitize_identifier(evt);
                        }
                    } else if (entry_child->tag == "script" || entry_child->tag == "log") {
                        std::string expr = entry_child->get_attr("expr");
                        if (!expr.empty()) {
                            act.name = sanitize_identifier(expr);
                        }
                    }
                }
                std::string direct_act = child->get_attr("action");
                if (!direct_act.empty()) {
                    act.name = sanitize_identifier(direct_act);
                }
                if (act.name.empty() && !act.assignments.empty()) {
                    act.name = "entry_" + current_parent_state;
                }
                if (!act.name.empty() || !act.assignments.empty()) {
                    auto* curr_state = model.find_state_mut(current_parent_state);
                    if (curr_state != nullptr) {
                        model.add_action(act.name);
                        curr_state->entry_actions.push_back(std::move(act));
                    }
                }
            } else if (tag == "onexit" || ends_with(tag, ":onexit")) {
                ActionSignature act;
                for (const auto& exit_child : child->children) {
                    if (exit_child->tag == "assign") {
                        std::string loc = exit_child->get_attr("location");
                        std::string expr = exit_child->get_attr("expr");
                        if (!loc.empty()) {
                            act.assignments.push_back({loc, expr});
                        }
                    } else if (exit_child->tag == "send" || exit_child->tag == "raise") {
                        std::string evt = exit_child->get_attr("event");
                        if (!evt.empty()) {
                            act.name = sanitize_identifier(evt);
                        }
                    }
                }
                std::string direct_act = child->get_attr("action");
                if (!direct_act.empty()) {
                    act.name = sanitize_identifier(direct_act);
                }
                if (act.name.empty() && !act.assignments.empty()) {
                    act.name = "exit_" + current_parent_state;
                }
                if (!act.name.empty() || !act.assignments.empty()) {
                    auto* curr_state = model.find_state_mut(current_parent_state);
                    if (curr_state != nullptr) {
                        model.add_action(act.name);
                        curr_state->exit_actions.push_back(std::move(act));
                    }
                }
            } else if (tag == "state" || ends_with(tag, ":state") || tag == "parallel" || ends_with(tag, ":parallel") ||
                       tag == "final" || ends_with(tag, ":final")) {
                std::string state_id = child->get_attr("id");
                if (state_id.empty()) {
                    state_id = "State_" + std::to_string(model.states.size() + 1);
                }
                const std::string state_name = sanitize_identifier(state_id);
                parse_scxml_children(child, model, state_name);
            } else if (tag == "history" || ends_with(tag, ":history")) {
                std::string hist_type = child->get_attr("type");
                std::string hist_id = child->get_attr("id");
                std::string target_state = current_parent_state;
                if (target_state.empty() && !hist_id.empty()) {
                    target_state = sanitize_identifier(hist_id);
                }
                auto* curr_state = model.find_state_mut(target_state);
                if (curr_state != nullptr) {
                    curr_state->has_history = true;
                    if (hist_type == "deep") {
                        curr_state->has_deep_history = true;
                    }
                }
            } else if (tag == "defer" || ends_with(tag, ":defer")) {
                std::string defer_event = child->get_attr("event");
                if (!defer_event.empty()) {
                    std::string s_evt = sanitize_identifier(defer_event);
                    SignalDefinition sig_def;
                    sig_def.name = s_evt;
                    model.add_signal(std::move(sig_def));
                    auto* curr_state = model.find_state_mut(current_parent_state);
                    if (curr_state != nullptr) {
                        curr_state->deferred_events.push_back(s_evt);
                    }
                }
            } else if (tag == "invoke" || ends_with(tag, ":invoke")) {
                std::string act = child->get_attr("src");
                if (act.empty()) {
                    act = child->get_attr("id");
                }
                if (!act.empty()) {
                    auto* curr_state = model.find_state_mut(current_parent_state);
                    if (curr_state != nullptr) {
                        curr_state->do_activity = sanitize_identifier(act);
                    }
                }
            } else if (tag == "transition" || ends_with(tag, ":transition")) {
                parse_scxml_transition(child, model, current_parent_state);
            }
        }
    }

    static void parse_scxml_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                       const std::string& current_state) {
        std::string event = trans_node->get_attr("event");
        std::string cond = trans_node->get_attr("cond");
        std::string target = trans_node->get_attr("target");
        std::string action;
        std::vector<ActionAssignment> assignments;

        if (action.empty())
            action = trans_node->get_attr("action");
        if (action.empty())
            action = trans_node->get_attr("effect");
        for (const auto& child : trans_node->children) {
            if (child->tag == "assign") {
                std::string loc = child->get_attr("location");
                std::string expr = child->get_attr("expr");
                if (!loc.empty()) {
                    assignments.push_back({loc, expr});
                }
            } else if (child->tag == "send" || child->tag == "raise" || ends_with(child->tag, ":send") ||
                       ends_with(child->tag, ":raise")) {
                std::string send_event = child->get_attr("event");
                if (!send_event.empty()) {
                    action = send_event;
                }
            } else if (child->tag == "log" || child->tag == "script") {
                std::string log_expr = child->get_attr("expr");
                if (!log_expr.empty()) {
                    action = log_expr;
                }
            }
        }

        if (target.empty() && current_state.empty()) {
            return;
        }

        std::string src = current_state;
        std::string dst = target.empty() ? src : target;

        bool is_internal = target.empty() || trans_node->get_attr("type") == "internal";

        TransitionEdge trans;
        trans.source = sanitize_identifier(src);
        trans.target = sanitize_identifier(dst);
        trans.event = sanitize_identifier(event);
        if (!cond.empty()) {
            auto parsed = GuardExpressionParser::parse(cond);
            if (!parsed.cpp_type.empty()) {
                trans.guard = parsed.cpp_type;
                for (const auto& atomic : parsed.atomic_guards) {
                    model.add_guard(atomic);
                }
            }
        }
        if (!action.empty() || !assignments.empty()) {
            std::string act_name =
                !action.empty() ? sanitize_identifier(action) : ("act_" + trans.source + "_" + trans.target);
            trans.action = act_name;
            ActionSignature sig;
            sig.name = act_name;
            sig.assignments = assignments;
            trans.action_sig = std::move(sig);
            model.add_action(act_name);
        }
        trans.kind = is_internal ? TransitionEdgeKind::Internal : TransitionEdgeKind::External;

        std::string prio_str = trans_node->get_attr("priority");
        if (!prio_str.empty()) {
            try {
                trans.priority = static_cast<std::uint32_t>(std::stoul(prio_str));
            } catch (...) {
            }
        }

        if (!model.is_choice_node(trans.source)) {
            model.add_state(trans.source);
        }
        if (!model.is_choice_node(trans.target)) {
            model.add_state(trans.target);
        }
        if (!trans.event.empty()) {
            model.add_event(trans.event);
        }

        model.add_transition(std::move(trans));
    }
};

}  // namespace fsm::frontend::formal

namespace fsm::frontend {
using formal::ScxmlParser;
}  // namespace fsm::frontend
