#include "fsm/backend/formal/xstate_serializer.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fsm/ir/guard.hpp"

namespace fsm::backend::formal {

using namespace fsm::ir;

namespace {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4U);
    for (const char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(static_cast<unsigned char>(c) >> 4U) & 0x0FU]);
                    out.push_back(kHex[static_cast<unsigned char>(c) & 0x0FU]);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string json_string(std::string_view s) {
    return "\"" + json_escape(s) + "\"";
}

std::string indent_str(std::size_t depth) {
    return std::string(depth * 2U, ' ');
}

bool is_numeric_literal(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    std::size_t i = 0U;
    if (s[i] == '-' || s[i] == '+') {
        ++i;
    }
    bool has_digits = false;
    bool has_dot = false;
    for (; i < s.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
            has_digits = true;
        } else if (s[i] == '.' && !has_dot) {
            has_dot = true;
        } else {
            return false;
        }
    }
    return has_digits;
}

std::string format_json_literal(const DataType& dt, std::string_view initial_value) {
    std::string val(initial_value);
    if (val.empty()) {
        if (dt.is_boolean()) {
            return "false";
        }
        if (dt.is_string()) {
            return "\"\"";
        }
        if (dt.is_integer() || (dt.is_primitive() && (dt.primitive() == PrimitiveTypeKind::Float32 ||
                                                      dt.primitive() == PrimitiveTypeKind::Float64))) {
            return "0";
        }
        return "null";
    }
    if (val == "true" || val == "false" || val == "null") {
        return val;
    }
    if (is_numeric_literal(val)) {
        if (val.front() == '+') {
            val.erase(val.begin());
        }
        return val;
    }
    if (val.size() >= 2U && val.front() == '"' && val.back() == '"') {
        return val;
    }
    return json_string(val);
}

std::string format_action_label(const ActionSignature& act) {
    if (!act.name.empty()) {
        return act.name;
    }
    if (!act.assignments.empty()) {
        const auto& a = act.assignments.front();
        return a.target.name + " " + std::string(assignment_op_to_string(a.op)) + " " + a.expression;
    }
    if (!act.instructions.empty()) {
        if (const auto* call = std::get_if<ActionCallOp>(&act.instructions.front().op)) {
            return call->function_name;
        }
        if (const auto* se = std::get_if<SignalEmitOp>(&act.instructions.front().op)) {
            return "emit:" + se->signal_name;
        }
    }
    return "action";
}

std::string resolve_guard_label(const TransitionEdge& tr, const FsmIr& model) {
    if (!tr.guard.has_value() || tr.guard->empty() || *tr.guard == "else" || *tr.guard == "otherwise" ||
        *tr.guard == "default") {
        return "";
    }
    if (tr.guard->find("fsm::") != std::string::npos) {
        return GuardExpressionParser::to_diagram_string(*tr.guard, model.guards);
    }
    if (const auto* g = model.find_guard(*tr.guard)) {
        if (g->raw_expression.has_value() && !g->raw_expression->empty()) {
            return *g->raw_expression;
        }
    }
    return *tr.guard;
}

std::vector<std::string> collect_transition_actions(const TransitionEdge& tr) {
    std::vector<std::string> actions;
    if (tr.condition_action.has_value()) {
        actions.push_back(format_action_label(*tr.condition_action));
    }
    if (tr.transition_action.has_value()) {
        actions.push_back(format_action_label(*tr.transition_action));
    } else if (const std::string act_name = tr.get_action(); !act_name.empty()) {
        actions.push_back(act_name);
    }
    return actions;
}

bool is_descendant_of(const std::string& candidate, const std::string& ancestor,
                      const std::unordered_map<std::string, const StateNode*>& state_map) {
    std::string curr = candidate;
    std::unordered_set<std::string> visited;
    while (!curr.empty() && visited.insert(curr).second) {
        auto it = state_map.find(curr);
        if (it == state_map.end()) {
            break;
        }
        if (it->second->parent_state == ancestor) {
            return true;
        }
        curr = it->second->parent_state;
    }
    return false;
}

