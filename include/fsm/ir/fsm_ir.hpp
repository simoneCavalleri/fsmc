/**
 * @file fsm_ir.hpp
 * @brief Canonical Intermediate Representation (IR) Root Metamodel for Finite State Machines.
 */

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
#include "fsm/ir/clock_definition.hpp"
#include "fsm/ir/concurrency_semantics.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/enum_definition.hpp"
#include "fsm/ir/event_model.hpp"
#include "fsm/ir/expression.hpp"
#include "fsm/ir/formal_property.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/port_definition.hpp"
#include "fsm/ir/region.hpp"
#include "fsm/ir/signal_definition.hpp"
#include "fsm/ir/state_kind.hpp"
#include "fsm/ir/state_node.hpp"
#include "fsm/ir/struct_definition.hpp"
#include "fsm/ir/transition_edge.hpp"
#include "fsm/ir/transition_edge_kind.hpp"
#include "fsm/ir/trigger.hpp"
#include "fsm/ir/type_definition.hpp"
#include "fsm/ir/variable_definition.hpp"

namespace fsm::ir {

/**
 * @brief Canonical Intermediate Representation (IR) Root Metamodel for Finite State Machines.
 *
 * `FsmIr` serves as the universal abstract syntax tree (AST) exchanged across the compiler pipeline:
 * - Ingested by Frontend Parsers (`include/fsm/frontend/`)
 * - Transformed and Verified by Middle-End Optimization/SMT Passes (`include/fsm/middleend/`)
 * - Serialized by Backend Code Generators and Diagram Transpilers (`include/fsm/backend/`)
 */
struct FsmIr {
    std::string id;                                           ///< Unique deterministic identifier
    std::string name = "MyStateMachine";                      ///< State machine class name
    std::string package = "";                                 ///< Logical package / module / model namespace
    std::string initial_state;                                ///< Initial state unqualified name
    std::string initial_state_id;                             ///< Initial state deterministic ID
    ConcurrencySemantics concurrency{};                       ///< Mathematical concurrency / dispatch semantics
    ExecutionSemantics execution_semantics{};                 ///< Formal execution semantics & preemption policy
    std::vector<std::string> satisfies_reqs;                  ///< Requirement traceability IDs
    std::unordered_map<std::string, std::string> attributes;  ///< Target-agnostic metadata attributes

    std::vector<StateNode> states;              ///< Hierarchy of state nodes (simple, composite, parallel)
    std::vector<TransitionEdge> transitions;    ///< Directed transition edges with triggers, guards, actions
    std::vector<PortDefinition> ports;          ///< Typed InPorts / OutPorts with range contracts
    std::vector<SignalDefinition> signals;      ///< MBSE typed signal definitions
    std::vector<ClockDefinition> clocks;        ///< Continuous Timed Automata clocks
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
    [[nodiscard]] const StateNode* find_state_by_id(std::string_view state_id) const noexcept;

    /**
     * @brief Finds a mutable state node by its deterministic unique identifier.
     */
    [[nodiscard]] StateNode* find_state_by_id(std::string_view state_id) noexcept;

    /**
     * @brief Finds a state node by its unqualified local name.
     * @param state_name Name of the state.
     */
    [[nodiscard]] const StateNode* find_state_by_name(std::string_view state_name) const noexcept;

    /**
     * @brief Finds a mutable state node by its unqualified local name.
     */
    [[nodiscard]] StateNode* find_state_by_name(std::string_view state_name) noexcept;

    /**
     * @brief Finds a state node by its fully qualified name (FQN).
     */
    [[nodiscard]] const StateNode* find_state_by_fqn(std::string_view state_fqn) const noexcept;

    /**
     * @brief Finds a mutable state node by its fully qualified name (FQN).
     */
    [[nodiscard]] StateNode* find_state_by_fqn(std::string_view state_fqn) noexcept;

    /**
     * @brief Convenience lookup for a state by name.
     */
    [[nodiscard]] const StateNode* find_state(std::string_view state_name) const noexcept;

    /**
     * @brief Convenience mutable lookup for a state by name.
     */
    [[nodiscard]] StateNode* find_state(std::string_view state_name) noexcept;

    /**
     * @brief Convenience mutable lookup for a state by name.
     */
    [[nodiscard]] StateNode* find_state_mut(std::string_view state_name) noexcept;

    /**
     * @brief Finds an I/O port definition by its name.
     */
    [[nodiscard]] const PortDefinition* find_port(std::string_view port_name) const noexcept;

    /**
     * @brief Finds a mutable I/O port definition by its name.
     */
    [[nodiscard]] PortDefinition* find_port_mut(std::string_view port_name) noexcept;

    /**
     * @brief Returns a filtered list of all input ports.
     */
    [[nodiscard]] std::vector<PortDefinition> get_in_ports() const;

    /**
     * @brief Returns a filtered list of all output ports.
     */
    [[nodiscard]] std::vector<PortDefinition> get_out_ports() const;

    /**
     * @brief Finds a signal definition by its name.
     */
    [[nodiscard]] const SignalDefinition* find_signal(std::string_view sig_name) const noexcept;

    /**
     * @brief Finds a datapath state variable by its name.
     */
    [[nodiscard]] const VariableDefinition* find_variable(std::string_view var_name) const noexcept;

    /**
     * @brief Returns all registered enumeration types.
     */
    [[nodiscard]] std::vector<TypeDefinition> get_enums() const;

