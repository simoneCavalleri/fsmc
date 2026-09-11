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

void CameoXmiParser::find_state_machines(const std::shared_ptr<XmlNode>& node,
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
        if (curr->tag == "StateMachine" || ends_with(curr->tag, ":StateMachine") || type_attr == "uml:StateMachine" ||
            xmi_type == "uml:StateMachine" || type_attr == "StateMachine" || xmi_type == "StateMachine") {
            out_sm.push_back(curr);
        }

        for (const auto& child : curr->children) {
            work_list.push_back(child);
        }
    }
}

void CameoXmiParser::parse_state_machine_element(const std::shared_ptr<XmlNode>& parent_node, FsmIr& model,
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
                    std::string choice_name =
                        raw_name.empty() ? ("Choice_" + sanitize_identifier(node_id)) : sanitize_identifier(raw_name);
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
                        std::string clean_name = sanitize_identifier(d_name);
                        auto* curr = model.find_state_mut(state_name);
                        if (curr != nullptr) {
                            curr->deferred_events.push_back(clean_name);
                            model.add_event(clean_name);
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

void CameoXmiParser::parse_transition_element(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
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
        event_name = trans_node->get_attr("event");
    }
    if (event_name.empty()) {
        for (const auto& trig : trans_node->find_children("trigger")) {
            std::string t_name = trig->get_attr("name");
            if (t_name.empty()) {
                t_name = trig->get_attr("event");
            }
            if (t_name.empty()) {
                t_name = trig->get_attr("signal");
            }
            if (!t_name.empty()) {
                event_name = t_name;
                break;
            }
        }
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
        auto parsed = directive::GuardExpressionParser::parse(guard_name);
        if (!parsed.cpp_type.empty()) {
            trans.guard = parsed.cpp_type;
            for (const auto& atomic : parsed.atomic_guards) {
                model.add_guard(atomic);
            }
        }
    }
    if (!action_name.empty()) {
        trans.transition_action = ActionSignature(sanitize_identifier(action_name));
        model.add_action(trans.transition_action->name);
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

}  // namespace fsm::frontend::formal
