#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/enum_definition.hpp"
#include "fsm/ir/event_model.hpp"
#include "fsm/ir/formal_property.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/port_definition.hpp"
#include "fsm/ir/region.hpp"
#include "fsm/ir/signal_definition.hpp"
#include "fsm/ir/state_kind.hpp"
#include "fsm/ir/state_node.hpp"
#include "fsm/ir/expression.hpp"
#include "fsm/ir/struct_definition.hpp"
#include "fsm/ir/transition_edge.hpp"
#include "fsm/ir/transition_edge_kind.hpp"
#include "fsm/ir/trigger.hpp"
#include "fsm/ir/type_definition.hpp"
#include "fsm/ir/variable_definition.hpp"

namespace fsm::ir {

// ============================================================================
/**
 * @brief Canonical Intermediate Representation (IR) Root Metamodel for Finite State Machines.
 *
 * `FsmIr` serves as the universal abstract syntax tree (AST) exchanged across the compiler pipeline:
 * - Ingested by Frontend Parsers (`include/fsm/frontend/`)
 * - Transformed and Verified by Middle-End Optimization/SMT Passes (`include/fsm/middleend/`)
 * - Serialized by Backend Code Generators and Diagram Transpilers (`include/fsm/backend/`)
 */
struct FsmIr {
    std::string id;                                              ///< Unique deterministic identifier
    std::string name = "MyStateMachine";                         ///< State machine class name
    std::string package = "";                                    ///< Logical package / module / model namespace
    std::string initial_state;                                   ///< Initial state unqualified name
    std::string initial_state_id;                                ///< Initial state deterministic ID
    std::vector<std::string> satisfies_reqs;                     ///< Requirement traceability IDs
    std::unordered_map<std::string, std::string> attributes;     ///< Target-agnostic metadata attributes

    std::vector<StateNode> states;              ///< Hierarchy of state nodes (simple, composite, parallel)
    std::vector<TransitionEdge> transitions;    ///< Directed transition edges with triggers, guards, actions
    std::vector<PortDefinition> ports;          ///< Typed InPorts / OutPorts with range contracts
    std::vector<SignalDefinition> signals;      ///< MBSE typed signal definitions
    std::vector<VariableDefinition> variables;  ///< Internal registers with datapath bounds
    std::vector<TypeDefinition> custom_types;   ///< Canonical user-defined compound types (Enum, Struct, Alias)
    std::vector<FormalProperty> properties;     ///< Formal LTL/CTL temporal verification specifications
    std::vector<GuardModel> guards;             ///< Guard predicates with C++ / SMT expressions
    std::vector<ActionModel> actions;           ///< Action effects and assignment sequences
    std::vector<ChoiceNodeModel> choice_nodes;  ///< Choice pseudostates for middle-end inlining

    // ========================================================================
    // Lookups and Query Methods
    // ========================================================================

    /**
     * @brief Finds a state node by its deterministic unique identifier.
     * @param state_id The unique state identifier string.
     * @return Const pointer to StateNode if found, nullptr otherwise.
     */
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

    [[nodiscard]] const PortDefinition* find_port(std::string_view port_name) const noexcept {
        for (const auto& port : ports) {
            if (port.name == port_name)
                return &port;
        }
        return nullptr;
    }