    /**
     * @brief Returns all registered structure types.
     */
    [[nodiscard]] std::vector<TypeDefinition> get_structs() const;

    /**
     * @brief Returns all registered type aliases.
     */
    [[nodiscard]] std::vector<TypeDefinition> get_aliases() const;

    /**
     * @brief Finds a user-defined custom type by name.
     */
    [[nodiscard]] const TypeDefinition* find_type(std::string_view type_name) const noexcept;

    /**
     * @brief Finds a mutable user-defined custom type by name.
     */
    [[nodiscard]] TypeDefinition* find_type_mut(std::string_view type_name) noexcept;

    /**
     * @brief Finds an enumeration custom type by name.
     */
    [[nodiscard]] const TypeDefinition* find_enum(std::string_view enum_name) const noexcept;

    /**
     * @brief Finds a mutable enumeration custom type by name.
     */
    [[nodiscard]] TypeDefinition* find_enum_mut(std::string_view enum_name) noexcept;

    /**
     * @brief Finds a structure custom type by name.
     */
    [[nodiscard]] const TypeDefinition* find_struct(std::string_view struct_name) const noexcept;

    /**
     * @brief Finds a mutable structure custom type by name.
     */
    [[nodiscard]] TypeDefinition* find_struct_mut(std::string_view struct_name) noexcept;

    /**
     * @brief Checks whether a custom type exists.
     */
    [[nodiscard]] bool has_type(std::string_view type_name) const noexcept;

    /**
     * @brief Finds a formal verification property by name.
     */
    [[nodiscard]] const FormalProperty* find_property(std::string_view prop_name) const noexcept;

    /**
     * @brief Returns a list of all signal/event names in the model.
     */
    [[nodiscard]] std::vector<std::string> get_event_names() const;

    /**
     * @brief Returns event models for all signals in the model.
     */
    [[nodiscard]] std::vector<EventModel> get_events() const;

    /**
     * @brief Checks whether a given node name corresponds to a Choice pseudostate.
     */
    [[nodiscard]] bool is_choice_node(const std::string& node_name) const noexcept;

    // ========================================================================
    // AST Builders and Mutators
    // ========================================================================

    /**
     * @brief Adds or replaces a custom compound type definition in the model.
     */
    void add_type(TypeDefinition type);

    /**
     * @brief Retrieves an existing state or creates a new one linked to parent.
     */
    StateNode& add_or_get_state(const std::string& state_name, const std::string& parent_fqn = "",
                                StateKind kind = StateKind::Atomic);

    /**
     * @brief Adds or gets a state node.
     */
    StateNode& add_state(const std::string& state_name, const std::string& parent = "",
                         StateKind kind = StateKind::Atomic);

    /**
     * @brief Adds or replaces a state node by value.
     */
    StateNode& add_state(StateNode node);

    /**
     * @brief Adds or replaces a port definition.
     */
    void add_port(PortDefinition port);

    /**
     * @brief Adds or replaces a signal definition.
     */
    void add_signal(SignalDefinition sig);

    /**
     * @brief Adds or replaces an internal state variable.
     */
    void add_variable(VariableDefinition var);

    /**
     * @brief Adds an enumeration type definition.
     */
    void add_enum(EnumDefinition def);

    /**
     * @brief Adds a structure type definition.
     */
    void add_struct(StructDefinition def);

    /**
     * @brief Adds a formal verification property.
     */
    void add_property(FormalProperty prop);

    /**
     * @brief Registers an event name and optional description.
     */
    void add_event(const std::string& event_name, std::string desc = "");

    /**
     * @brief Adds or updates a guard model with optional expressions.
     */
    void add_guard(const std::string& guard_name, std::string desc = "",
                   std::optional<std::string> raw_expr = std::nullopt, std::optional<std::string> expr = std::nullopt);

    /**
     * @brief Registers an action name.
     */
    void add_action(const std::string& action_name);

    /**
     * @brief Registers a choice pseudostate name.
     */
    void add_choice_node(const std::string& choice_name);

    /**
     * @brief Appends a transition edge to the model.
     */
    void add_transition(TransitionEdge edge);

    /**
     * @brief Creates and appends a transition edge with deterministic ID computation.
     */
    TransitionEdge& add_transition(const std::string& src_id, const std::string& dst_id, TriggerVariant trigger,
                                   std::optional<GuardAstNode> guard = std::nullopt,
                                   std::optional<ActionSignature> trans_action = std::nullopt,
                                   TransitionEdgeKind edge_kind = TransitionEdgeKind::External,
                                   std::optional<ActionSignature> cond_action = std::nullopt);

    // ========================================================================
    // Graph Operations & Canonicalization Algorithms (delegated to FsmGraphOps)
    // ========================================================================

    void normalize_hierarchy();
    void sync_interfaces();
    void rebuild_adjacency_indices();
    void sort_transitions_by_priority();
    void canonicalize();
    [[nodiscard]] bool is_well_formed(std::string& error) const noexcept;

    bool operator==(const FsmIr& other) const noexcept {
        return name == other.name && package == other.package && initial_state_id == other.initial_state_id &&
               concurrency == other.concurrency && satisfies_reqs == other.satisfies_reqs &&
               attributes == other.attributes && states == other.states && transitions == other.transitions &&
               ports == other.ports && signals == other.signals && variables == other.variables &&
               custom_types == other.custom_types && properties == other.properties;
    }
};

}  // namespace fsm::ir
