#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/guard_parser.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

// ============================================================================
// Lightweight XML Element Representation
// ============================================================================
struct XmlNode {
    std::string tag;
    std::map<std::string, std::string> attributes;
    std::string text_content;
    std::vector<std::shared_ptr<XmlNode>> children;

    [[nodiscard]] std::string get_attr(const std::string& name, const std::string& default_val = "") const {
        // Exact match
        auto attr_it = attributes.find(name);
        if (attr_it != attributes.end()) {
            return attr_it->second;
        }
        // Case-insensitive / prefix-agnostic match (e.g., "xmi:id" or "id" or "xmi:type" or "type")
        for (const auto& [attr_key, attr_val] : attributes) {
            if (attr_key == name || ends_with(attr_key, ":" + name)) {
                return attr_val;
            }
        }
        return default_val;
    }

    [[nodiscard]] std::vector<std::shared_ptr<XmlNode>> find_children(const std::string& tag_name) const {
        std::vector<std::shared_ptr<XmlNode>> result;
        for (const auto& child : children) {
            if (child->tag == tag_name || ends_with(child->tag, ":" + tag_name)) {
                result.push_back(child);
            }
        }
        return result;
    }
};

// ============================================================================
// Minimalist Zero-Dependency XML Tokenizer
// ============================================================================
class SimpleXmlParser {
  public:
    static std::shared_ptr<XmlNode> parse(std::string_view xml_text, std::string& err) {
        size_t idx = 0;
        const size_t len = xml_text.size();
        auto root = std::make_shared<XmlNode>();
        root->tag = "__ROOT__";

        std::vector<std::shared_ptr<XmlNode>> node_stack;
        node_stack.push_back(root);

        while (idx < len) {
            // Find '<'
            size_t tag_start = xml_text.find('<', idx);
            if (tag_start == std::string_view::npos) {
                break;
            }

            // Text before tag
            if (tag_start > idx && !node_stack.empty()) {
                std::string_view text = xml_text.substr(idx, tag_start - idx);
                std::string clean_text = trim_sv(text);
                if (!clean_text.empty()) {
                    node_stack.back()->text_content += clean_text;
                }
            }

            // Check comment <!-- ... -->
            if (starts_with(xml_text.substr(tag_start), "<!--")) {
                size_t comment_end = xml_text.find("-->", tag_start + 4);
                if (comment_end == std::string_view::npos) {
                    idx = len;
                    break;
                }
                idx = comment_end + 3;
                continue;
            }

            // Check processing instruction <? ... ?> or <! ... >
            if (starts_with(xml_text.substr(tag_start), "<?") || starts_with(xml_text.substr(tag_start), "<!")) {
                size_t pi_end = xml_text.find('>', tag_start + 2);
                if (pi_end == std::string_view::npos) {
                    idx = len;
                    break;
                }
                idx = pi_end + 1;
                continue;
            }

            // Closing tag </tag>
            if (starts_with(xml_text.substr(tag_start), "</")) {
                size_t close_end = xml_text.find('>', tag_start + 2);
                if (close_end == std::string_view::npos) {
                    err = "Malformed XML: unclosed closing tag";
                    return nullptr;
                }
                if (node_stack.size() > 1) {
                    node_stack.pop_back();
                }
                idx = close_end + 1;
                continue;
            }

            // Opening or self-closing tag <tag attr="val"...>
            size_t tag_end = xml_text.find('>', tag_start + 1);
            if (tag_end == std::string_view::npos) {
                err = "Malformed XML: unclosed opening tag";
                return nullptr;
            }

            std::string_view tag_body = xml_text.substr(tag_start + 1, tag_end - tag_start - 1);
            bool is_self_closing = false;
            if (ends_with(tag_body, "/")) {
                is_self_closing = true;
                tag_body = tag_body.substr(0, tag_body.size() - 1);
            }

            auto new_node = std::make_shared<XmlNode>();
            parse_tag_body(tag_body, new_node->tag, new_node->attributes);

            if (!node_stack.empty()) {
                node_stack.back()->children.push_back(new_node);
            }

            if (!is_self_closing) {
                node_stack.push_back(new_node);
            }

            idx = tag_end + 1;
        }

        if (node_stack.size() > 1) {
            err = "Malformed XML: unclosed tag <" + node_stack.back()->tag + ">";
            return nullptr;
        }

        if (root->children.empty()) {
            err = "Empty XML document";
            return nullptr;
        }

        return root->children.size() == 1 ? root->children.front() : root;
    }

