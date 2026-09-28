#include "fsm/frontend/formal/cameo_xmi_graph_resolver.hpp"

#include <unordered_map>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::formal {

using namespace ::fsm::ir;
using namespace ::fsm::frontend::directive;

void CameoXmiGraphResolver::build_id_map(const std::shared_ptr<XmlNode>& node,
                                         std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map) {
    if (!node) {
        return;
    }

    // Filter out graphical diagram layout bloat
    if (node->tag == "Diagram" || ends_with(node->tag, ":Diagram") || node->tag == "DiagramElement" ||
        ends_with(node->tag, ":DiagramElement") || node->tag == "diagram_representation") {
        return;
    }

    std::string id = node->get_attr("xmi:id");
    if (id.empty()) {
        id = node->get_attr("id");
    }
    if (!id.empty()) {
        id_map[id] = node;
    }

    for (const auto& child : node->children) {
        build_id_map(child, id_map);
    }
}

std::shared_ptr<XmlNode> CameoXmiGraphResolver::find_state_machine(
    const std::shared_ptr<XmlNode>& root, const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map) {
    // Search in id_map first
    for (const auto& [_, node] : id_map) {
        std::string type = node->get_attr("xmi:type");
        if (type == "uml:StateMachine" || type == "StateMachine" || node->tag == "StateMachine" ||
            ends_with(node->tag, ":StateMachine")) {
            return node;
        }
    }
    // Fallback recursive search
    return root->find_child_recursive("StateMachine");
}

void CameoXmiGraphResolver::process_region(const std::shared_ptr<XmlNode>& region_node, FsmIr& model,
                                           const std::string& parent_state,
                                           const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                           std::unordered_map<std::string, std::string>& id_to_name,
                                           std::unordered_map<std::string, StateKind>& id_to_kind) {
    for (const auto& child : region_node->children) {
        if (child->tag == "subvertex" || ends_with(child->tag, ":subvertex")) {
            process_subvertex(child, model, parent_state, id_map, id_to_name, id_to_kind);
        }
    }
}

/**
 * @brief Processes an individual Cameo/MagicDraw UML subvertex XML node (state or pseudostate).
 *
 * Implements the following UML/SysML mapping stages:
 * 1. Attribute extraction: retrieves XML ID, type tag, pseudostate kind, and node display name.
 * 2. Pseudostate classification: identifies Initial, Choice, Junction, ShallowHistory, and DeepHistory nodes.
 * 3. State name canonicalization: generates a sanitized C++ identifier for state symbols.
 * 4. Identifier mapping: records XMI ID-to-name and ID-to-kind associations for subsequent transition edge resolution.
 * 5. History configuration: updates parent state history flags when a history pseudostate is encountered.
 * 6. State registration: adds non-pseudostate nodes to the canonical FsmIr model.
 * 7. Action effect parsing: extracts entry, exit, doActivity, and deferrableTrigger definitions.
 * 8. Nested region traversal: recursively resolves child composite regions and regional transitions.
 *
 * @param vertex_node XML node representing the vertex element.
 * @param model Intermediate representation receiving state definitions.
 * @param parent_state Identifier of the enclosing parent composite state.
 * @param id_map Global map of XMI ID to XML DOM nodes.
 * @param id_to_name Map from XMI ID to sanitized state names.
 * @param id_to_kind Map from XMI ID to StateKind enumerations.
 */