    [[nodiscard]] PortDefinition* find_port_mut(std::string_view port_name) noexcept {
        for (auto& port : ports) {
            if (port.name == port_name)
                return &port;
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<PortDefinition> get_in_ports() const {
        std::vector<PortDefinition> res;
        for (const auto& p : ports) {
            if (p.is_in())
                res.push_back(p);
        }
        return res;
    }

    [[nodiscard]] std::vector<PortDefinition> get_out_ports() const {
        std::vector<PortDefinition> res;
        for (const auto& p : ports) {
            if (p.is_out())
                res.push_back(p);
        }
        return res;
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

    [[nodiscard]] std::vector<TypeDefinition> get_enums() const {
        std::vector<TypeDefinition> res;
        for (const auto& ct : custom_types) {
            if (ct.is_enum())
                res.push_back(ct);
        }
        return res;
    }

    [[nodiscard]] std::vector<TypeDefinition> get_structs() const {
        std::vector<TypeDefinition> res;
        for (const auto& ct : custom_types) {
            if (ct.is_struct())
                res.push_back(ct);
        }
        return res;
    }

    [[nodiscard]] std::vector<TypeDefinition> get_aliases() const {
        std::vector<TypeDefinition> res;
        for (const auto& ct : custom_types) {
            if (ct.is_alias())
                res.push_back(ct);
        }
        return res;
    }

    [[nodiscard]] const TypeDefinition* find_type(std::string_view type_name) const noexcept {
        for (const auto& t : custom_types) {
            if (t.name == type_name)
                return &t;
        }
        return nullptr;
    }

    [[nodiscard]] TypeDefinition* find_type_mut(std::string_view type_name) noexcept {
        for (auto& t : custom_types) {
            if (t.name == type_name)
                return &t;
        }
        return nullptr;
    }

    [[nodiscard]] const TypeDefinition* find_enum(std::string_view enum_name) const noexcept {
        const auto* t = find_type(enum_name);
        return (t != nullptr && t->is_enum()) ? t : nullptr;
    }

    [[nodiscard]] TypeDefinition* find_enum_mut(std::string_view enum_name) noexcept {
        auto* t = find_type_mut(enum_name);
        return (t != nullptr && t->is_enum()) ? t : nullptr;
    }

    [[nodiscard]] const TypeDefinition* find_struct(std::string_view struct_name) const noexcept {
        const auto* t = find_type(struct_name);
        return (t != nullptr && t->is_struct()) ? t : nullptr;
    }

    [[nodiscard]] TypeDefinition* find_struct_mut(std::string_view struct_name) noexcept {
        auto* t = find_type_mut(struct_name);
        return (t != nullptr && t->is_struct()) ? t : nullptr;
    }

    [[nodiscard]] bool has_type(std::string_view type_name) const noexcept {
        return find_type(type_name) != nullptr;
    }

    void add_type(TypeDefinition type) {
        // Canonical primary storage
        for (auto& existing : custom_types) {
            if (existing.name == type.name) {
                existing = std::move(type);
                return;
            }
        }
        custom_types.push_back(std::move(type));
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

    [[nodiscard]] StateNode* find_state(std::string_view state_name) noexcept {
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

    void add_port(PortDefinition port) {
        for (auto& existing : ports) {
            if (existing.name == port.name) {
                existing = std::move(port);
                return;
            }
        }
        ports.push_back(std::move(port));
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

    void add_enum(EnumDefinition def) {
        add_type(TypeDefinition(std::move(def)));
    }

    void add_struct(StructDefinition def) {
        add_type(TypeDefinition(std::move(def)));
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

    void add_event(const std::string& event_name, std::string desc = "") {
        if (event_name.empty())
            return;
        for (auto& s : signals) {
            if (s.name == event_name) {
                if (s.description.empty() && !desc.empty()) {
                    s.description = std::move(desc);
                }
                return;
            }
        }
        SignalDefinition sig(event_name);
        sig.description = std::move(desc);
        signals.push_back(std::move(sig));
    }

    [[nodiscard]] std::vector<std::string> get_event_names() const {
        std::vector<std::string> names;
        names.reserve(signals.size());
        for (const auto& sig : signals) {
            names.push_back(sig.name);
        }
        return names;
    }

    [[nodiscard]] std::vector<EventModel> get_events() const {
        std::vector<EventModel> evs;
        evs.reserve(signals.size());
        for (const auto& sig : signals) {
            evs.emplace_back(sig.name, sig.description);
        }
        return evs;
    }

    void add_guard(const std::string& guard_name, std::string desc = "",
                   std::optional<std::string> raw_expr = std::nullopt,
                   std::optional<std::string> cpp_expr = std::nullopt) {
        if (guard_name.empty())
            return;
        for (auto& g : guards) {
            if (g.name == guard_name) {
                if (raw_expr.has_value() && !g.raw_expression.has_value()) {
                    g.raw_expression = raw_expr;
                }
                if (cpp_expr.has_value() && !g.cpp_expression.has_value()) {
                    g.cpp_expression = cpp_expr;
                }
                return;
            }
        }
        guards.emplace_back(guard_name, std::move(desc), std::move(raw_expr), std::move(cpp_expr));
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

    /**
     * @brief Automatically synthesizes and synchronizes canonical Guard and Action interfaces
     * from all transition edges and state entry/exit actions across the graph.
     */
    void sync_interfaces() {
        auto is_valid_ident = [](std::string_view s) {
            if (s.empty()) return false;
            if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
            for (char c : s) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
            }
            return true;
        };

        // 1. Synchronize guards
        for (const auto& tr : transitions) {
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
                    if (is_valid_ident(tok) && tok != "true" && tok != "false" && tok != "else" &&
                        tok != "otherwise" && tok != "default" && tok != "fsm" && tok != "and_" &&
                        tok != "or_" && tok != "not_") {
                        bool found = false;
                        for (const auto& existing : guards) {
                            if (existing.name == tok) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            guards.emplace_back(tok, "", tok, tok);
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

        // 2. Synchronize actions from transitions
        for (const auto& tr : transitions) {
            std::string a_name = tr.get_action();
            if (!a_name.empty()) {
                bool found = false;
                for (const auto& existing : actions) {
                    if (existing.name == a_name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    actions.emplace_back(a_name);
                }
            }
        }

        // 3. Synchronize actions from state entry and exit actions
        for (const auto& st : states) {
            for (const auto& act : st.entry_actions) {
                if (!act.name.empty()) {
                    bool found = false;
                    for (const auto& existing : actions) {
                        if (existing.name == act.name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        actions.emplace_back(act.name);
                    }
                }
            }
            for (const auto& act : st.exit_actions) {
                if (!act.name.empty()) {
                    bool found = false;
                    for (const auto& existing : actions) {
                        if (existing.name == act.name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        actions.emplace_back(act.name);
                    }
                }
            }
        }
    }

    /**
     * @brief Rebuilds contiguous transition adjacency lists on each StateNode for O(1) graph traversal.
     */
    void rebuild_adjacency_indices() {
        for (auto& st : states) {
            st.outgoing_transitions.clear();
            st.incoming_transitions.clear();
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(transitions.size()); ++i) {
            const auto& tr = transitions[i];
            if (!tr.source.empty()) {
                if (auto* src_node = find_state(tr.source)) {
                    src_node->outgoing_transitions.push_back(i);
                }
            }
            for (const auto& s_id : tr.source_ids) {
                if (s_id != tr.source) {
                    if (auto* src_node = find_state(s_id)) {
                        if (std::find(src_node->outgoing_transitions.begin(),
                                      src_node->outgoing_transitions.end(), i) ==
                            src_node->outgoing_transitions.end()) {
                            src_node->outgoing_transitions.push_back(i);
                        }
                    }
                }
            }
            if (!tr.target.empty()) {
                if (auto* dst_node = find_state(tr.target)) {
                    dst_node->incoming_transitions.push_back(i);
                }
            }
            for (const auto& t_id : tr.target_ids) {
                if (t_id != tr.target) {
                    if (auto* dst_node = find_state(t_id)) {
                        if (std::find(dst_node->incoming_transitions.begin(),
                                      dst_node->incoming_transitions.end(), i) ==
                            dst_node->incoming_transitions.end()) {
                            dst_node->incoming_transitions.push_back(i);
                        }
                    }
                }
            }
        }
    }

    // Recomputes deterministic IDs and sorts containers canonically
    void canonicalize() {
        if (id.empty()) {
            id = compute_deterministic_id(name + (package.empty() ? "" : "_" + package));
        }
        if (initial_state_id.empty() && !initial_state.empty()) {
            initial_state_id = initial_state;
        }
        // Synchronize transitions: guard <-> guard_ast, action <-> action_sig
        for (auto& tr : transitions) {
            if (tr.guard_ast.has_value() && (!tr.guard.has_value() || *tr.guard != tr.guard_ast->to_string())) {
                tr.guard = tr.guard_ast->to_string();
            } else if (!tr.guard_ast.has_value() && tr.guard.has_value() && !tr.guard->empty()) {
                tr.guard_ast = GuardAstNode(*tr.guard);
            }
            if (tr.action_sig.has_value() && (!tr.action.has_value() || *tr.action != tr.action_sig->name)) {
                tr.action = tr.action_sig->name;
            } else if (!tr.action_sig.has_value() && tr.action.has_value() && !tr.action->empty()) {
                tr.action_sig = ActionSignature(*tr.action, *tr.action);
            }
        }
        // Sort states by FQN for deterministic canonical order
        std::sort(states.begin(), states.end(), [](const StateNode& a, const StateNode& b) { return a.fqn < b.fqn; });
        // Sort ports by name
        std::sort(ports.begin(), ports.end(),
                  [](const PortDefinition& a, const PortDefinition& b) { return a.name < b.name; });
        // Sort signals by name
        std::sort(signals.begin(), signals.end(),
                  [](const SignalDefinition& a, const SignalDefinition& b) { return a.name < b.name; });
        // Sort variables by name
        std::sort(variables.begin(), variables.end(),
                  [](const VariableDefinition& a, const VariableDefinition& b) { return a.name < b.name; });
        // Sort custom types by name (single source of truth)
        std::sort(custom_types.begin(), custom_types.end(),
                  [](const TypeDefinition& a, const TypeDefinition& b) { return a.name < b.name; });
        // Sort properties by name
        std::sort(properties.begin(), properties.end(),
                  [](const FormalProperty& a, const FormalProperty& b) { return a.name < b.name; });
        // Synchronize interfaces (guards and actions)
        sync_interfaces();
        std::sort(guards.begin(), guards.end(),
                  [](const GuardModel& a, const GuardModel& b) { return a.name < b.name; });
        std::sort(actions.begin(), actions.end(),
                  [](const ActionModel& a, const ActionModel& b) { return a.name < b.name; });
        // Rebuild adjacency indices on StateNode
        rebuild_adjacency_indices();
    }

    /**
     * @brief Performs structural well-formedness verification on the IR graph.
     *
     * Validates graph invariants:
     * - Initial state is valid and exists in states if specified.
     * - All transitions refer to valid source and target states.
     *
     * Note: Semantic type resolution, datapath checking, and action validation
     * are decoupled into the Middle-End (fsm::middleend::SemanticAnalyzer / SemanticValidationPass).
     *
     * @param error Output error message if the IR is structurally malformed.
     * @return True if structurally consistent, false otherwise.
     */
    [[nodiscard]] bool is_well_formed(std::string& error) const noexcept {
        if (!initial_state.empty() && find_state(initial_state) == nullptr) {
            error = "Initial state '" + initial_state + "' not found in states";
            return false;
        }
        for (const auto& tr : transitions) {
            if (!tr.source.empty() && find_state(tr.source) == nullptr) {
                error = "Transition source state '" + tr.source + "' not found in states";
                return false;
            }
            if (!tr.target.empty() && find_state(tr.target) == nullptr) {
                error = "Transition target state '" + tr.target + "' not found in states";
                return false;
            }
        }
        return true;
    }

    bool operator==(const FsmIr& other) const noexcept {
        return name == other.name && package == other.package && initial_state_id == other.initial_state_id &&
               satisfies_reqs == other.satisfies_reqs && attributes == other.attributes && states == other.states &&
               transitions == other.transitions && ports == other.ports && signals == other.signals &&
               variables == other.variables && custom_types == other.custom_types && properties == other.properties;
    }
};

}  // namespace fsm::ir

namespace fsm {
using namespace ir;
}  // namespace fsm