  private:
    static std::string trim_sv(std::string_view input_sv) {
        size_t start = 0;
        while (start < input_sv.size() && (std::isspace(static_cast<unsigned char>(input_sv[start])) != 0)) {
            start++;
        }
        if (start == input_sv.size()) {
            return "";
        }
        size_t end = input_sv.size() - 1;
        while (end > start && (std::isspace(static_cast<unsigned char>(input_sv[end])) != 0)) {
            end--;
        }
        return std::string(input_sv.substr(start, end - start + 1));
    }

    static void parse_tag_body(std::string_view body, std::string& out_tag,
                               std::map<std::string, std::string>& out_attrs) {
        size_t pos = 0;
        const size_t len = body.size();

        // Skip leading space
        while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
            pos++;
        }

        // Tag name
        size_t name_start = pos;
        while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) == 0) && body[pos] != '/') {
            pos++;
        }
        out_tag = std::string(body.substr(name_start, pos - name_start));

        // Attributes
        while (pos < len) {
            while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
                pos++;
            }
            if (pos >= len) {
                break;
            }

            size_t attr_name_start = pos;
            while (pos < len && body[pos] != '=' && (std::isspace(static_cast<unsigned char>(body[pos])) == 0)) {
                pos++;
            }
            std::string attr_name(body.substr(attr_name_start, pos - attr_name_start));

            while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
                pos++;
            }
            if (pos < len && body[pos] == '=') {
                pos++;
                while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
                    pos++;
                }
                if (pos < len && (body[pos] == '"' || body[pos] == '\'')) {
                    char quote = body[pos++];
                    size_t val_start = pos;
                    while (pos < len && body[pos] != quote) {
                        pos++;
                    }
                    std::string attr_val(body.substr(val_start, pos - val_start));
                    if (pos < len && body[pos] == quote) {
                        pos++;
                    }
                    out_attrs[attr_name] = unescape_xml(attr_val);
                }
            } else if (!attr_name.empty()) {
                out_attrs[attr_name] = "true";
            }
        }
    }

    static std::string unescape_xml(std::string str) {
        auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
            size_t p = 0;
            while ((p = s.find(from, p)) != std::string::npos) {
                s.replace(p, from.length(), to);
                p += to.length();
            }
        };
        replace_all(str, "&amp;", "&");
        replace_all(str, "&lt;", "<");
        replace_all(str, "&gt;", ">");
        replace_all(str, "&quot;", "\"");
        replace_all(str, "&apos;", "'");
        return str;
    }
};