void CameoXmiGraphResolver::process_subvertex(const std::shared_ptr<XmlNode>& vertex_node, FsmIr& model,
                                              const std::string& parent_state,
                                              const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                              std::unordered_map<std::string, std::string>& id_to_name,
                                              std::unordered_map<std::string, StateKind>& id_to_kind) {
    // 1. Extract XML attributes and element metadata
    std::string id = vertex_node->get_attr("xmi:id");
    if (id.empty()) {
        id = vertex_node->get_attr("id");
    }
    std::string type = vertex_node->get_attr("xmi:type");
    if (type.empty()) {
        type = vertex_node->get_attr("type");
    }
    std::string kind = vertex_node->get_attr("kind");
    std::string raw_name = vertex_node->get_attr("name");

    StateKind st_kind = StateKind::Atomic;
    bool is_initial = false;

    // 2. Classify UML pseudostates (Initial, Choice, Junction, ShallowHistory, DeepHistory)
    if (type == "uml:Pseudostate" || ends_with(type, ":Pseudostate") || kind == "initial" || kind == "choice" ||
        kind == "junction" || kind == "shallowHistory" || kind == "deepHistory") {
        if (kind == "initial" || raw_name == "Initial" || raw_name == "initial") {
            st_kind = StateKind::Initial;
            is_initial = true;
        } else if (kind == "choice") {
            st_kind = StateKind::Choice;
        } else if (kind == "junction") {
            st_kind = StateKind::Junction;
        } else if (kind == "shallowHistory") {
            st_kind = StateKind::ShallowHistory;
        } else if (kind == "deepHistory") {
            st_kind = StateKind::DeepHistory;
        }
    }

    // 3. Resolve canonical state name
    std::string state_name;
    if (!raw_name.empty()) {
        state_name = sanitize_identifier(raw_name);
    } else if (is_initial) {
        state_name = parent_state.empty() ? "Initial" : (parent_state + "_Initial");
    } else if (!id.empty()) {
        state_name = "State_" + sanitize_identifier(id);
    } else {
        state_name = "State_" + std::to_string(model.states.size() + 1);
    }

    // 4. Record mapping from XMI ID to state name and kind
    if (!id.empty()) {
        id_to_name[id] = state_name;
        id_to_kind[id] = st_kind;
    }

    // 5. Handle History pseudostates (attach history semantics directly to enclosing parent state)
    if (st_kind == StateKind::ShallowHistory || st_kind == StateKind::DeepHistory) {
        if (!parent_state.empty()) {
            if (auto* p = model.find_state_mut(parent_state)) {
                p->has_history = true;
                if (st_kind == StateKind::DeepHistory) {
                    p->has_deep_history = true;
                }
            }
        }
        return;
    }

    // 6. Register regular states and choice nodes into FsmIr
    if (st_kind != StateKind::Initial && st_kind != StateKind::Junction) {
        model.add_state(state_name, parent_state, st_kind);
    }

    // 7. Parse entry, exit, doActivity, and deferrableTrigger actions
    for (const auto& act : vertex_node->children) {
        // Entry actions
        if (act->tag == "entry" || ends_with(act->tag, ":entry")) {
            std::string aname = act->get_attr("name");
            if (!aname.empty()) {
                model.add_action(sanitize_identifier(aname));
                if (auto* s = model.find_state_mut(state_name)) {
                    s->entry_actions.push_back(ActionSignature(sanitize_identifier(aname)));
                }
            }
            // Exit actions
        } else if (act->tag == "exit" || ends_with(act->tag, ":exit")) {
            std::string aname = act->get_attr("name");
            if (!aname.empty()) {
                model.add_action(sanitize_identifier(aname));
                if (auto* s = model.find_state_mut(state_name)) {
                    s->exit_actions.push_back(ActionSignature(sanitize_identifier(aname)));
                }
            }
            // Ongoing state activities
        } else if (act->tag == "doActivity" || ends_with(act->tag, ":doActivity")) {
            std::string aname = act->get_attr("name");
            if (!aname.empty()) {
                if (auto* s = model.find_state_mut(state_name)) {
                    s->do_activity = sanitize_identifier(aname);
                }
            }
            // Deferred event triggers
        } else if (act->tag == "deferrableTrigger" || ends_with(act->tag, ":deferrableTrigger")) {
            std::string def_name = act->get_attr("name");
            std::string trig_id = act->get_attr("xmi:idref");
            if (trig_id.empty())
                trig_id = act->get_attr("idref");
            if (trig_id.empty())
                trig_id = act->get_attr("event");
            if (!trig_id.empty()) {
                auto it = id_to_name.find(trig_id);
                if (it != id_to_name.end()) {
                    def_name = it->second;
                }
            }
            if (def_name.empty()) {
                for (const auto& trig : act->find_children("trigger")) {
                    def_name = trig->get_attr("name");
                    if (def_name.empty()) {
                        std::string ref = trig->get_attr("xmi:idref");
                        if (ref.empty())
                            ref = trig->get_attr("idref");
                        if (ref.empty())
                            ref = trig->get_attr("event");
                        if (!ref.empty()) {
                            auto it = id_to_name.find(ref);
                            if (it != id_to_name.end()) {
                                def_name = it->second;
                            }
                        }
                    }
                    if (!def_name.empty()) {
                        break;
                    }
                }
            }
            if (!def_name.empty()) {
                std::string clean_name = sanitize_identifier(def_name);
                if (auto* s = model.find_state_mut(state_name)) {
                    s->deferred_events.push_back(clean_name);
                }
                model.add_event(clean_name);
            }
        }
    }

    // 8. Recursively process nested composite regions and regional transitions
    for (const auto& sub : vertex_node->children) {
        if (sub->tag == "region" || ends_with(sub->tag, ":region")) {
            if (auto* s = model.find_state_mut(state_name)) {
                s->is_composite = true;
            }
            process_region(sub, model, state_name, id_map, id_to_name, id_to_kind);
            process_transitions_in_region(sub, model, state_name, id_map, id_to_name, id_to_kind);
        }
    }
}