std::string compute_xstate_target(const TransitionEdge& tr, const std::string& machine_id,
                                  const std::unordered_map<std::string, const StateNode*>& state_map) {
    if (tr.target.empty()) {
        return "";
    }
    // Build dotted path from machine root to target state: "#<machine_id>.<Root>...<Target>"
    std::vector<std::string> chain;
    std::string curr = tr.target;
    std::unordered_set<std::string> visited;
    while (!curr.empty() && visited.insert(curr).second) {
        chain.push_back(curr);
        auto it = state_map.find(curr);
        if (it == state_map.end() || it->second->parent_state.empty()) {
            break;
        }
        curr = it->second->parent_state;
    }
    std::reverse(chain.begin(), chain.end());

    // If source and target share the exact same parent and target is not a history pseudostate,
    // use concise sibling target name; otherwise use absolute XState v5 "#id.path" target.
    const auto src_it = state_map.find(tr.source);
    const auto dst_it = state_map.find(tr.target);
    const std::string src_parent = (src_it != state_map.end()) ? src_it->second->parent_state : "";
    const std::string dst_parent = (dst_it != state_map.end()) ? dst_it->second->parent_state : "";

    std::string suffix;
    if (tr.target_is_history || tr.target_is_deep_history) {
        suffix = "." + tr.target + (tr.target_is_deep_history ? "_deep_hist" : "_hist");
    }

    if (src_parent == dst_parent && suffix.empty()) {
        return tr.target;
    }

    std::string abs_target = "#" + machine_id;
    for (const auto& seg : chain) {
        abs_target += "." + seg;
    }
    abs_target += suffix;
    return abs_target;
}

void emit_transition_object(std::ostringstream& out, const TransitionEdge& tr, const FsmIr& model,
                            const std::unordered_map<std::string, const StateNode*>& state_map, std::size_t depth) {
    const std::string ind = indent_str(depth);
    const std::string ind1 = indent_str(depth + 1U);
    out << "{\n";
    bool first = true;
    auto sep = [&]() {
        if (!first) {
            out << ",\n";
        }
        first = false;
    };

    if (tr.kind != TransitionEdgeKind::Internal && !tr.target.empty()) {
        sep();
        out << ind1 << "\"target\": " << json_string(compute_xstate_target(tr, model.name, state_map));
    }

    const std::string guard_label = resolve_guard_label(tr, model);
    if (!guard_label.empty()) {
        sep();
        out << ind1 << "\"guard\": " << json_string(guard_label);
    }

    const auto actions = collect_transition_actions(tr);
    if (!actions.empty()) {
        sep();
        if (actions.size() == 1U) {
            out << ind1 << "\"actions\": " << json_string(actions.front());
        } else {
            out << ind1 << "\"actions\": [";
            for (std::size_t i = 0; i < actions.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << json_string(actions[i]);
            }
            out << "]";
        }
    }

    // XState v5 reenter semantics:
    // - External self-transition or composite->descendant transition re-enters source (`reenter: true`)
    // - Local transition does not re-enter source (`reenter: false`)
    if (tr.kind == TransitionEdgeKind::External &&
        (tr.source == tr.target || is_descendant_of(tr.target, tr.source, state_map))) {
        sep();
        out << ind1 << "\"reenter\": true";
    } else if (tr.kind == TransitionEdgeKind::Local) {
        sep();
        out << ind1 << "\"reenter\": false";
    }

    out << "\n" << ind << "}";
}