// ============================================================================
// Cameo / MagicDraw OMG XMI 2.x Parser
// ============================================================================
class CameoXmiParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "cameo"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::string xml_err;
        auto root = SimpleXmlParser::parse(content, xml_err);
        if (!root) {
            error_message = "Cameo XMI Parser: Failed to parse XML structure: " + xml_err;
            return false;
        }

        // Map xmi:id -> State Name
        std::map<std::string, std::string> id_to_name;
        // Map xmi:id -> is_choice
        std::map<std::string, bool> id_is_choice;
        // Map xmi:id -> is_initial
        std::map<std::string, bool> id_is_initial;
        // Map xmi:id -> is_history
        std::map<std::string, bool> id_is_history;
        std::map<std::string, bool> id_is_deep_history;

        // 1. Locate StateMachine node(s)
        std::vector<std::shared_ptr<XmlNode>> sm_nodes;
        find_state_machines(root, sm_nodes);

        if (sm_nodes.empty()) {
            error_message =
                "Cameo XMI Parser: No <packagedElement xmi:type=\"uml:StateMachine\"> or "
                "<uml:StateMachine> found.";
            return false;
        }

        const auto& sm_node = sm_nodes.front();
        std::string sm_name = sm_node->get_attr("name");
        if (!sm_name.empty()) {
            model.name = sanitize_identifier(sm_name);
        }

        // 2. Parse regions, states, pseudostates, and transitions
        parse_state_machine_element(sm_node, model, "", id_to_name, id_is_choice, id_is_initial, id_is_history,
                                    id_is_deep_history);

        if (model.states.empty() && model.choice_nodes.empty()) {
            error_message = "Cameo XMI Parser: No valid states or transitions extracted from model.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
    static void find_state_machines(const std::shared_ptr<XmlNode>& node,
                                    std::vector<std::shared_ptr<XmlNode>>& out_sm) {
        if (!node) {
            return;
        }
        std::vector<std::shared_ptr<XmlNode>> work_list;
        work_list.push_back(node);

        while (!work_list.empty()) {
            auto curr = work_list.back();
            work_list.pop_back();

            std::string type_attr = curr->get_attr("type");
            std::string xmi_type = curr->get_attr("xmi:type");
            if (curr->tag == "StateMachine" || ends_with(curr->tag, ":StateMachine") ||
                type_attr == "uml:StateMachine" || xmi_type == "uml:StateMachine" || type_attr == "StateMachine" ||
                xmi_type == "StateMachine") {
                out_sm.push_back(curr);
            }

            for (const auto& child : curr->children) {
                work_list.push_back(child);
            }
        }
    }

    void parse_state_machine_element(const std::shared_ptr<XmlNode>& parent_node, FsmIr& model,
                                     const std::string& current_parent_state,
                                     std::map<std::string, std::string>& id_to_name,
                                     std::map<std::string, bool>& id_is_choice,
                                     std::map<std::string, bool>& id_is_initial,
                                     std::map<std::string, bool>& id_is_history,
                                     std::map<std::string, bool>& id_is_deep_history) {
        // Collect vertices (subvertex / region / state)
        for (const auto& child : parent_node->children) {
            const std::string tag = child->tag;
            const std::string type_attr = child->get_attr("type");
            const std::string xmi_type = child->get_attr("xmi:type");
            const std::string node_id = child->get_attr("id");

            if (tag == "region" || ends_with(tag, ":region")) {
                parse_state_machine_element(child, model, current_parent_state, id_to_name, id_is_choice, id_is_initial,
                                            id_is_history, id_is_deep_history);
                continue;
            }

            if (tag == "subvertex" || tag == "vertex" || ends_with(tag, ":subvertex") || tag == "node") {
                const std::string kind = child->get_attr("kind");
                std::string raw_name = child->get_attr("name");

                // Check Pseudostates (initial, choice, junction, history)
                if (type_attr == "uml:Pseudostate" || xmi_type == "uml:Pseudostate" || !kind.empty()) {
                    if (kind == "initial" || raw_name == "Initial" || raw_name == "initial" ||
                        (kind.empty() && raw_name.find("Initial") != std::string::npos)) {
                        id_is_initial[node_id] = true;
                        id_to_name[node_id] = "[*]";
                    } else if (kind == "choice" || kind == "junction" || raw_name.find("Choice") != std::string::npos) {
                        std::string choice_name = raw_name.empty() ? ("Choice_" + sanitize_identifier(node_id))
                                                                   : sanitize_identifier(raw_name);
                        id_is_choice[node_id] = true;
                        id_to_name[node_id] = choice_name;
                        model.add_choice_node(choice_name);
                    } else if (kind == "shallowHistory" || kind == "history") {
                        std::string hist_name = sanitize_identifier(raw_name.empty() ? current_parent_state : raw_name);
                        id_is_history[node_id] = true;
                        id_to_name[node_id] = hist_name;
                    } else if (kind == "deepHistory") {
                        std::string hist_name = sanitize_identifier(raw_name.empty() ? current_parent_state : raw_name);
                        id_is_deep_history[node_id] = true;
                        id_is_history[node_id] = true;
                        id_to_name[node_id] = hist_name;
                    } else if (kind == "entryPoint") {
                        std::string ep_name = raw_name.empty() ? ("EntryPoint_" + sanitize_identifier(node_id))
                                                               : sanitize_identifier(raw_name);
                        id_to_name[node_id] = ep_name;
                        model.add_or_get_state(ep_name, current_parent_state, StateKind::EntryPoint);
                    } else if (kind == "exitPoint") {
                        std::string xp_name = raw_name.empty() ? ("ExitPoint_" + sanitize_identifier(node_id))
                                                               : sanitize_identifier(raw_name);
                        id_to_name[node_id] = xp_name;
                        model.add_or_get_state(xp_name, current_parent_state, StateKind::ExitPoint);
                    }
                    continue;
                }

                // Regular State
                if (raw_name.empty()) {
                    raw_name = "State_" + sanitize_identifier(node_id);
                }
                const std::string state_name = sanitize_identifier(raw_name);
                id_to_name[node_id] = state_name;

                model.add_state(state_name, current_parent_state);

                // If nested under composite
                if (!current_parent_state.empty()) {
                    auto* parent = model.find_state_mut(current_parent_state);
                    if (parent != nullptr) {
                        parent->is_composite = true;
                        if (parent->initial_sub_state.empty()) {
                            parent->initial_sub_state = state_name;
                        }
                    }
                }

                // Check deferrable triggers (UML 2.5)
                for (const auto& defer_node : child->children) {
                    if (defer_node->tag == "deferrableTrigger" || ends_with(defer_node->tag, ":deferrableTrigger")) {
                        std::string d_name = defer_node->get_attr("name");
                        if (d_name.empty()) {
                            d_name = defer_node->get_attr("trigger");
                        }
                        if (!d_name.empty()) {
                            auto* curr = model.find_state_mut(state_name);
                            if (curr != nullptr) {
                                curr->deferred_events.push_back(sanitize_identifier(d_name));
                            }
                        }
                    }
                }

                // Check entry, exit, and doActivity actions (UML 2.5)
                for (const auto& act_node : child->children) {
                    if (act_node->tag == "entry" || ends_with(act_node->tag, ":entry")) {
                        std::string a_name = act_node->get_attr("name");
                        if (!a_name.empty()) {
                            auto* curr = model.find_state_mut(state_name);
                            if (curr != nullptr) {
                                model.add_action(sanitize_identifier(a_name));
                                curr->entry_actions.push_back(ActionSignature{sanitize_identifier(a_name)});
                            }
                        }
                    } else if (act_node->tag == "exit" || ends_with(act_node->tag, ":exit")) {
                        std::string a_name = act_node->get_attr("name");
                        if (!a_name.empty()) {
                            auto* curr = model.find_state_mut(state_name);
                            if (curr != nullptr) {
                                model.add_action(sanitize_identifier(a_name));
                                curr->exit_actions.push_back(ActionSignature{sanitize_identifier(a_name)});
                            }
                        }
                    } else if (act_node->tag == "doActivity" || ends_with(act_node->tag, ":doActivity")) {
                        std::string a_name = act_node->get_attr("name");
                        if (!a_name.empty()) {
                            auto* curr = model.find_state_mut(state_name);
                            if (curr != nullptr) {
                                curr->do_activity = sanitize_identifier(a_name);
                            }
                        }
                    }
                }

                // Check nested regions inside this state (Composite State)
                for (const auto& sub : child->children) {
                    if (sub->tag == "region" || ends_with(sub->tag, ":region")) {
                        auto* curr = model.find_state_mut(state_name);
                        if (curr != nullptr) {
                            curr->is_composite = true;
                        }
                        parse_state_machine_element(sub, model, state_name, id_to_name, id_is_choice, id_is_initial,
                                                    id_is_history, id_is_deep_history);
                    }
                }
            }
        }

        // Parse transitions in this region
        for (const auto& child : parent_node->children) {
            const std::string tag = child->tag;
            if (tag == "transition" || ends_with(tag, ":transition")) {
                parse_transition_element(child, model, current_parent_state, id_to_name, id_is_choice, id_is_initial,
                                         id_is_history, id_is_deep_history);
            }
        }
    }

    static void parse_transition_element(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                         const std::string& current_parent_state,
                                         const std::map<std::string, std::string>& id_to_name,
                                         const std::map<std::string, bool>& /*id_is_choice*/,
                                         const std::map<std::string, bool>& id_is_initial,
                                         const std::map<std::string, bool>& id_is_history,
                                         const std::map<std::string, bool>& id_is_deep_history) {
        const std::string src_id = trans_node->get_attr("source");
        const std::string dst_id = trans_node->get_attr("target");

        auto src_it = id_to_name.find(src_id);
        auto dst_it = id_to_name.find(dst_id);

        std::string src_name = (src_it != id_to_name.end()) ? src_it->second : sanitize_identifier(src_id);
        std::string dst_name = (dst_it != id_to_name.end()) ? dst_it->second : sanitize_identifier(dst_id);

        bool src_is_init = (id_is_initial.count(src_id) != 0);

        // Initial transition: [*] -> Target
        if (src_is_init || src_name == "[*]" || src_name == "Initial" || src_name == "initial") {
            if (current_parent_state.empty()) {
                model.initial_state = dst_name;
            } else {
                auto* parent = model.find_state_mut(current_parent_state);
                if (parent != nullptr) {
                    parent->initial_sub_state = dst_name;
                }
            }
            return;
        }

        if (src_name.empty() || dst_name.empty()) {
            return;
        }

        // Trigger / Event
        std::string event_name = trans_node->get_attr("trigger");
        if (event_name.empty()) {
            for (const auto& trig : trans_node->find_children("trigger")) {
                std::string t_name = trig->get_attr("name");
                if (!t_name.empty()) {
                    event_name = t_name;
                    break;
                }
            }
        }
        if (event_name.empty()) {
            event_name = trans_node->get_attr("name");
        }

        // Guard
        std::string guard_name = trans_node->get_attr("guard");
        if (guard_name.empty()) {
            for (const auto& guard_node : trans_node->find_children("guard")) {
                std::string g_name = guard_node->get_attr("name");
                if (!g_name.empty()) {
                    guard_name = g_name;
                    break;
                }
                for (const auto& spec : guard_node->find_children("specification")) {
                    std::string body = spec->get_attr("body");
                    if (!body.empty()) {
                        guard_name = body;
                        break;
                    }
                }
            }
        }

        // Action / Effect
        std::string action_name = trans_node->get_attr("effect");
        if (action_name.empty()) {
            action_name = trans_node->get_attr("action");
        }
        if (action_name.empty()) {
            for (const auto& eff : trans_node->find_children("effect")) {
                std::string act_name = eff->get_attr("name");
                if (act_name.empty())
                    act_name = eff->get_attr("action");
                if (act_name.empty())
                    act_name = eff->get_attr("effect");
                if (!act_name.empty()) {
                    action_name = act_name;
                    break;
                }
            }
        }

        bool is_history = (id_is_history.count(dst_id) != 0);
        bool is_deep_history = (id_is_deep_history.count(dst_id) != 0);

        TransitionEdge trans;
        trans.source = src_name;
        trans.target = dst_name;
        trans.event = sanitize_identifier(event_name);
        if (!guard_name.empty()) {
            auto parsed = GuardExpressionParser::parse(guard_name);
            if (!parsed.cpp_type.empty()) {
                trans.guard = parsed.cpp_type;
                for (const auto& atomic : parsed.atomic_guards) {
                    model.add_guard(atomic);
                }
            }
        }
        if (!action_name.empty()) {
            trans.action = sanitize_identifier(action_name);
            model.add_action(*trans.action);
        }
        trans.target_is_history = is_history;
        trans.target_is_deep_history = is_deep_history;

        std::string kind_attr = trans_node->get_attr("kind");
        if (kind_attr == "internal" || (src_name == dst_name && kind_attr == "local")) {
            trans.kind = TransitionEdgeKind::Internal;
        } else {
            trans.kind = TransitionEdgeKind::External;
        }

        std::string prio_attr = trans_node->get_attr("priority");
        if (!prio_attr.empty()) {
            try {
                trans.priority = static_cast<std::uint32_t>(std::stoul(prio_attr));
            } catch (...) {
            }
        }

        if (!model.is_choice_node(src_name)) {
            model.add_state(src_name);
        }
        if (!model.is_choice_node(dst_name)) {
            model.add_state(dst_name);
        }
        if (!trans.event.empty()) {
            model.add_event(trans.event);
        }

        model.add_transition(std::move(trans));
    }
};

}  // namespace fsm::codegen