void CameoXmiGraphResolver::process_transitions_in_region(
    const std::shared_ptr<XmlNode>& region_node, FsmIr& model, const std::string& parent_state,
    const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
    const std::unordered_map<std::string, std::string>& id_to_name,
    const std::unordered_map<std::string, StateKind>& id_to_kind) {
    for (const auto& child : region_node->children) {
        if (child->tag == "transition" || ends_with(child->tag, ":transition")) {
            process_transition(child, model, parent_state, id_map, id_to_name, id_to_kind);
        }
    }
}

void CameoXmiGraphResolver::process_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                               const std::string& parent_state,
                                               const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& id_map,
                                               const std::unordered_map<std::string, std::string>& id_to_name,
                                               const std::unordered_map<std::string, StateKind>& id_to_kind) {
    std::string src_id = trans_node->get_attr("source");
    std::string dst_id = trans_node->get_attr("target");

    auto src_it = id_to_name.find(src_id);
    auto dst_it = id_to_name.find(dst_id);

    std::string src_name = (src_it != id_to_name.end()) ? src_it->second : sanitize_identifier(src_id);
    std::string dst_name = (dst_it != id_to_name.end()) ? dst_it->second : sanitize_identifier(dst_id);

    auto kind_it = id_to_kind.find(src_id);
    bool src_is_init = (kind_it != id_to_kind.end() && kind_it->second == StateKind::Initial);

    // Initial transition
    if (src_is_init || src_name == "Initial" || src_name == "[*]") {
        if (parent_state.empty()) {
            model.initial_state = dst_name;
        } else {
            if (auto* p = model.find_state_mut(parent_state)) {
                p->initial_sub_state = dst_name;
            }
        }
        return;
    }

    if (src_name.empty() || dst_name.empty()) {
        return;
    }

    // Resolve Event / Trigger
    std::string event_name = trans_node->get_attr("trigger");
    if (event_name.empty()) {
        event_name = trans_node->get_attr("event");
    }

    // Inspect child triggers or resolve cross-referenced triggers
    for (const auto& trig : trans_node->find_children("trigger")) {
        std::string tname = trig->get_attr("name");
        std::string idref = trig->get_attr("xmi:idref");
        if (idref.empty()) {
            idref = trig->get_attr("idref");
        }
        if (!idref.empty()) {
            auto trig_it = id_map.find(idref);
            if (trig_it != id_map.end()) {
                if (tname.empty()) {
                    tname = trig_it->second->get_attr("name");
                }
                std::string sig_ref = trig_it->second->get_attr("signal");
                if (tname.empty() && !sig_ref.empty()) {
                    auto sig_it = id_map.find(sig_ref);
                    if (sig_it != id_map.end()) {
                        tname = sig_it->second->get_attr("name");
                    }
                }
            }
        }
        if (!tname.empty()) {
            event_name = tname;
            break;
        }
    }

    // Guard condition
    std::string guard_expr = trans_node->get_attr("guard");
    if (guard_expr.empty()) {
        for (const auto& g : trans_node->find_children("guard")) {
            guard_expr = g->get_attr("name");
            if (guard_expr.empty()) {
                for (const auto& spec : g->children) {
                    guard_expr = spec->get_attr("body");
                    if (guard_expr.empty()) {
                        guard_expr = spec->text_content;
                    }
                }
            }
        }
    }

    // Action effect
    std::string action_name = trans_node->get_attr("effect");
    if (action_name.empty()) {
        action_name = trans_node->get_attr("action");
    }
    if (action_name.empty()) {
        for (const auto& eff : trans_node->find_children("effect")) {
            action_name = eff->get_attr("name");
            if (action_name.empty()) {
                action_name = eff->text_content;
            }
        }
    }

    TransitionEdge trans;
    trans.source = src_name;
    trans.target = dst_name;
    trans.event = sanitize_identifier(event_name);
    if (!event_name.empty()) {
        SignalDefinition sig;
        sig.name = trans.event;
        model.add_signal(std::move(sig));
    }

    if (!guard_expr.empty()) {
        auto parsed = GuardExpressionParser::parse(guard_expr);
        if (!parsed.cpp_type.empty()) {
            trans.guard = parsed.cpp_type;
            for (const auto& a : parsed.atomic_guards) {
                model.add_guard(a);
            }
        } else {
            trans.guard = sanitize_identifier(guard_expr);
            model.add_guard(trans.guard.value());
        }
    }

    if (!action_name.empty()) {
        trans.transition_action = ActionSignature(sanitize_identifier(action_name));
        model.add_action(trans.transition_action->name);
    }

    model.add_transition(std::move(trans));
}

