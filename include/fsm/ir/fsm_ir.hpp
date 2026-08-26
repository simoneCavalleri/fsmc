#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/event_model.hpp"
#include "fsm/ir/formal_property.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/region.hpp"
#include "fsm/ir/signal_definition.hpp"
#include "fsm/ir/state_kind.hpp"
#include "fsm/ir/state_node.hpp"
#include "fsm/ir/transition_edge.hpp"
#include "fsm/ir/transition_edge_kind.hpp"
#include "fsm/ir/trigger.hpp"
#include "fsm/ir/variable_definition.hpp"

namespace fsm::codegen {

// ============================================================================
// FsmIr: Strongly-Typed Formal Intermediate Representation Root
// ============================================================================

struct FsmIr {
    std::string id;
    std::string name = "MyStateMachine";
    std::string ns = "fsm_generated";
    std::string context_type = "no_context";
    std::string initial_state;
    std::string initial_state_id;
    bool thread_safe = true;
    std::vector<std::string> satisfies_reqs;

    std::vector<StateNode> states;
    std::vector<TransitionEdge> transitions;
    std::vector<SignalDefinition> signals;
    std::vector<VariableDefinition> variables;
    std::vector<FormalProperty> properties;
    std::vector<EventModel> events;
    std::vector<GuardModel> guards;
    std::vector<ActionModel> actions;
    std::vector<ChoiceNodeModel> choice_nodes;

    // Lookups
    [[nodiscard]] const StateNode* find_state_by_id(std::string_view state_id) const noexcept {
        for (const auto& s : states) {
            if (s.id == state_id)
                return &s;
        }
        return nullptr;
    }

    [[nodiscard]] StateNode* find_state_by_id(std::string_view state_id) noexcept {
        for (auto& s : states) {
            if (s.id == state_id)
                return &s;
        }
        return nullptr;
    }

    [[nodiscard]] const StateNode* find_state_by_name(std::string_view state_name) const noexcept {
        for (const auto& s : states) {
            if (s.name == state_name)
                return &s;
        }
        return nullptr;
    }

    [[nodiscard]] StateNode* find_state_by_name(std::string_view state_name) noexcept {
        for (auto& s : states) {
            if (s.name == state_name)
                return &s;
        }
        return nullptr;
    }

    [[nodiscard]] const StateNode* find_state_by_fqn(std::string_view state_fqn) const noexcept {
        for (const auto& s : states) {
            if (s.fqn == state_fqn)
                return &s;
        }
        return nullptr;
    }

    [[nodiscard]] StateNode* find_state_by_fqn(std::string_view state_fqn) noexcept {
        for (auto& s : states) {
            if (s.fqn == state_fqn)
                return &s;
        }
        return nullptr;
    }

    [[nodiscard]] const SignalDefinition* find_signal(std::string_view sig_name) const noexcept {
        for (const auto& sig : signals) {
            if (sig.name == sig_name)
                return &sig;
        }
        return nullptr;
    }

    [[nodiscard]] const VariableDefinition* find_variable(std::string_view var_name) const noexcept {
        for (const auto& var : variables) {
            if (var.name == var_name)
                return &var;
        }
        return nullptr;
    }

    [[nodiscard]] const FormalProperty* find_property(std::string_view prop_name) const noexcept {
        for (const auto& prop : properties) {
            if (prop.name == prop_name)
                return &prop;
        }
        return nullptr;
    }

    StateNode& add_or_get_state(const std::string& state_name, const std::string& parent_fqn = "",
                                StateKind kind = StateKind::Atomic) {
        for (auto& s : states) {
            if (s.name == state_name) {
                return s;
            }
        }
        std::string full_fqn = parent_fqn.empty() ? state_name : (parent_fqn + "." + state_name);
        std::string s_id = compute_deterministic_id(full_fqn);
        StateNode node(s_id, state_name, full_fqn, kind);
        node.parent_state = parent_fqn;
        if (!parent_fqn.empty()) {
            const auto* parent = find_state_by_name(parent_fqn);
            if (parent != nullptr) {
                node.parent_id = parent->id;
            }
        }
        states.push_back(node);
        // Link child to parent
        if (!parent_fqn.empty()) {
            auto* parent = find_state_by_name(parent_fqn);
            if (parent != nullptr) {
                parent->children_ids.push_back(node.id);
                parent->is_composite = true;
                if (parent->kind == StateKind::Atomic) {
                    parent->kind = StateKind::Composite;
                }
            }
        }
        return states.back();
    }

