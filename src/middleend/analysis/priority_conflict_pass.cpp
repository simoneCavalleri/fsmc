/**
 * @file priority_conflict_pass.cpp
 * @brief Implementation of PriorityConflictPass.
 */

#include "fsm/middleend/analysis/priority_conflict_pass.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsm::middleend::analysis {

namespace {

std::string get_trigger_key(const ir::TransitionEdge& edge) {
    if (!edge.event.empty()) {
        return "event:" + edge.event;
    }
    if (std::holds_alternative<ir::SignalTrigger>(edge.trigger)) {
        const auto& sig = std::get<ir::SignalTrigger>(edge.trigger).signal_name;
        if (!sig.empty()) {
            return "event:" + sig;
        }
    }
    if (std::holds_alternative<ir::TimeTrigger>(edge.trigger)) {
        const auto& tt = std::get<ir::TimeTrigger>(edge.trigger);
        return "time:" + std::to_string(tt.duration_in_ms());
    }
    if (std::holds_alternative<ir::ChangeTrigger>(edge.trigger)) {
        return "change:" + std::get<ir::ChangeTrigger>(edge.trigger).raw_expression;
    }
    if (std::holds_alternative<ir::AnonymousTrigger>(edge.trigger)) {
        return "anonymous";
    }
    return "unknown";
}

bool is_ancestor_of(const ir::FsmIr& ir, const std::string& parent_id, const std::string& child_id) {
    const auto* child = ir.find_state_by_id(child_id);
    if (!child)
        child = ir.find_state_by_name(child_id);
    if (!child)
        return false;

    std::string canonical_parent_id = parent_id;
    std::string canonical_parent_name = parent_id;
    if (const auto* p_node = ir.find_state_by_id(parent_id)) {
        canonical_parent_name = p_node->name;
    } else if (const auto* p_node2 = ir.find_state_by_name(parent_id)) {
        canonical_parent_id = p_node2->id;
    }

    const auto* curr = child;
    std::unordered_set<std::string> visited;
    while (curr) {
        if (!visited.insert(curr->id.empty() ? curr->name : curr->id).second) {
            break;
        }
        std::string p_id = curr->parent_id.value_or("");
        std::string p_name = curr->parent_state;

        if ((!p_id.empty() && (p_id == canonical_parent_id || p_id == parent_id)) ||
            (!p_name.empty() && (p_name == canonical_parent_name || p_name == parent_id || p_name == canonical_parent_id))) {
            return true;
        }

        const auto* next_p = !p_id.empty() ? ir.find_state_by_id(p_id) : nullptr;
        if (!next_p && !p_name.empty()) {
            next_p = ir.find_state_by_name(p_name);
        }
        if (!next_p && !p_id.empty()) {
            next_p = ir.find_state_by_name(p_id);
        }
        curr = next_p;
    }
    return false;
}

std::string resolve_id(const ir::FsmIr& ir, const std::string& id_or_name) {
    if (auto* s = ir.find_state_by_id(id_or_name))
        return s->id;
    if (auto* s = ir.find_state_by_name(id_or_name))
        return s->id;
    return id_or_name;
}

}  // namespace

bool PriorityConflictPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool fatal_conflict = false;

    // 1. Peer outgoing transitions conflict check
    std::unordered_map<std::string, std::vector<const ir::TransitionEdge*>> state_transitions;
    for (const auto& edge : ir.transitions) {
        std::string src = resolve_id(ir, edge.source_id.empty() ? edge.source : edge.source_id);
        state_transitions[src].push_back(&edge);
    }

    for (const auto& [state_id, edges] : state_transitions) {
        auto* state = ir.find_state_by_id(state_id);
        std::string state_name = state ? state->name : state_id;

        for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
                const auto* t1 = edges[i];
                const auto* t2 = edges[j];

                std::string k1 = get_trigger_key(*t1);
                std::string k2 = get_trigger_key(*t2);

                if (k1 == k2 && !k1.empty()) {
                    // Both trigger on same event
                    bool guards_equal = (!t1->guard.has_value() && !t2->guard.has_value()) ||
                                        (t1->guard.has_value() && t2->guard.has_value() && *t1->guard == *t2->guard);
                    if (guards_equal && t1->priority == t2->priority) {
                        diag.report(diagnostic::Diagnostic::warning(
                            "W0402", "Potential priority race: state '" + state_name +
                                         "' has multiple outgoing transitions on trigger '" + k1 +
                                         "' with identical priority (" + std::to_string(t1->priority) + ")."));
                    }
                }
            }
        }
    }

    // 2. Hierarchical preemption conflict check
    ir::HierarchicalPriority priority_policy = ir.execution_semantics.preemption_priority;

    for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
        for (std::size_t j = 0; j < ir.transitions.size(); ++j) {
            if (i == j)
                continue;

            const auto& t1 = ir.transitions[i];
            const auto& t2 = ir.transitions[j];

            std::string src1 = resolve_id(ir, t1.source_id.empty() ? t1.source : t1.source_id);
            std::string src2 = resolve_id(ir, t2.source_id.empty() ? t2.source : t2.source_id);

            if (src1 == src2)
                continue;

            if (is_ancestor_of(ir, src1, src2)) {
                // src1 is ancestor (parent) of src2 (child)
                std::string k1 = get_trigger_key(t1);
                std::string k2 = get_trigger_key(t2);

                if (k1 == k2 && !k1.empty()) {
                    // Preemption conflict check
                    // In fsmc, non-zero priority: 1 = Highest, 2 = Second highest, etc.
                    if (t1.priority > 0 && t2.priority > 0) {
                        if (priority_policy == ir::HierarchicalPriority::OuterFirst && t2.priority < t1.priority) {
                            // Child transition has higher numerical priority than parent under OuterFirst policy
                            diag.report(diagnostic::Diagnostic::error(
                                "E0402", "Hierarchical priority contradiction: child transition from '" + src2 +
                                             "' specifies priority " + std::to_string(t2.priority) +
                                             " exceeding parent transition from '" + src1 + "' (" +
                                             std::to_string(t1.priority) + ") under OuterFirst policy."));
                            fatal_conflict = true;
                        } else if (priority_policy == ir::HierarchicalPriority::InnerFirst &&
                                   t1.priority < t2.priority) {
                            // Parent transition has higher numerical priority than child under InnerFirst policy
                            diag.report(diagnostic::Diagnostic::error(
                                "E0402", "Hierarchical priority contradiction: parent transition from '" + src1 +
                                             "' specifies priority " + std::to_string(t1.priority) +
                                             " exceeding child transition from '" + src2 + "' (" +
                                             std::to_string(t2.priority) + ") under InnerFirst policy."));
                            fatal_conflict = true;
                        }
                    }
                }
            }
        }
    }

    return !fatal_conflict;
}

}  // namespace fsm::middleend::analysis