void CameoXmiGraphResolver::relink_stereotypes_and_profiles(
    const std::shared_ptr<XmlNode>& root, FsmIr& model,
    const std::unordered_map<std::string, std::shared_ptr<XmlNode>>& /*id_map*/,
    const std::unordered_map<std::string, std::string>& id_to_name) {
    // Look for root children or siblings of Model that apply stereotypes via base_Element / base_NamedElement
    for (const auto& child : root->children) {
        std::string base_id = child->get_attr("base_Element");
        if (base_id.empty()) {
            base_id = child->get_attr("base_NamedElement");
        }
        if (base_id.empty()) {
            base_id = child->get_attr("base_State");
        }
        if (base_id.empty()) {
            base_id = child->get_attr("base_Transition");
        }

        if (base_id.empty()) {
            continue;
        }

        auto it = id_to_name.find(base_id);
        if (it == id_to_name.end()) {
            continue;
        }
        const std::string& target_state_name = it->second;

        // Check if this is a SysML Requirement
        if (child->tag.find("Requirement") != std::string::npos || ends_with(child->tag, ":Requirement")) {
            std::string req_id = child->get_attr("id");
            if (req_id.empty()) {
                req_id = child->get_attr("name");
            }
            if (!req_id.empty()) {
                if (auto* st = model.find_state_mut(target_state_name)) {
                    st->traceability_reqs.push_back(req_id);
                }
                model.satisfies_reqs.push_back(req_id);
            }
        }
    }
}

bool CameoXmiGraphResolver::resolve(const std::shared_ptr<XmlNode>& root, FsmIr& model, std::string& err) {
    if (!root) {
        err = "Cameo Graph Resolver: Null XML root.";
        return false;
    }

    // Pass 1: Build Flat UUID Map & filter graphic layout
    std::unordered_map<std::string, std::shared_ptr<XmlNode>> id_map;
    build_id_map(root, id_map);

    // Locate StateMachine node
    auto sm_node = find_state_machine(root, id_map);
    if (!sm_node) {
        err = "Cameo Graph Resolver: No uml:StateMachine found in XMI.";
        return false;
    }

    std::string sm_name = sm_node->get_attr("name");
    model.name = sm_name.empty() ? "CameoStateMachine" : sanitize_identifier(sm_name);

    // Pass 2: Extract states, regions, and transitions
    std::unordered_map<std::string, std::string> id_to_name;
    std::unordered_map<std::string, StateKind> id_to_kind;

    // Process top-level regions or direct subvertices
    for (const auto& child : sm_node->children) {
        if (child->tag == "region" || ends_with(child->tag, ":region")) {
            process_region(child, model, "", id_map, id_to_name, id_to_kind);
        } else if (child->tag == "subvertex" || ends_with(child->tag, ":subvertex")) {
            process_subvertex(child, model, "", id_map, id_to_name, id_to_kind);
        }
    }

    // Process transitions
    for (const auto& child : sm_node->children) {
        if (child->tag == "region" || ends_with(child->tag, ":region")) {
            process_transitions_in_region(child, model, "", id_map, id_to_name, id_to_kind);
        } else if (child->tag == "transition" || ends_with(child->tag, ":transition")) {
            process_transition(child, model, "", id_map, id_to_name, id_to_kind);
        }
    }

    // Pass 2b: Stereotype & Profile Relinking (base_Element)
    relink_stereotypes_and_profiles(root, model, id_map, id_to_name);

    if (model.states.empty()) {
        err = "Cameo Graph Resolver: No states extracted from StateMachine.";
        return false;
    }

    if (model.initial_state.empty() && !model.states.empty()) {
        model.initial_state = model.states.front().name;
    }

    return true;
}

}  // namespace fsm::frontend::formal
