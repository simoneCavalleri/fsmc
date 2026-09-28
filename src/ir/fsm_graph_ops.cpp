#include "fsm/ir/fsm_graph_ops.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::ir {

void FsmGraphOps::normalize_hierarchy(FsmIr& ir) {
    if (!ir.initial_state.empty()) {
        if (auto* init = ir.find_state_mut(ir.initial_state)) {
            init->parent_state = "";
        }
    }

    // Find states with outgoing transitions at the root scope.
    // Such states are inferred to be at the root level — clear any spurious
    // parent_state that the parser may have assigned due to textual context.
    // Exception: states with pinned_parent=true have their parent explicitly
    // assigned by a middleend pass (e.g. OrthogonalProductPass) and must
    // keep their hierarchical relationship for runtime is_substate_of checks.
    for (const auto& t : ir.transitions) {
        if (t.parent_scope.empty() && !t.source.empty()) {
            if (auto* src_state = ir.find_state_mut(t.source)) {
                if (!src_state->pinned_parent) {
                    src_state->parent_state = "";
                }
            }
        }
    }

    // Rebuild children_ids and recompute is_composite for all states
    for (auto& p : ir.states) {
        p.children_ids.clear();
        bool has_children = false;
        for (const auto& c : ir.states) {
            if (c.parent_state == p.name) {
                has_children = true;
                p.children_ids.push_back(c.id);
            }
        }
        p.is_composite = (has_children || !p.initial_sub_state.empty());
        if (p.is_composite && p.kind == StateKind::Atomic) {
            p.kind = StateKind::Composite;
        }
    }
}

void FsmGraphOps::sync_interfaces(FsmIr& ir) {
    auto is_valid_ident = [](std::string_view s) {
        if (s.empty())
            return false;
        if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_')
            return false;
        for (char c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                return false;
        }
        return true;
    };

    // 1. Synchronize guards
    for (const auto& tr : ir.transitions) {
        std::vector<std::string> raw_names;
        if (tr.guard_ast.has_value()) {
            tr.guard_ast->collect_atomic_guards(raw_names);
        } else {
            std::string g_name = tr.get_guard();
            if (!g_name.empty()) {
                raw_names.push_back(std::move(g_name));
            }
        }

        for (const auto& raw : raw_names) {
            std::string token;
            auto try_add_token = [&](const std::string& tok) {
                if (is_valid_ident(tok) && tok != "true" && tok != "false" && tok != "else" && tok != "otherwise" &&
                    tok != "default" && tok != "fsm" && tok != "and_" && tok != "or_" && tok != "not_") {
                    bool found = false;
                    for (const auto& existing : ir.guards) {
                        if (existing.name == tok) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        ir.guards.emplace_back(tok, "", tok, tok);
                    }
                }
            };

            for (char c : raw) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                    token += c;
                } else {
                    if (!token.empty()) {
                        try_add_token(token);
                        token.clear();
                    }
                }
            }
            if (!token.empty()) {
                try_add_token(token);
            }
        }
    }

    // 2. Synchronize actions from transitions (both condition actions and transition actions)
    auto try_add_action = [&](const std::string& a_name) {
        if (a_name.empty())
            return;
        bool found = false;
        for (const auto& existing : ir.actions) {
            if (existing.name == a_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            ir.actions.emplace_back(a_name);
        }
    };

    for (const auto& tr : ir.transitions) {
        if (tr.condition_action.has_value()) {
            try_add_action(tr.condition_action->name);
        }
        if (tr.transition_action.has_value()) {
            try_add_action(tr.transition_action->name);
        }
    }

    // 3. Synchronize actions from state entry and exit actions
    for (const auto& st : ir.states) {
        for (const auto& act : st.entry_actions) {
            if (!act.name.empty()) {
                try_add_action(act.name);
            }
        }
        for (const auto& act : st.exit_actions) {
            if (!act.name.empty()) {
                try_add_action(act.name);
            }
        }
    }
}

void FsmGraphOps::rebuild_adjacency_indices(FsmIr& ir) {
    for (auto& st : ir.states) {
        st.outgoing_transitions.clear();
        st.incoming_transitions.clear();
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(ir.transitions.size()); ++i) {
        const auto& tr = ir.transitions[i];
        if (!tr.source.empty()) {
            if (auto* src_node = ir.find_state(tr.source)) {
                src_node->outgoing_transitions.push_back(i);
            }
        }
        for (const auto& s_id : tr.source_ids) {
            if (s_id != tr.source) {
                if (auto* src_node = ir.find_state(s_id)) {
                    if (std::find(src_node->outgoing_transitions.begin(), src_node->outgoing_transitions.end(), i) ==
                        src_node->outgoing_transitions.end()) {
                        src_node->outgoing_transitions.push_back(i);
                    }
                }
            }
        }
        if (!tr.target.empty()) {
            if (auto* dst_node = ir.find_state(tr.target)) {
                dst_node->incoming_transitions.push_back(i);
            }
        }
        for (const auto& t_id : tr.target_ids) {
            if (t_id != tr.target) {
                if (auto* dst_node = ir.find_state(t_id)) {
                    if (std::find(dst_node->incoming_transitions.begin(), dst_node->incoming_transitions.end(), i) ==
                        dst_node->incoming_transitions.end()) {
                        dst_node->incoming_transitions.push_back(i);
                    }
                }
            }
        }
    }
}

