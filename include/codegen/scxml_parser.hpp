#pragma once

#include <cctype>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/guard_parser.hpp"
#include "codegen/parser_interface.hpp"

namespace fsm::codegen {

class ScxmlParser : public IParser {
  public:
    [[nodiscard]] static std::string format_name() { return "scxml"; }

    bool parse(std::string_view content, FsmModel& model, std::string& error_message) override {
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
    void parse_scxml_children(const std::shared_ptr<XmlNode>& parent_node, FsmModel& model,
                              const std::string& current_parent_state) {
        for (const auto& child : parent_node->children) {
            const std::string tag = child->tag;

            if (tag == "state" || ends_with(tag, ":state") || tag == "parallel") {
                std::string state_id = child->get_attr("id");
                if (state_id.empty()) {
                    state_id = "State_" + std::to_string(model.states.size() + 1);
                }
                const std::string state_name = sanitize_identifier(state_id);
                model.add_state(state_name, current_parent_state);

                std::string sub_initial = child->get_attr("initial");
                if (!current_parent_state.empty()) {
                    auto* parent = model.find_state_mut(current_parent_state);
                    if (parent != nullptr) {
                        parent->is_composite = true;
                        if (parent->initial_sub_state.empty()) {
                            parent->initial_sub_state = state_name;
                        }
                    }
                }

                auto* curr_state = model.find_state_mut(state_name);
                if (curr_state != nullptr && !sub_initial.empty()) {
                    curr_state->is_composite = true;
                    curr_state->initial_sub_state = sanitize_identifier(sub_initial);
                }

                // Check nested elements inside state
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
                    auto* curr_state = model.find_state_mut(current_parent_state);
                    if (curr_state != nullptr) {
                        curr_state->deferred_events.push_back(sanitize_identifier(defer_event));
                    }
                }
            } else if (tag == "transition" || ends_with(tag, ":transition")) {
                parse_scxml_transition(child, model, current_parent_state);
            }
        }
    }

    static void parse_scxml_transition(const std::shared_ptr<XmlNode>& trans_node, FsmModel& model,
                                       const std::string& current_state) {
        std::string event = trans_node->get_attr("event");
        std::string cond = trans_node->get_attr("cond");
        std::string target = trans_node->get_attr("target");
        std::string action;

        // Action from child <send>, <raise>, <script>, <log>
        for (const auto& child : trans_node->children) {
            if (child->tag == "send" || child->tag == "raise" || ends_with(child->tag, ":send")) {
                std::string send_event = child->get_attr("event");
                if (!send_event.empty()) {
                    action = send_event;
                    break;
                }
            }
            if (child->tag == "log" || child->tag == "script") {
                std::string log_expr = child->get_attr("expr");
                if (!log_expr.empty()) {
                    action = log_expr;
                    break;
                }
            }
        }

        if (target.empty() && current_state.empty()) {
            return;
        }

        std::string src = current_state;
        std::string dst = target.empty() ? src : target;

        bool is_internal = target.empty() || trans_node->get_attr("type") == "internal";

        TransitionModel trans;
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
        if (!action.empty()) {
            trans.action = sanitize_identifier(action);
            model.add_action(*trans.action);
        }
        trans.kind = is_internal ? TransitionKind::Internal : TransitionKind::External;

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

}  // namespace fsm::codegen
