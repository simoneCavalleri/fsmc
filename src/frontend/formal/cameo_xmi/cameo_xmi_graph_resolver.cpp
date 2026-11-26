#include "cameo_xmi_graph_resolver.hpp"

#include <cctype>
#include <regex>
#include <sstream>
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

static std::optional<ActionSignature> try_build_cameo_assignment_action(std::string_view raw_action,
                                                                        const std::string& fallback_prefix) {
    static const std::regex assign_regex(
        R"(^((?:(?:out|in|reg|local)\.)?)?(?:(?:service|context)\.)?([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(=|\+=|-=|\*=|\/=)\s*(.+)$)",
        std::regex::optimize);
    static const std::regex inc_regex(
        R"(^((?:(?:out|in|reg|local)\.)?)?(?:(?:service|context)\.)?([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(\+\+|--)$)",
        std::regex::optimize);

    std::vector<ActionAssignment> assignments;
    std::string raw_str(raw_action);
    std::stringstream ss(raw_str);
    std::string stmt;
    while (std::getline(ss, stmt, ';')) {
        size_t first = stmt.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        size_t last = stmt.find_last_not_of(" \t\r\n");
        std::string trimmed = stmt.substr(first, last - first + 1);
        std::smatch m;
        if (std::regex_match(trimmed, m, assign_regex)) {
            std::string scope_prefix = m[1].str();
            std::string var_path = m[2].str();
            std::string op = m[3].str();
            std::string rhs = m[4].str();
            size_t rfirst = rhs.find_first_not_of(" \t\r\n");
            size_t rlast = rhs.find_last_not_of(" \t\r\n");
            if (rfirst != std::string::npos) {
                rhs = rhs.substr(rfirst, rlast - rfirst + 1);
            }
            LValueTarget tgt(scope_prefix + var_path);
            if (op == "=") {
                assignments.push_back({tgt, rhs});
            } else if (op == "+=") {
                assignments.push_back({tgt, var_path + " + " + rhs});
            } else if (op == "-=") {
                assignments.push_back({tgt, var_path + " - " + rhs});
            } else if (op == "*=") {
                assignments.push_back({tgt, var_path + " * (" + rhs + ")"});
            } else if (op == "/=") {
                assignments.push_back({tgt, var_path + " / (" + rhs + ")"});
            }
        } else if (std::regex_match(trimmed, m, inc_regex)) {
            std::string scope_prefix = m[1].str();
            std::string var_path = m[2].str();
            std::string op = m[3].str();
            LValueTarget tgt(scope_prefix + var_path);
            assignments.push_back({tgt, op == "++" ? (var_path + " + 1") : (var_path + " - 1")});
        }
    }

    if (assignments.empty()) {
        return std::nullopt;
    }
    std::string target_id = assignments.front().target.name;
    for (char& ch : target_id) {
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
            ch = '_';
        }
    }
    ActionSignature sig(fallback_prefix + "_" + target_id);
    sig.assignments = std::move(assignments);
    return sig;
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

    // 2. Classify UML pseudostates (Initial, Choice, Junction, ShallowHistory, DeepHistory, Fork, Join, EntryPoint,
    // ExitPoint)
    if (type == "uml:FinalState" || ends_with(type, ":FinalState")) {
        st_kind = StateKind::Final;
    } else if (type == "uml:Pseudostate" || ends_with(type, ":Pseudostate") || kind == "initial" || kind == "choice" ||
               kind == "junction" || kind == "shallowHistory" || kind == "deepHistory" || kind == "fork" ||
               kind == "join" || kind == "entryPoint" || kind == "exitPoint") {
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
        } else if (kind == "fork") {
            st_kind = StateKind::Fork;
        } else if (kind == "join") {
            st_kind = StateKind::Join;
        } else if (kind == "entryPoint") {
            st_kind = StateKind::EntryPoint;
        } else if (kind == "exitPoint") {
            st_kind = StateKind::ExitPoint;
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
            if (!id.empty()) {
                id_to_name[id] = parent_state;
            }
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
    if (st_kind == StateKind::Choice) {
        model.add_choice_node(state_name);
    } else if (st_kind != StateKind::Initial && st_kind != StateKind::Junction) {
        auto& st = model.add_state(state_name, parent_state, st_kind);
        std::string req = vertex_node->get_attr("satisfies");
        if (req.empty()) {
            for (const auto& child : vertex_node->children) {
                if (child->tag == "xmi:Extension" || ends_with(child->tag, ":Extension")) {
                    for (const auto& ext_child : child->children) {
                        if (ext_child->tag == "satisfies" || ends_with(ext_child->tag, ":satisfies")) {
                            req = ext_child->get_attr("value");
                            if (req.empty()) {
                                req = ext_child->text_content;
                            }
                        }
                    }
                }
            }
        }
        if (!req.empty()) {
            std::istringstream rss(req);
            std::string item;
            while (std::getline(rss, item, ',')) {
                auto f = item.find_first_not_of(" \t\r\n");
                auto l = item.find_last_not_of(" \t\r\n");
                std::string trimmed_req = (f != std::string::npos) ? item.substr(f, l - f + 1) : "";
                if (!trimmed_req.empty() && std::find(st.traceability_reqs.begin(), st.traceability_reqs.end(),
                                                      trimmed_req) == st.traceability_reqs.end()) {
                    st.traceability_reqs.push_back(std::move(trimmed_req));
                }
            }
        }
        std::string inv = vertex_node->get_attr("invariant");
        if (inv.empty()) {
            for (const auto& inv_node : vertex_node->find_children("stateInvariant")) {
                inv = inv_node->get_attr("body");
                if (inv.empty()) {
                    for (const auto& spec : inv_node->children) {
                        inv = spec->get_attr("body");
                        if (inv.empty()) {
                            for (const auto& b : spec->find_children("body")) {
                                if (!b->text_content.empty()) {
                                    inv = b->text_content;
                                    break;
                                }
                            }
                        }
                        if (inv.empty()) {
                            inv = spec->text_content;
                        }
                    }
                }
            }
        }
        if (!inv.empty()) {
            st.time_invariant = inv;
        }
    }

    auto extract_behavior_text = [](const std::shared_ptr<XmlNode>& node) -> std::string {
        std::string txt = node->get_attr("body");
        if (txt.empty()) {
            for (const auto& body_node : node->find_children("body")) {
                if (!body_node->text_content.empty()) {
                    txt = body_node->text_content;
                    break;
                }
            }
        }
        if (txt.empty()) {
            txt = node->get_attr("name");
        }
        if (txt.empty()) {
            txt = node->text_content;
        }
        return txt;
    };

    // 7. Parse entry, exit, doActivity, and deferrableTrigger actions
    for (const auto& act : vertex_node->children) {
        // Entry actions
        if (act->tag == "entry" || ends_with(act->tag, ":entry")) {
            std::string aname = extract_behavior_text(act);
            if (!aname.empty()) {
                auto assign_sig = try_build_cameo_assignment_action(aname, state_name + "_entry");
                ActionSignature sig =
                    assign_sig.has_value() ? std::move(*assign_sig) : ActionSignature(sanitize_identifier(aname));
                model.add_action(sig.name);
                if (auto* s = model.find_state_mut(state_name)) {
                    s->entry_actions.push_back(std::move(sig));
                }
            }
            // Exit actions
        } else if (act->tag == "exit" || ends_with(act->tag, ":exit")) {
            std::string aname = extract_behavior_text(act);
            if (!aname.empty()) {
                auto assign_sig = try_build_cameo_assignment_action(aname, state_name + "_exit");
                ActionSignature sig =
                    assign_sig.has_value() ? std::move(*assign_sig) : ActionSignature(sanitize_identifier(aname));
                model.add_action(sig.name);
                if (auto* s = model.find_state_mut(state_name)) {
                    s->exit_actions.push_back(std::move(sig));
                }
            }
            // Ongoing state activities
        } else if (act->tag == "doActivity" || ends_with(act->tag, ":doActivity")) {
            std::string aname = extract_behavior_text(act);
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

    // 8. Recursively process nested composite / parallel regions and regional transitions
    std::vector<std::shared_ptr<XmlNode>> region_nodes;
    for (const auto& sub : vertex_node->children) {
        if (sub->tag == "region" || ends_with(sub->tag, ":region")) {
            region_nodes.push_back(sub);
        }
    }
    if (region_nodes.size() > 1) {
        if (auto* s = model.find_state_mut(state_name)) {
            s->is_composite = true;
            s->kind = StateKind::Parallel;
        }
        for (size_t idx = 0; idx < region_nodes.size(); ++idx) {
            const auto& sub = region_nodes[idx];
            std::string reg_name = sanitize_identifier(sub->get_attr("name"));
            if (reg_name.empty()) {
                reg_name = state_name + "_Region" + std::to_string(idx + 1);
            }
            model.add_state(reg_name, state_name);
            if (auto* reg_state = model.find_state_mut(reg_name)) {
                reg_state->is_composite = true;
            }
            process_region(sub, model, reg_name, id_map, id_to_name, id_to_kind);

            OrthogonalRegion ortho_reg;
            ortho_reg.id = reg_name;
            ortho_reg.name = reg_name;
            for (const auto& st : model.states) {
                if (st.parent_state == reg_name) {
                    ortho_reg.state_ids.push_back(st.name);
                }
            }
            if (const auto* reg_state = model.find_state(reg_name)) {
                ortho_reg.initial_state_id =
                    reg_state->initial_sub_state.empty() ? reg_name : reg_state->initial_sub_state;
            } else {
                ortho_reg.initial_state_id = reg_name;
            }
            if (auto* parent = model.find_state_mut(state_name)) {
                parent->orthogonal_regions.push_back(std::move(ortho_reg));
            }
        }
    } else {
        for (const auto& sub : region_nodes) {
            if (auto* s = model.find_state_mut(state_name)) {
                s->is_composite = true;
            }
            process_region(sub, model, state_name, id_map, id_to_name, id_to_kind);
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

    for (const auto& child : region_node->children) {
        if (child->tag != "subvertex" && !ends_with(child->tag, ":subvertex")) {
            continue;
        }
        std::string sub_id = child->get_attr("xmi:id");
        if (sub_id.empty()) {
            sub_id = child->get_attr("id");
        }
        std::string sub_state_name;
        if (auto it = id_to_name.find(sub_id); it != id_to_name.end()) {
            sub_state_name = it->second;
        } else {
            sub_state_name = sanitize_identifier(child->get_attr("name"));
        }
        if (sub_state_name.empty()) {
            continue;
        }

        std::vector<std::shared_ptr<XmlNode>> sub_regions;
        for (const auto& sub_child : child->children) {
            if (sub_child->tag == "transition" || ends_with(sub_child->tag, ":transition")) {
                process_transition(sub_child, model, sub_state_name, id_map, id_to_name, id_to_kind);
            } else if (sub_child->tag == "region" || ends_with(sub_child->tag, ":region")) {
                sub_regions.push_back(sub_child);
            }
        }
        if (sub_regions.size() > 1) {
            for (size_t idx = 0; idx < sub_regions.size(); ++idx) {
                const auto& sub = sub_regions[idx];
                std::string reg_name = sanitize_identifier(sub->get_attr("name"));
                if (reg_name.empty()) {
                    reg_name = sub_state_name + "_Region" + std::to_string(idx + 1);
                }
                process_transitions_in_region(sub, model, reg_name, id_map, id_to_name, id_to_kind);

                // Refresh orthogonal region initial_state_id after initial transitions are processed
                if (const auto* reg_state = model.find_state(reg_name);
                    reg_state != nullptr && !reg_state->initial_sub_state.empty()) {
                    if (auto* parent = model.find_state_mut(sub_state_name);
                        parent != nullptr && idx < parent->orthogonal_regions.size()) {
                        parent->orthogonal_regions[idx].initial_state_id = reg_state->initial_sub_state;
                    }
                }
            }
        } else if (sub_regions.size() == 1) {
            process_transitions_in_region(sub_regions[0], model, sub_state_name, id_map, id_to_name, id_to_kind);
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
                        for (const auto& body_node : spec->find_children("body")) {
                            if (!body_node->text_content.empty()) {
                                guard_expr = body_node->text_content;
                                break;
                            }
                        }
                    }
                    if (guard_expr.empty()) {
                        guard_expr = spec->text_content;
                    }
                }
            }
        }
    }

    // Priority (MOF <xmi:Extension> or legacy priority attribute)
    std::uint32_t priority_val = 0;
    std::string prio_str = trans_node->get_attr("priority");
    if (prio_str.empty()) {
        for (const auto& child : trans_node->children) {
            if (child->tag == "xmi:Extension" || ends_with(child->tag, ":Extension")) {
                for (const auto& ext_child : child->children) {
                    if (ext_child->tag == "priority" || ends_with(ext_child->tag, ":priority")) {
                        prio_str = ext_child->get_attr("value");
                        if (prio_str.empty()) {
                            prio_str = ext_child->text_content;
                        }
                    }
                }
            }
        }
    }
    if (!prio_str.empty()) {
        try {
            priority_val = static_cast<std::uint32_t>(std::stoul(prio_str));
        } catch (...) {
        }
    }

    // Action effect
    std::string action_name = trans_node->get_attr("effect");
    if (action_name.empty()) {
        action_name = trans_node->get_attr("action");
    }
    if (action_name.empty()) {
        for (const auto& eff : trans_node->find_children("effect")) {
            action_name = eff->get_attr("body");
            if (action_name.empty()) {
                for (const auto& body_node : eff->find_children("body")) {
                    if (!body_node->text_content.empty()) {
                        action_name = body_node->text_content;
                        break;
                    }
                }
            }
            if (action_name.empty()) {
                action_name = eff->get_attr("name");
            }
            if (action_name.empty()) {
                action_name = eff->text_content;
            }
        }
    }

    TransitionEdge trans;
    trans.source = src_name;
    trans.target = dst_name;
    trans.priority = priority_val;
    std::string trans_kind = trans_node->get_attr("kind");
    if (trans_kind == "internal") {
        trans.kind = TransitionEdgeKind::Internal;
    } else if (trans_kind == "local") {
        trans.kind = TransitionEdgeKind::Local;
    }
    auto dst_kind_it = id_to_kind.find(dst_id);
    if (dst_kind_it != id_to_kind.end() &&
        (dst_kind_it->second == StateKind::ShallowHistory || dst_kind_it->second == StateKind::DeepHistory)) {
        trans.target_is_history = true;
        trans.target_is_deep_history = (dst_kind_it->second == StateKind::DeepHistory);
    }
    static const std::regex when_ev_regex(R"(^\s*when\s*\((.+)\)\s*$)", std::regex::icase);
    std::smatch wm;
    if (!event_name.empty() && std::regex_match(event_name, wm, when_ev_regex)) {
        std::string expr = wm[1].str();
        auto f = expr.find_first_not_of(" \t\r\n");
        auto l = expr.find_last_not_of(" \t\r\n");
        expr = (f != std::string::npos) ? expr.substr(f, l - f + 1) : "";
        trans.trigger = ChangeTrigger(expr, true);
    } else {
        trans.event = sanitize_identifier(event_name);
    }
    if (!trans.event.empty()) {
        static const std::regex time_ev_regex(R"(^(after|every)_([0-9]+)(ms|us|s|m)$)", std::regex::icase);
        std::smatch tm;
        if (std::regex_match(trans.event, tm, time_ev_regex)) {
            std::string kw = tm[1].str();
            std::transform(kw.begin(), kw.end(), kw.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const TimeTriggerKind tkind = (kw == "every") ? TimeTriggerKind::Every : TimeTriggerKind::After;
            const std::uint64_t val = static_cast<std::uint64_t>(std::stoull(tm[2].str()));
            std::string u = tm[3].str();
            std::transform(u.begin(), u.end(), u.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (u == "us") {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Microseconds);
            } else if (u == "s") {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Seconds);
            } else if (u == "m") {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Minutes);
            } else {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Milliseconds);
            }
        }
    }
    if (!trans.event.empty()) {
        SignalDefinition sig;
        sig.name = trans.event;
        model.add_signal(std::move(sig));
    }

    if (!guard_expr.empty()) {
        auto parsed = GuardExpressionParser::parse(guard_expr);
        if (!parsed.canonical_expression.empty()) {
            trans.guard = parsed.canonical_expression;
            for (const auto& detail : parsed.atomic_guard_details) {
                model.add_guard(detail.name, "", detail.expression, detail.expression);
            }
            for (const auto& a : parsed.atomic_guards) {
                model.add_guard(a);
            }
        } else {
            // Generate a stable synthetic guard name instead of sanitizing the raw boolean
            // expression (which would corrupt operator characters like >, &&, ! etc.).
            std::string guard_name = "guard_" + sanitize_identifier(src_name) + "_to_" + sanitize_identifier(dst_name) +
                                     "_" + std::to_string(model.transitions.size() + 1);
            model.add_guard(guard_name, "", std::optional<std::string>{guard_expr},
                            std::optional<std::string>{guard_expr});
            trans.guard = guard_name;
        }
    }

    if (!action_name.empty()) {
        std::string prefix = sanitize_identifier(src_name) + "_" + sanitize_identifier(dst_name) + "_act";
        auto assign_sig = try_build_cameo_assignment_action(action_name, prefix);
        if (assign_sig.has_value()) {
            const std::string base = assign_sig->name;
            std::size_t suffix = 2;
            auto has_action = [&](const std::string& n) {
                return std::any_of(model.actions.begin(), model.actions.end(),
                                   [&](const auto& a) { return a.name == n; });
            };
            while (has_action(assign_sig->name)) {
                assign_sig->name = base + "_" + std::to_string(suffix++);
                assign_sig->invocation = assign_sig->name;
            }
        }
        trans.transition_action =
            assign_sig.has_value() ? std::move(*assign_sig) : ActionSignature(sanitize_identifier(action_name));
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