void FsmGraphOps::sort_transitions_by_priority(FsmIr& ir) {
    std::stable_sort(ir.transitions.begin(), ir.transitions.end(),
                     [](const TransitionEdge& a, const TransitionEdge& b) {
                         if (a.source != b.source)
                             return false;
                         std::uint32_t pa = a.priority == 0 ? std::numeric_limits<std::uint32_t>::max() : a.priority;
                         std::uint32_t pb = b.priority == 0 ? std::numeric_limits<std::uint32_t>::max() : b.priority;
                         return pa < pb;
                     });
}

void FsmGraphOps::canonicalize(FsmIr& ir) {
    if (ir.id.empty()) {
        ir.id = compute_deterministic_id(ir.name + (ir.package.empty() ? "" : "_" + ir.package));
    }
    if (ir.initial_state_id.empty() && !ir.initial_state.empty()) {
        ir.initial_state_id = ir.initial_state;
    }
    // Synchronize transitions: guard <-> guard_ast
    for (auto& tr : ir.transitions) {
        if (tr.guard_ast.has_value() && (!tr.guard.has_value() || *tr.guard != tr.guard_ast->to_string())) {
            tr.guard = tr.guard_ast->to_string();
        } else if (!tr.guard_ast.has_value() && tr.guard.has_value() && !tr.guard->empty()) {
            tr.guard_ast = GuardAstNode(*tr.guard);
        }
    }
    // Sort transitions by priority within each source state
    sort_transitions_by_priority(ir);
    // Sort states by FQN for deterministic canonical order
    std::sort(ir.states.begin(), ir.states.end(), [](const StateNode& a, const StateNode& b) { return a.fqn < b.fqn; });
    // Sort ports by name
    std::sort(ir.ports.begin(), ir.ports.end(),
              [](const PortDefinition& a, const PortDefinition& b) { return a.name < b.name; });
    // Sort signals by name
    std::sort(ir.signals.begin(), ir.signals.end(),
              [](const SignalDefinition& a, const SignalDefinition& b) { return a.name < b.name; });
    // Sort variables by name
    std::sort(ir.variables.begin(), ir.variables.end(),
              [](const VariableDefinition& a, const VariableDefinition& b) { return a.name < b.name; });
    // Sort custom types by name (single source of truth)
    std::sort(ir.custom_types.begin(), ir.custom_types.end(),
              [](const TypeDefinition& a, const TypeDefinition& b) { return a.name < b.name; });
    // Sort properties by name
    std::sort(ir.properties.begin(), ir.properties.end(),
              [](const FormalProperty& a, const FormalProperty& b) { return a.name < b.name; });
    // Synchronize interfaces (guards and actions)
    sync_interfaces(ir);
    std::sort(ir.guards.begin(), ir.guards.end(),
              [](const GuardModel& a, const GuardModel& b) { return a.name < b.name; });
    std::sort(ir.actions.begin(), ir.actions.end(),
              [](const ActionModel& a, const ActionModel& b) { return a.name < b.name; });
    // Rebuild adjacency indices on StateNode
    rebuild_adjacency_indices(ir);
}

bool FsmGraphOps::is_well_formed(const FsmIr& ir, std::string& error) noexcept {
    if (!ir.concurrency.is_valid()) {
        error =
            "Invalid concurrency semantics: SynchronousReactive clock model incompatible with Interleaved orthogonal "
            "resolution";
        return false;
    }
    if (!ir.initial_state.empty() && ir.find_state(ir.initial_state) == nullptr) {
        error = "Initial state '" + ir.initial_state + "' not found in states";
        return false;
    }
    for (const auto& tr : ir.transitions) {
        if (!tr.source.empty() && ir.find_state(tr.source) == nullptr) {
            error = "Transition source state '" + tr.source + "' not found in states";
            return false;
        }
        if (!tr.target.empty() && ir.find_state(tr.target) == nullptr) {
            error = "Transition target state '" + tr.target + "' not found in states";
            return false;
        }
    }
    return true;
}

}  // namespace fsm::ir
