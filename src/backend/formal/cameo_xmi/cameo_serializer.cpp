#include "fsm/backend/formal/cameo_serializer.hpp"

#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "fsm/ir/guard.hpp"
#include "fsm/ir/type_definition.hpp"

namespace fsm::backend::formal {

using namespace fsm::ir;
using ir::GuardExpressionParser;

static std::string escape_xml(const std::string& input) {
    std::string res;
    res.reserve(input.size() * 2);
    for (char ch : input) {
        switch (ch) {
            case '&':
                res += "&amp;";
                break;
            case '<':
                res += "&lt;";
                break;
            case '>':
                res += "&gt;";
                break;
            case '"':
                res += "&quot;";
                break;
            case '\'':
                res += "&apos;";
                break;
            default:
                res += ch;
                break;
        }
    }
    return res;
}

std::string CameoSerializer::serialize(const FsmIr& model) {
    std::ostringstream out;
    std::string model_name = model.name.empty() ? "GeneratedFSM" : model.name;

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<xmi:XMI xmi:version=\"2.5.1\" xmlns:uml=\"http://www.omg.org/spec/UML/20131001\" "
           "xmlns:xmi=\"http://www.omg.org/spec/XMI/20131001\">\n";
    out << "  <uml:Model xmi:id=\"_m1\" name=\"" << escape_xml(model_name) << "Model\">\n";
    // Emit declared typed signals as formal directives
    for (const auto& sig : model.signals) {
        if (!sig.attributes.empty() || !sig.validators.empty()) {
            out << "    <!-- @fsm:signal " << sig.name;
            if (!sig.attributes.empty()) {
                out << "{";
                for (size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].type << " " << sig.attributes[i].name;
                }
                out << "}";
            }
            if (!sig.validators.empty()) {
                out << " validator=\"" << sig.validators.front() << "\"";
            }
            out << " -->\n";
        }
    }
    // Emit declared properties
    for (const auto& prop : model.properties) {
        out << "    <!-- @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind)
            << " ltl=\"" << prop.raw_formula << "\"";
        if (!prop.traceability_req.empty()) {
            out << " req=\"" << prop.traceability_req << "\"";
        }
        if (!prop.description.empty()) {
            out << " desc=\"" << prop.description << "\"";
        }
        out << " -->\n";
    }
    // Emit declared enums & structs as formal directives
    for (const auto& ct : model.custom_types) {
        if (ct.is_enum()) {
            out << "    <!-- @fsm:enum " << ir::format_enum_directive(ct) << " -->\n";
        } else if (ct.is_struct()) {
            out << "    <!-- @fsm:struct " << ir::format_struct_directive(ct) << " -->\n";
        }
    }
    for (const auto& port : model.ports) {
        out << "    <!-- @fsm:port name=" << port.name << " type=" << port.type
            << " dir=" << port_direction_to_string(port.direction);
        if (port.min_value.has_value()) {
            out << " min=" << *port.min_value;
        }
        if (port.max_value.has_value()) {
            out << " max=" << *port.max_value;
        }
        out << " -->\n";
    }
    for (const auto& var : model.variables) {
        out << "    <!-- @fsm:var name=" << var.name << " type=" << var.type;
        if (var.physical_unit.has_value()) {
            out << " unit=\"" << *var.physical_unit << "\"";
        }
        if (!var.initial_value.empty()) {
            out << " init=" << var.initial_value;
        }
        if (var.min_value.has_value()) {
            out << " min=" << *var.min_value;
        }
        if (var.max_value.has_value()) {
            out << " max=" << *var.max_value;
        }
        if (!var.description.empty()) {
            out << " desc=\"" << var.description << "\"";
        }
        out << " -->\n";
    }
    out << "    <packagedElement xmi:type=\"uml:StateMachine\" xmi:id=\"_sm1\" name=\"" << escape_xml(model_name)
        << "\">\n";
    out << "      <region xmi:type=\"uml:Region\" xmi:id=\"_r1\">\n";

    std::map<std::string, std::string> id_map;
    size_t id_counter = 1;
    for (const auto& s : model.states) {
        id_map[s.name] = "_s" + std::to_string(id_counter++);
    }
    for (const auto& c : model.choice_nodes) {
        id_map[c.name] = "_ps_choice_" + std::to_string(id_counter++);
    }