void emit_state_node(std::ostringstream& out, const StateNode& st, const FsmIr& model,
                     const std::unordered_map<std::string, const StateNode*>& state_map,
                     const std::unordered_map<std::string, std::vector<const StateNode*>>& children_map,
                     const std::unordered_map<std::string, std::vector<const TransitionEdge*>>& trans_by_src,
                     std::size_t depth) {
    const std::string ind = indent_str(depth);
    const std::string ind1 = indent_str(depth + 1U);
    out << "{\n";
    bool first = true;
    auto sep = [&]() {
        if (!first) {
            out << ",\n";
        }
        first = false;
    };

    // State kind / type
    if (st.kind == StateKind::Final) {
        sep();
        out << ind1 << "\"type\": \"final\"";
    } else if (st.kind == StateKind::Parallel) {
        sep();
        out << ind1 << "\"type\": \"parallel\"";
    } else if (st.kind == StateKind::ShallowHistory) {
        sep();
        out << ind1 << "\"type\": \"history\",\n" << ind1 << "\"history\": \"shallow\"";
    } else if (st.kind == StateKind::DeepHistory) {
        sep();
        out << ind1 << "\"type\": \"history\",\n" << ind1 << "\"history\": \"deep\"";
    }

    const auto child_it = children_map.find(st.name);
    const bool has_children = (child_it != children_map.end() && !child_it->second.empty());

    // Initial substate for composite (non-parallel) states
    if (has_children && st.kind != StateKind::Parallel) {
        std::string init_sub = st.initial_sub_state;
        if (init_sub.empty()) {
            init_sub = child_it->second.front()->name;
        }
        sep();
        out << ind1 << "\"initial\": " << json_string(init_sub);
    }

    // Entry actions
    if (!st.entry_actions.empty()) {
        sep();
        out << ind1 << "\"entry\": [";
        for (std::size_t i = 0; i < st.entry_actions.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << json_string(format_action_label(st.entry_actions[i]));
        }
        out << "]";
    }

    // Exit actions
    if (!st.exit_actions.empty()) {
        sep();
        out << ind1 << "\"exit\": [";
        for (std::size_t i = 0; i < st.exit_actions.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << json_string(format_action_label(st.exit_actions[i]));
        }
        out << "]";
    }

    // Do-activity -> XState v5 invoke
    if (st.do_activity.has_value() && !st.do_activity->empty()) {
        sep();
        out << ind1 << "\"invoke\": { \"src\": " << json_string(*st.do_activity) << " }";
    }

    // Group outgoing transitions into `on` (events), `after` (timed), and `always` (eventless/completion)
    if (auto tr_it = trans_by_src.find(st.name); tr_it != trans_by_src.end()) {
        std::map<std::string, std::vector<const TransitionEdge*>> on_map;
        std::map<std::uint64_t, std::vector<const TransitionEdge*>> after_map;
        std::vector<const TransitionEdge*> always_list;

        for (const auto* tr : tr_it->second) {
            if (std::holds_alternative<TimeTrigger>(tr->trigger)) {
                const auto& tt = std::get<TimeTrigger>(tr->trigger);
                after_map[tt.duration_in_ms()].push_back(tr);
            } else if (tr->event.empty() || tr->event == "anonymous_event" || tr->event == "completion_event") {
                always_list.push_back(tr);
            } else {
                on_map[tr->event].push_back(tr);
            }
        }

        if (!on_map.empty()) {
            sep();
            out << ind1 << "\"on\": {\n";
            bool first_ev = true;
            const std::string ind2 = indent_str(depth + 2U);
            for (const auto& [ev_name, edges] : on_map) {
                if (!first_ev) {
                    out << ",\n";
                }
                first_ev = false;
                out << ind2 << json_string(ev_name) << ": ";
                if (edges.size() == 1U) {
                    emit_transition_object(out, *edges.front(), model, state_map, depth + 2U);
                } else {
                    out << "[\n";
                    for (std::size_t ei = 0; ei < edges.size(); ++ei) {
                        if (ei > 0) {
                            out << ",\n";
                        }
                        out << indent_str(depth + 3U);
                        emit_transition_object(out, *edges[ei], model, state_map, depth + 3U);
                    }
                    out << "\n" << ind2 << "]";
                }
            }
            out << "\n" << ind1 << "}";
        }

        if (!after_map.empty()) {
            sep();
            out << ind1 << "\"after\": {\n";
            bool first_after = true;
            const std::string ind2 = indent_str(depth + 2U);
            for (const auto& [dur_ms, edges] : after_map) {
                if (!first_after) {
                    out << ",\n";
                }
                first_after = false;
                out << ind2 << json_string(std::to_string(dur_ms)) << ": ";
                if (edges.size() == 1U) {
                    emit_transition_object(out, *edges.front(), model, state_map, depth + 2U);
                } else {
                    out << "[\n";
                    for (std::size_t ei = 0; ei < edges.size(); ++ei) {
                        if (ei > 0) {
                            out << ",\n";
                        }
                        out << indent_str(depth + 3U);
                        emit_transition_object(out, *edges[ei], model, state_map, depth + 3U);
                    }
                    out << "\n" << ind2 << "]";
                }
            }
            out << "\n" << ind1 << "}";
        }

        if (!always_list.empty()) {
            sep();
            out << ind1 << "\"always\": [\n";
            for (std::size_t ai = 0; ai < always_list.size(); ++ai) {
                if (ai > 0) {
                    out << ",\n";
                }
                out << indent_str(depth + 2U);
                emit_transition_object(out, *always_list[ai], model, state_map, depth + 2U);
            }
            out << "\n" << ind1 << "]";
        }
    }

    // Metadata (requirements / deferred events)
    if (!st.traceability_reqs.empty() || !st.deferred_events.empty()) {
        sep();
        out << ind1 << "\"meta\": {\n";
        bool first_meta = true;
        const std::string ind2 = indent_str(depth + 2U);
        if (!st.traceability_reqs.empty()) {
            first_meta = false;
            out << ind2 << "\"requirements\": [";
            for (std::size_t i = 0; i < st.traceability_reqs.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << json_string(st.traceability_reqs[i]);
            }
            out << "]";
        }
        if (!st.deferred_events.empty()) {
            if (!first_meta) {
                out << ",\n";
            }
            out << ind2 << "\"deferred\": [";
            for (std::size_t i = 0; i < st.deferred_events.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << json_string(st.deferred_events[i]);
            }
            out << "]";
        }
        out << "\n" << ind1 << "}";
    }

    // Nested child states + synthesized history pseudostates
    if (has_children || st.has_history || st.has_deep_history) {
        sep();
        out << ind1 << "\"states\": {\n";
        bool first_child = true;
        const std::string ind2 = indent_str(depth + 2U);
        if (has_children) {
            for (const auto* child : child_it->second) {
                if (!first_child) {
                    out << ",\n";
                }
                first_child = false;
                out << ind2 << json_string(child->name) << ": ";
                emit_state_node(out, *child, model, state_map, children_map, trans_by_src, depth + 2U);
            }
        }
        if (st.has_history) {
            if (!first_child) {
                out << ",\n";
            }
            first_child = false;
            out << ind2 << json_string(st.name + "_hist") << ": {\n"
                << indent_str(depth + 3U) << "\"type\": \"history\",\n"
                << indent_str(depth + 3U) << "\"history\": \"shallow\"\n"
                << ind2 << "}";
        }
        if (st.has_deep_history) {
            if (!first_child) {
                out << ",\n";
            }
            out << ind2 << json_string(st.name + "_deep_hist") << ": {\n"
                << indent_str(depth + 3U) << "\"type\": \"history\",\n"
                << indent_str(depth + 3U) << "\"history\": \"deep\"\n"
                << ind2 << "}";
        }
        out << "\n" << ind1 << "}";
    }

    if (first) {
        out << ind << "}";
    } else {
        out << "\n" << ind << "}";
    }
}

}  // namespace