    [[nodiscard]] const StateNode* find_state(std::string_view state_name) const noexcept {
        return find_state_by_name(state_name);
    }

    [[nodiscard]] StateNode* find_state_mut(std::string_view state_name) noexcept {
        return find_state_by_name(state_name);
    }

    StateNode& add_state(const std::string& state_name, const std::string& parent = "",
                         StateKind kind = StateKind::Atomic) {
        return add_or_get_state(state_name, parent, kind);
    }

    StateNode& add_state(StateNode node) {
        for (auto& existing : states) {
            if (existing.name == node.name) {
                existing = std::move(node);
                return existing;
            }
        }
        states.push_back(std::move(node));
        return states.back();
    }

    void add_signal(SignalDefinition sig) {
        for (auto& existing : signals) {
            if (existing.name == sig.name) {
                existing = std::move(sig);
                return;
            }
        }
        signals.push_back(std::move(sig));
    }

    void add_variable(VariableDefinition var) {
        for (auto& existing : variables) {
            if (existing.name == var.name) {
                existing = std::move(var);
                return;
            }
        }
        variables.push_back(std::move(var));
    }

    void add_property(FormalProperty prop) {
        for (auto& existing : properties) {
            if (existing.name == prop.name) {
                existing = std::move(prop);
                return;
            }
        }
        properties.push_back(std::move(prop));
    }

    void add_event(const std::string& event_name) {
        if (event_name.empty())
            return;
        for (const auto& ev : events) {
            if (ev.name == event_name)
                return;
        }
        events.emplace_back(event_name);
        if (find_signal(event_name) == nullptr) {
            signals.emplace_back(event_name);
        }
    }

    void add_guard(const std::string& guard_name) {
        if (guard_name.empty())
            return;
        for (const auto& g : guards) {
            if (g.name == guard_name)
                return;
        }
        guards.emplace_back(guard_name);
    }

    void add_action(const std::string& action_name) {
        if (action_name.empty())
            return;
        for (const auto& a : actions) {
            if (a.name == action_name)
                return;
        }
        actions.emplace_back(action_name);
    }

    void add_choice_node(const std::string& choice_name) {
        if (choice_name.empty())
            return;
        for (const auto& c : choice_nodes) {
            if (c.name == choice_name)
                return;
        }
        choice_nodes.emplace_back(choice_name);
    }

    [[nodiscard]] bool is_choice_node(const std::string& node_name) const noexcept {
        for (const auto& c : choice_nodes) {
            if (c.name == node_name)
                return true;
        }
        const auto* s = find_state_by_name(node_name);
        return s != nullptr && s->kind == StateKind::Choice;
    }

    // Direct transition addition
    void add_transition(TransitionEdge edge) { transitions.push_back(std::move(edge)); }

    TransitionEdge& add_transition(const std::string& src_id, const std::string& dst_id, TriggerVariant trigger,
                                   std::optional<GuardAstNode> guard = std::nullopt,
                                   std::optional<ActionSignature> action = std::nullopt,
                                   TransitionEdgeKind edge_kind = TransitionEdgeKind::External) {
        std::string trig_str;
        if (std::holds_alternative<SignalTrigger>(trigger)) {
            trig_str = std::get<SignalTrigger>(trigger).signal_name;
            add_event(trig_str);
        } else if (std::holds_alternative<TimeTrigger>(trigger)) {
            trig_str = std::to_string(std::get<TimeTrigger>(trigger).duration_ms);
        }
        std::string guard_str = guard.has_value() ? guard->to_string() : "";
        std::string canonical_sig = src_id + "->" + dst_id + ":" + trig_str + "[" + guard_str + "]";
        std::string edge_id = compute_deterministic_id(canonical_sig);

        TransitionEdge edge(edge_id, src_id, dst_id, std::move(trigger));
        if (guard.has_value()) {
            edge.guard = guard->to_string();
            edge.guard_ast = std::move(guard);
        }
        if (action.has_value()) {
            edge.action = action->name;
            edge.action_sig = std::move(action);
        }
        edge.kind = edge_kind;
        transitions.push_back(std::move(edge));
        return transitions.back();
    }