    // Map parent states
    std::map<std::string, std::string> parent_map;
    for (const auto& s : model.states) {
        parent_map[s.name] = s.parent_state;
    }

    // Root initial pseudostate
    out << "        <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"_ps_root\" kind=\"initial\"/>\n";

    // Emit choice pseudostates
    for (const auto& choice : model.choice_nodes) {
        out << "        <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << id_map[choice.name] << "\" name=\""
            << escape_xml(choice.name) << "\" kind=\"choice\"/>\n";
    }

    std::set<size_t> emitted_transitions;

    auto resolve_guard_str = [&](const std::string& guard_val) -> std::string {
        return GuardExpressionParser::to_diagram_string(guard_val, model.guards);
    };

    auto format_action_for_xmi = [](const ActionSignature& act) -> std::string {
        if (!act.assignments.empty()) {
            std::string expr;
            for (std::size_t i = 0; i < act.assignments.size(); ++i) {
                if (i > 0) {
                    expr += "; ";
                }
                const auto& a = act.assignments[i];
                expr += a.target.full_path() + " " + std::string(assignment_op_to_string(a.op)) + " " + a.expression;
            }
            return expr;
        }
        return act.name;
    };

    // Recursive state emission
    std::function<void(const StateNode&, size_t)> emit_state_node = [&](const StateNode& state, size_t indent) {
        std::string pad(indent * 2, ' ');
        std::string st_id = id_map[state.name];

        if (state.kind == StateKind::EntryPoint) {
            out << pad << "<subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << st_id << "\" name=\""
                << escape_xml(state.name) << "\" kind=\"entryPoint\"/>\n";
            return;
        }
        if (state.kind == StateKind::ExitPoint) {
            out << pad << "<subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << st_id << "\" name=\""
                << escape_xml(state.name) << "\" kind=\"exitPoint\"/>\n";
            return;
        }
        if (state.kind == StateKind::Fork) {
            out << pad << "<subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << st_id << "\" name=\""
                << escape_xml(state.name) << "\" kind=\"fork\"/>\n";
            return;
        }
        if (state.kind == StateKind::Join) {
            out << pad << "<subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << st_id << "\" name=\""
                << escape_xml(state.name) << "\" kind=\"join\"/>\n";
            return;
        }

        const std::string vertex_type = (state.kind == StateKind::Final) ? "uml:FinalState" : "uml:State";
        out << pad << "<subvertex xmi:type=\"" << vertex_type << "\" xmi:id=\"" << st_id << "\" name=\""
            << escape_xml(state.name) << "\">\n";

        if (!state.traceability_reqs.empty()) {
            out << pad << "  <xmi:Extension extender=\"fsmc\">\n";
            out << pad << "    <satisfies value=\"";
            for (size_t r = 0; r < state.traceability_reqs.size(); ++r) {
                if (r > 0)
                    out << ", ";
                out << escape_xml(state.traceability_reqs[r]);
            }
            out << "\"/>\n";
            out << pad << "  </xmi:Extension>\n";
        }
        if (state.time_invariant.has_value() && !state.time_invariant->empty() && state.kind != StateKind::Final) {
            out << pad << "  <stateInvariant xmi:type=\"uml:Constraint\" xmi:id=\"_inv" << st_id << "\">\n";
            out << pad << "    <specification xmi:type=\"uml:OpaqueExpression\" xmi:id=\"_invspec" << st_id << "\">\n";
            out << pad << "      <body>" << escape_xml(*state.time_invariant) << "</body>\n";
            out << pad << "    </specification>\n";
            out << pad << "  </stateInvariant>\n";
        }

        for (const auto& act : state.entry_actions) {
            out << pad << "  <entry xmi:type=\"uml:OpaqueBehavior\" xmi:id=\"_ent" << st_id << "_" << (id_counter++)
                << "\">\n";
            out << pad << "    <body>" << escape_xml(format_action_for_xmi(act)) << "</body>\n";
            out << pad << "  </entry>\n";
        }
        if (state.do_activity.has_value() && !state.do_activity->empty() && state.kind != StateKind::Final) {
            out << pad << "  <doActivity xmi:type=\"uml:OpaqueBehavior\" xmi:id=\"_do" << st_id << "\" name=\""
                << escape_xml(*state.do_activity) << "\"/>\n";
        }
        for (const auto& act : state.exit_actions) {
            out << pad << "  <exit xmi:type=\"uml:OpaqueBehavior\" xmi:id=\"_ext" << st_id << "_" << (id_counter++)
                << "\">\n";
            out << pad << "    <body>" << escape_xml(format_action_for_xmi(act)) << "</body>\n";
            out << pad << "  </exit>\n";
        }
        for (const auto& d_evt : state.deferred_events) {
            out << pad << "  <deferrableTrigger xmi:type=\"uml:Trigger\" xmi:id=\"_dt" << st_id << "_" << (id_counter++)
                << "\" name=\"" << escape_xml(d_evt) << "\"/>\n";
        }

        auto emit_region_contents = [&](const std::string& scope_name, const std::string& scope_id,
                                        const StateNode& scope_state, size_t region_indent,
                                        const std::string& region_name_attr) {
            std::string rpad(region_indent * 2, ' ');
            out << rpad << "<region xmi:type=\"uml:Region\" xmi:id=\"_r_" << scope_id << "\"";
            if (!region_name_attr.empty()) {
                out << " name=\"" << escape_xml(region_name_attr) << "\"";
            }
            out << ">\n";

            if (!scope_state.initial_sub_state.empty() && id_map.count(scope_state.initial_sub_state)) {
                std::string init_ps = "_ps_init_" + scope_id;
                out << rpad << "  <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << init_ps
                    << "\" kind=\"initial\"/>\n";
                out << rpad << "  <transition xmi:type=\"uml:Transition\" xmi:id=\"_t_init_" << scope_id
                    << "\" source=\"" << init_ps << "\" target=\"" << id_map[scope_state.initial_sub_state] << "\"/>\n";
            }

            bool has_shallow_hist = scope_state.has_history && !scope_state.has_deep_history;
            bool has_deep_hist = scope_state.has_deep_history;
            for (const auto& t : model.transitions) {
                if (t.target == scope_name) {
                    if (t.target_is_deep_history) {
                        has_deep_hist = true;
                    } else if (t.target_is_history) {
                        has_shallow_hist = true;
                    }
                }
            }
            if (has_shallow_hist) {
                out << rpad << "  <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"_ps_hist_" << scope_id
                    << "\" kind=\"shallowHistory\"/>\n";
            }
            if (has_deep_hist) {
                out << rpad << "  <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"_ps_dhist_" << scope_id
                    << "\" kind=\"deepHistory\"/>\n";
            }

            // Child states
            for (const auto& child : model.states) {
                if (child.parent_state == scope_name) {
                    emit_state_node(child, region_indent + 1);
                }
            }

            auto emit_uml_transition = [&](size_t idx, const TransitionEdge& trans, const std::string& tpad) {
                std::string src_id = id_map.count(trans.source) ? id_map[trans.source] : trans.source;
                std::string dst_id = id_map.count(trans.target) ? id_map[trans.target] : trans.target;
                if (const auto* tgt_st = model.find_state(trans.target);
                    tgt_st != nullptr && tgt_st->is_composite && id_map.count(trans.target)) {
                    if (trans.target_is_deep_history) {
                        dst_id = "_ps_dhist_" + id_map[trans.target];
                    } else if (trans.target_is_history) {
                        dst_id = "_ps_hist_" + id_map[trans.target];
                    }
                }
                std::string trig_str;
                if (const auto* ct = std::get_if<ChangeTrigger>(&trans.trigger);
                    ct != nullptr && !ct->raw_expression.empty()) {
                    trig_str = "when(" + ct->raw_expression + ")";
                } else if (const auto* tt = std::get_if<TimeTrigger>(&trans.trigger);
                           tt != nullptr && trans.event.empty()) {
                    trig_str = std::string(tt->kind == TimeTriggerKind::Every ? "every_" : "after_") +
                               std::to_string(tt->duration_value) + std::string(time_unit_to_string(tt->unit));
                } else if (!trans.event.empty() && trans.event != "Anonymous") {
                    trig_str = trans.event;
                }
                std::string guard_str =
                    (trans.guard && !trans.guard->empty()) ? resolve_guard_str(*trans.guard) : std::string{};
                std::string act_str = trans.transition_action.has_value()
                                          ? format_action_for_xmi(*trans.transition_action)
                                          : trans.get_action();

                out << tpad << "<transition xmi:type=\"uml:Transition\" xmi:id=\"_t" << (idx + 1) << "\" source=\""
                    << src_id << "\" target=\"" << dst_id << "\"";
                if (trans.kind == TransitionEdgeKind::Internal) {
                    out << " kind=\"internal\"";
                } else if (trans.kind == TransitionEdgeKind::Local) {
                    out << " kind=\"local\"";
                }
                if (trig_str.empty() && guard_str.empty() && act_str.empty() && trans.priority == 0) {
                    out << "/>\n";
                    return;
                }
                out << ">\n";
                if (trans.priority > 0) {
                    out << tpad << "  <xmi:Extension extender=\"fsmc\">\n";
                    out << tpad << "    <priority value=\"" << trans.priority << "\"/>\n";
                    out << tpad << "  </xmi:Extension>\n";
                }
                if (!trig_str.empty()) {
                    out << tpad << "  <trigger xmi:type=\"uml:Trigger\" xmi:id=\"_tr_" << (idx + 1) << "\" name=\""
                        << escape_xml(trig_str) << "\"/>\n";
                }
                if (!guard_str.empty()) {
                    out << tpad << "  <guard xmi:type=\"uml:Constraint\" xmi:id=\"_g_" << (idx + 1) << "\">\n";
                    out << tpad << "    <specification xmi:type=\"uml:OpaqueExpression\" xmi:id=\"_gspec_" << (idx + 1)
                        << "\">\n";
                    out << tpad << "      <body>" << escape_xml(guard_str) << "</body>\n";
                    out << tpad << "    </specification>\n";
                    out << tpad << "  </guard>\n";
                }
                if (!act_str.empty()) {
                    out << tpad << "  <effect xmi:type=\"uml:OpaqueBehavior\" xmi:id=\"_eff_" << (idx + 1) << "\">\n";
                    out << tpad << "    <body>" << escape_xml(act_str) << "</body>\n";
                    out << tpad << "  </effect>\n";
                }
                out << tpad << "</transition>\n";
            };

            // Local transitions within this region scope
            for (size_t i = 0; i < model.transitions.size(); ++i) {
                if (emitted_transitions.count(i))
                    continue;
                const auto& trans = model.transitions[i];
                auto it_p = parent_map.find(trans.source);
                std::string p_src = (it_p != parent_map.end()) ? it_p->second : "";
                if (p_src == scope_name) {
                    emitted_transitions.insert(i);
                    emit_uml_transition(i, trans, rpad + "  ");
                }
            }

            out << rpad << "</region>\n";
        };

        if (state.kind == StateKind::Parallel) {
            std::vector<const StateNode*> regions;
            for (const auto& child : model.states) {
                if (child.parent_state == state.name) {
                    regions.push_back(&child);
                }
            }
            if (!regions.empty()) {
                for (size_t r = 0; r < regions.size(); ++r) {
                    const auto* reg_state = regions[r];
                    std::string reg_scope_id = st_id + "_reg" + std::to_string(r + 1);
                    emit_region_contents(reg_state->name, reg_scope_id, *reg_state, indent + 1, reg_state->name);
                }
            } else {
                emit_region_contents(state.name, st_id, state, indent + 1, "");
            }
        } else if (state.is_composite) {
            emit_region_contents(state.name, st_id, state, indent + 1, "");
        }

        out << pad << "</subvertex>\n";
    };