std::string XStateSerializer::serialize(const FsmIr& model) {
    FsmIr canonical = model;
    canonical.canonicalize();

    std::unordered_map<std::string, const StateNode*> state_map;
    std::unordered_map<std::string, std::vector<const StateNode*>> children_map;
    std::vector<const StateNode*> root_states;

    for (const auto& st : canonical.states) {
        state_map[st.name] = &st;
    }
    for (const auto& st : canonical.states) {
        if (st.parent_state.empty() || state_map.count(st.parent_state) == 0U) {
            root_states.push_back(&st);
        } else {
            children_map[st.parent_state].push_back(&st);
        }
    }

    std::unordered_map<std::string, std::vector<const TransitionEdge*>> trans_by_src;
    for (const auto& tr : canonical.transitions) {
        trans_by_src[tr.source].push_back(&tr);
    }

    const std::string machine_id = canonical.name.empty() ? "Fsm" : canonical.name;

    std::ostringstream out;
    out << "{\n";
    out << "  \"id\": " << json_string(machine_id);

    std::string initial_root = canonical.initial_state;
    if (initial_root.empty() && !root_states.empty()) {
        initial_root = root_states.front()->name;
    }
    // If initial_state points to a nested state, walk up to its top-level root state for root `initial`
    if (!initial_root.empty()) {
        std::string top = initial_root;
        std::unordered_set<std::string> visited;
        while (visited.insert(top).second) {
            auto it = state_map.find(top);
            if (it == state_map.end() || it->second->parent_state.empty() ||
                state_map.count(it->second->parent_state) == 0U) {
                break;
            }
            top = it->second->parent_state;
        }
        out << ",\n  \"initial\": " << json_string(top);
    }

    // XState v5 context: internal registers + I/O ports
    if (!canonical.variables.empty() || !canonical.ports.empty()) {
        out << ",\n  \"context\": {\n";
        bool first_ctx = true;
        for (const auto& var : canonical.variables) {
            if (!first_ctx) {
                out << ",\n";
            }
            first_ctx = false;
            out << "    " << json_string(var.name) << ": " << format_json_literal(var.type, var.initial_value);
        }
        for (const auto& port : canonical.ports) {
            if (!first_ctx) {
                out << ",\n";
            }
            first_ctx = false;
            out << "    " << json_string(port.name) << ": " << format_json_literal(port.type, port.default_value);
        }
        out << "\n  }";
    }

    // Top-level states
    out << ",\n  \"states\": {\n";
    for (std::size_t i = 0; i < root_states.size(); ++i) {
        if (i > 0) {
            out << ",\n";
        }
        out << "    " << json_string(root_states[i]->name) << ": ";
        emit_state_node(out, *root_states[i], canonical, state_map, children_map, trans_by_src, 2U);
    }
    out << "\n  }";

    // Preserve EFSM I/O port metadata and imperative action bodies in standard XState v5 top-level `meta`
    std::map<std::string, std::vector<std::string>> action_bodies;
    auto record_action_body = [&](const ActionSignature& act) {
        if (act.name.empty() || action_bodies.count(act.name) != 0U) {
            return;
        }
        std::vector<std::string> stmts;
        for (const auto& a : act.assignments) {
            stmts.push_back(a.target.full_path() + " " + std::string(assignment_op_to_string(a.op)) + " " +
                            a.expression);
        }
        for (const auto& inst : act.instructions) {
            if (const auto* st = std::get_if<StoreOp>(&inst.op)) {
                stmts.push_back(st->target.full_path() + " " + std::string(assignment_op_to_string(st->op)) + " " +
                                st->expression);
            } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                stmts.push_back(pw->port_name + " = " + pw->expression);
            }
        }
        if (!stmts.empty()) {
            action_bodies.emplace(act.name, std::move(stmts));
        }
    };
    for (const auto& st : canonical.states) {
        for (const auto& a : st.entry_actions) {
            record_action_body(a);
        }
        for (const auto& a : st.exit_actions) {
            record_action_body(a);
        }
        if (st.do_activity_action.has_value()) {
            record_action_body(*st.do_activity_action);
        }
    }
    for (const auto& tr : canonical.transitions) {
        if (tr.condition_action.has_value()) {
            record_action_body(*tr.condition_action);
        }
        if (tr.transition_action.has_value()) {
            record_action_body(*tr.transition_action);
        }
    }

    if (!canonical.ports.empty() || !action_bodies.empty()) {
        out << ",\n  \"meta\": {\n";
        bool first_meta = true;
        if (!canonical.ports.empty()) {
            first_meta = false;
            out << "    \"ports\": [\n";
            for (std::size_t i = 0; i < canonical.ports.size(); ++i) {
                const auto& p = canonical.ports[i];
                if (i > 0) {
                    out << ",\n";
                }
                out << "      { \"name\": " << json_string(p.name)
                    << ", \"direction\": " << json_string(p.is_in() ? "in" : "out")
                    << ", \"type\": " << json_string(p.type.to_canonical_string())
                    << ", \"default\": " << json_string(p.default_value) << " }";
            }
            out << "\n    ]";
        }
        if (!action_bodies.empty()) {
            if (!first_meta) {
                out << ",\n";
            }
            out << "    \"actions\": {\n";
            bool first_act = true;
            for (const auto& [aname, stmts] : action_bodies) {
                if (!first_act) {
                    out << ",\n";
                }
                first_act = false;
                out << "      " << json_string(aname) << ": [";
                for (std::size_t si = 0; si < stmts.size(); ++si) {
                    if (si > 0) {
                        out << ", ";
                    }
                    out << json_string(stmts[si]);
                }
                out << "]";
            }
            out << "\n    }";
        }
        out << "\n  }";
    }

    out << "\n}\n";

    return out.str();
}

}  // namespace fsm::backend::formal