    void normalize_hierarchy() {
        if (!initial_state.empty()) {
            if (auto* init = find_state_mut(initial_state)) {
                init->parent_state = "";
            }
        }

        // Find states with outgoing transitions at the root scope
        for (const auto& t : transitions) {
            if (t.parent_scope.empty() && !t.source.empty()) {
                if (auto* src_state = find_state_mut(t.source)) {
                    src_state->parent_state = "";
                }
            }
        }

        // Rebuild children_ids and recompute is_composite for all states
        for (auto& p : states) {
            p.children_ids.clear();
            bool has_children = false;
            for (const auto& c : states) {
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

    // Recomputes deterministic IDs and sorts containers canonically
    void canonicalize() {
        if (id.empty()) {
            id = compute_deterministic_id(name + "_" + ns);
        }
        if (initial_state_id.empty() && !initial_state.empty()) {
            initial_state_id = initial_state;
        }
        // Sort states by FQN for deterministic canonical order
        std::sort(states.begin(), states.end(), [](const StateNode& a, const StateNode& b) { return a.fqn < b.fqn; });
        // Sort signals by name
        std::sort(signals.begin(), signals.end(),
                  [](const SignalDefinition& a, const SignalDefinition& b) { return a.name < b.name; });
        // Sort variables by name
        std::sort(variables.begin(), variables.end(),
                  [](const VariableDefinition& a, const VariableDefinition& b) { return a.name < b.name; });
        // Sort properties by name
        std::sort(properties.begin(), properties.end(),
                  [](const FormalProperty& a, const FormalProperty& b) { return a.name < b.name; });
    }

    bool operator==(const FsmIr& other) const noexcept {
        return name == other.name && ns == other.ns && context_type == other.context_type &&
               initial_state_id == other.initial_state_id && thread_safe == other.thread_safe &&
               satisfies_reqs == other.satisfies_reqs && states == other.states && transitions == other.transitions &&
               signals == other.signals && variables == other.variables && properties == other.properties;
    }
};

}  // namespace fsm::codegen

namespace fsm {
using FsmIr = fsm::codegen::FsmIr;
using StateNode = fsm::codegen::StateNode;
using StateKind = fsm::codegen::StateKind;
using TransitionEdge = fsm::codegen::TransitionEdge;
using TransitionEdgeKind = fsm::codegen::TransitionEdgeKind;
using SignalDefinition = fsm::codegen::SignalDefinition;
using SignalAttribute = fsm::codegen::SignalAttribute;
using VariableDefinition = fsm::codegen::VariableDefinition;
using ActionAssignment = fsm::codegen::ActionAssignment;
using TemporalOp = fsm::codegen::TemporalOp;
using PropertyAstNode = fsm::codegen::PropertyAstNode;
using PropertyKind = fsm::codegen::PropertyKind;
using FormalProperty = fsm::codegen::FormalProperty;
using SignalTrigger = fsm::codegen::SignalTrigger;
using TimeTrigger = fsm::codegen::TimeTrigger;
using AnonymousTrigger = fsm::codegen::AnonymousTrigger;
using TriggerType = fsm::codegen::TriggerType;
using GuardAstNode = fsm::codegen::GuardAstNode;
using GuardOp = fsm::codegen::GuardOp;
using ActionSignature = fsm::codegen::ActionSignature;
using ActionModel = fsm::codegen::ActionModel;
using EventModel = fsm::codegen::EventModel;
using GuardModel = fsm::codegen::GuardModel;
using ChoiceNodeModel = fsm::codegen::ChoiceNodeModel;
using OrthogonalRegion = fsm::codegen::OrthogonalRegion;
using PortMapping = fsm::codegen::PortMapping;
using SubmachineRef = fsm::codegen::SubmachineRef;
using fsm::codegen::compute_deterministic_id;
}  // namespace fsm