    // Emit top-level states
    for (const auto& state : model.states) {
        if (state.parent_state.empty()) {
            emit_state_node(state, 4);
        }
    }

    // Root initial transition
    if (!model.initial_state.empty() && id_map.count(model.initial_state)) {
        out << "        <transition xmi:type=\"uml:Transition\" xmi:id=\"_t_root_init\" source=\"_ps_root\" target=\""
            << id_map[model.initial_state] << "\"/>\n";
    }

    // Outer / Cross-boundary transitions
    for (size_t i = 0; i < model.transitions.size(); ++i) {
        if (emitted_transitions.count(i))
            continue;
        const auto& trans = model.transitions[i];
        std::string src_id = id_map.count(trans.source) ? id_map[trans.source] : trans.source;
        std::string dst_id = id_map.count(trans.target) ? id_map[trans.target] : trans.target;
        if (const auto* tgt_st = model.find_state(trans.target);
            tgt_st != nullptr && tgt_st->is_composite && id_map.count(trans.target)) {
            if (trans.target_is_deep_history) {
                dst_id = "_ps_dhist_" + id_map[trans.target];
            } else if (trans.target_is_history) {
                dst_id = "_ps_hist_" + id_map[trans.target];
            }
        }
        std::string trig_str;
        if (const auto* ct = std::get_if<ChangeTrigger>(&trans.trigger); ct != nullptr && !ct->raw_expression.empty()) {
            trig_str = "when(" + ct->raw_expression + ")";
        } else if (const auto* tt = std::get_if<TimeTrigger>(&trans.trigger); tt != nullptr && trans.event.empty()) {
            trig_str = std::string(tt->kind == TimeTriggerKind::Every ? "every_" : "after_") +
                       std::to_string(tt->duration_value) + std::string(time_unit_to_string(tt->unit));
        } else if (!trans.event.empty() && trans.event != "Anonymous") {
            trig_str = trans.event;
        }
        std::string guard_str =
            (trans.guard && !trans.guard->empty()) ? resolve_guard_str(*trans.guard) : std::string{};
        std::string act_str =
            trans.transition_action.has_value() ? format_action_for_xmi(*trans.transition_action) : trans.get_action();

        out << "        <transition xmi:type=\"uml:Transition\" xmi:id=\"_t" << (i + 1) << "\" source=\"" << src_id
            << "\" target=\"" << dst_id << "\"";
        if (trans.kind == TransitionEdgeKind::Internal) {
            out << " kind=\"internal\"";
        } else if (trans.kind == TransitionEdgeKind::Local) {
            out << " kind=\"local\"";
        }
        if (trig_str.empty() && guard_str.empty() && act_str.empty() && trans.priority == 0) {
            out << "/>\n";
            continue;
        }
        out << ">\n";
        if (trans.priority > 0) {
            out << "          <xmi:Extension extender=\"fsmc\">\n";
            out << "            <priority value=\"" << trans.priority << "\"/>\n";
            out << "          </xmi:Extension>\n";
        }
        if (!trig_str.empty()) {
            out << "          <trigger xmi:type=\"uml:Trigger\" xmi:id=\"_tr_" << (i + 1) << "\" name=\""
                << escape_xml(trig_str) << "\"/>\n";
        }
        if (!guard_str.empty()) {
            out << "          <guard xmi:type=\"uml:Constraint\" xmi:id=\"_g_" << (i + 1) << "\">\n";
            out << "            <specification xmi:type=\"uml:OpaqueExpression\" xmi:id=\"_gspec_" << (i + 1)
                << "\">\n";
            out << "              <body>" << escape_xml(guard_str) << "</body>\n";
            out << "            </specification>\n";
            out << "          </guard>\n";
        }
        if (!act_str.empty()) {
            out << "          <effect xmi:type=\"uml:OpaqueBehavior\" xmi:id=\"_eff_" << (i + 1) << "\">\n";
            out << "            <body>" << escape_xml(act_str) << "</body>\n";
            out << "          </effect>\n";
        }
        out << "        </transition>\n";
    }

    out << "      </region>\n";
    out << "    </packagedElement>\n";

    // Emit declared signals
    for (const auto& sig : model.signals) {
        out << "    <packagedElement xmi:type=\"uml:Signal\" xmi:id=\"_sig_" << sig.name << "\" name=\""
            << escape_xml(sig.name) << "\"/>\n";
    }

    out << "  </uml:Model>\n";
    out << "</xmi:XMI>\n";
    return out.str();
}

}  // namespace fsm::backend::formal
