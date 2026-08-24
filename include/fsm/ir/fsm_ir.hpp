#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace fsm::codegen {

// ============================================================================
// Deterministic Hash Generator for IDs (Order-Invariant / Canonical)
// ============================================================================

inline std::string compute_deterministic_id(std::string_view canonical_str) {
    // 64-bit FNV-1a hash
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : canonical_str) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << "id_" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

// ============================================================================
// Enums & Primitive Descriptors
// ============================================================================

enum class StateKind : std::uint8_t {
    Atomic,
    Composite,
    Parallel,        // Orthogonal Region container
    Initial,         // Initial pseudostate
    Final,           // Final state
    ShallowHistory,  // [H]
    DeepHistory,     // [H*]
    Choice,          // Dynamic conditional branch pseudostate <<choice>>
    Junction,        // Static merge/branch pseudostate <<junction>>
    Fork,            // Parallel split pseudostate <<fork>>
    Join             // Parallel rendezvous pseudostate <<join>>
};

inline std::string state_kind_to_string(StateKind kind) {
    switch (kind) {
        case StateKind::Atomic:
            return "Atomic";
        case StateKind::Composite:
            return "Composite";
        case StateKind::Parallel:
            return "Parallel";
        case StateKind::Initial:
            return "Initial";
        case StateKind::Final:
            return "Final";
        case StateKind::ShallowHistory:
            return "ShallowHistory";
        case StateKind::DeepHistory:
            return "DeepHistory";
        case StateKind::Choice:
            return "Choice";
        case StateKind::Junction:
            return "Junction";
        case StateKind::Fork:
            return "Fork";
        case StateKind::Join:
            return "Join";
    }
    return "Atomic";
}

inline StateKind state_kind_from_string(std::string_view str) {
    if (str == "Composite")
        return StateKind::Composite;
    if (str == "Parallel")
        return StateKind::Parallel;
    if (str == "Initial")
        return StateKind::Initial;
    if (str == "Final")
        return StateKind::Final;
    if (str == "ShallowHistory")
        return StateKind::ShallowHistory;
    if (str == "DeepHistory")
        return StateKind::DeepHistory;
    if (str == "Choice")
        return StateKind::Choice;
    if (str == "Junction")
        return StateKind::Junction;
    if (str == "Fork")
        return StateKind::Fork;
    if (str == "Join")
        return StateKind::Join;
    return StateKind::Atomic;
}

enum class TransitionEdgeKind : std::uint8_t { External, Internal, Local };

inline std::string transition_edge_kind_to_string(TransitionEdgeKind kind) {
    switch (kind) {
        case TransitionEdgeKind::External:
            return "External";
        case TransitionEdgeKind::Internal:
            return "Internal";
        case TransitionEdgeKind::Local:
            return "Local";
    }
    return "External";
}

inline TransitionEdgeKind transition_edge_kind_from_string(std::string_view str) {
    if (str == "Internal")
        return TransitionEdgeKind::Internal;
    if (str == "Local")
        return TransitionEdgeKind::Local;
    return TransitionEdgeKind::External;
}

// ============================================================================
// Signal / Payload Definitions & Attributes
// ============================================================================

struct SignalAttribute {
    std::string name;
    std::string type;  // e.g. "uint32_t", "const uint8_t*", "std::string"
    std::string default_value;

    SignalAttribute() = default;
    SignalAttribute(std::string attr_name, std::string attr_type, std::string def_val = "")
        : name(std::move(attr_name)), type(std::move(attr_type)), default_value(std::move(def_val)) {}

    bool operator==(const SignalAttribute& other) const noexcept {
        return name == other.name && type == other.type && default_value == other.default_value;
    }
};

struct SignalDefinition {
    std::string name;
    std::vector<SignalAttribute> attributes;
    std::vector<std::string> validators;  // Predicates e.g. "len > 0", "ptr != nullptr"
    std::string description;

    SignalDefinition() = default;
    explicit SignalDefinition(std::string sig_name) : name(std::move(sig_name)) {}

    bool operator==(const SignalDefinition& other) const noexcept {
        return name == other.name && attributes == other.attributes && validators == other.validators &&
               description == other.description;
    }
};

// ============================================================================
// Action Signatures & Guard AST
// ============================================================================

struct ActionSignature {
    std::string name;
    std::string invocation;       // e.g. "ctx.on_data(payload)"
    bool accepts_event{false};    // Passes const Event&
    bool accepts_context{false};  // Passes Context&

    ActionSignature() = default;
    explicit ActionSignature(std::string act_name, std::string act_inv = "")
        : name(std::move(act_name)), invocation(std::move(act_inv)) {}

    bool operator==(const ActionSignature& other) const noexcept {
        return name == other.name && invocation == other.invocation && accepts_event == other.accepts_event &&
               accepts_context == other.accepts_context;
    }
};

enum class GuardOp : std::uint8_t { None, Not, And, Or };

/**
 * @brief AST node representing composable boolean guard logic (AND, OR, NOT, Atomic Predicates).
 */
struct GuardAstNode {
    GuardOp op{GuardOp::None};
    std::string expression;  ///< Raw predicate expression or guard struct name
    std::vector<GuardAstNode> children;

    GuardAstNode() = default;
    explicit GuardAstNode(std::string expr) : expression(std::move(expr)) {}
    GuardAstNode(GuardOp operation, std::vector<GuardAstNode> sub_nodes)
        : op(operation), children(std::move(sub_nodes)) {}

    [[nodiscard]] std::string to_string() const {
        if (op == GuardOp::None) {
            return expression;
        }
        if (op == GuardOp::Not) {
            if (!children.empty()) {
                return "!" + (children[0].op != GuardOp::None ? "(" + children[0].to_string() + ")"
                                                              : children[0].to_string());
            }
            return "!" + expression;
        }
        if (op == GuardOp::And || op == GuardOp::Or) {
            const std::string op_str = (op == GuardOp::And) ? " && " : " || ";
            std::string result;
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (i > 0)
                    result += op_str;
                const bool needs_parens = (children[i].op == GuardOp::Or && op == GuardOp::And);
                if (needs_parens)
                    result += "(";
                result += children[i].to_string();
                if (needs_parens)
                    result += ")";
            }
            return result;
        }
        return expression;
    }

    bool operator==(const GuardAstNode& other) const noexcept {
        return op == other.op && expression == other.expression && children == other.children;
    }
};

// ============================================================================
// Triggers (Signal, Timed, Anonymous)
// ============================================================================

enum class TriggerType : std::uint8_t { Signal, TimeAfter, TimeEvery, Anonymous };

struct SignalTrigger {
    std::string signal_name;
    std::string payload_binding;  // Name of payload argument, e.g. "payload"

    bool operator==(const SignalTrigger& other) const noexcept {
        return signal_name == other.signal_name && payload_binding == other.payload_binding;
    }
};

struct TimeTrigger {
    std::uint64_t duration_ms{0};
    bool periodic{false};  // true for every(ms), false for after(ms)

    bool operator==(const TimeTrigger& other) const noexcept {
        return duration_ms == other.duration_ms && periodic == other.periodic;
    }
};

struct AnonymousTrigger {
    bool operator==(const AnonymousTrigger& /*other*/) const noexcept { return true; }
};

using TriggerVariant = std::variant<SignalTrigger, TimeTrigger, AnonymousTrigger>;

// ============================================================================
// Orthogonal Region (for Parallel states)
// ============================================================================

struct OrthogonalRegion {
    std::string id;
    std::string name;
    std::string initial_state_id;
    std::vector<std::string> state_ids;

    bool operator==(const OrthogonalRegion& other) const noexcept {
        return id == other.id && name == other.name && initial_state_id == other.initial_state_id &&
               state_ids == other.state_ids;
    }
};

// ============================================================================
// StateNode in the Formal IR
// ============================================================================

/**
 * @brief Node representation of a state within the hierarchical state graph.
 */
struct StateNode {
    std::string id;                         ///< Deterministic FNV-1a unique hash
    std::string name;                       ///< Local state identifier
    std::string alias;                      ///< Optional display alias
    std::string fqn;                        ///< Fully qualified hierarchical path (e.g. "Operating.Running.Manual")
    StateKind kind{StateKind::Atomic};      ///< Structural state classification
    std::string parent_state;               ///< Immediate parent state name
    std::optional<std::string> parent_id;   ///< Parent state deterministic ID
    std::vector<std::string> children_ids;  ///< Ordered IDs of sub-states
    std::vector<OrthogonalRegion> orthogonal_regions;  ///< Parallel regions for orthogonal execution

    bool is_composite{false};
    std::string initial_sub_state;  ///< Default sub-state on hierarchical entry
    bool has_history{false};        ///< Shallow history pseudo-state [H]
    bool has_deep_history{false};   ///< Deep history pseudo-state [H*]

    std::vector<ActionSignature> entry_actions;  ///< Ordered entry action signatures
    std::vector<ActionSignature> exit_actions;   ///< Ordered exit action signatures
    std::optional<std::string> entry_action;
    std::optional<std::string> exit_action;
    std::optional<std::string> do_activity;  ///< Async background activity (e.g., coroutine/worker)

    std::vector<std::string> deferred_events;    ///< Events deferred while in this state
    std::vector<std::string> traceability_reqs;  ///< Traceability requirement tags (e.g., "REQ-SAFETY-01")
    std::string description;                     ///< Human-readable documentation comment

    StateNode() = default;
    explicit StateNode(std::string state_name, std::string state_desc = "", std::string parent = "")
        : name(std::move(state_name)), parent_state(std::move(parent)), description(std::move(state_desc)) {
        if (!name.empty()) {
            id = compute_deterministic_id(name);
            fqn = parent_state.empty() ? name : (parent_state + "." + name);
        }
    }
    StateNode(std::string state_id, std::string state_name, std::string state_fqn, StateKind state_kind)
        : id(std::move(state_id)), name(std::move(state_name)), fqn(std::move(state_fqn)), kind(state_kind) {}

    bool operator<(const StateNode& other) const noexcept { return name < other.name; }

    bool operator==(const StateNode& other) const noexcept {
        return id == other.id && name == other.name && fqn == other.fqn && kind == other.kind &&
               parent_id == other.parent_id && children_ids == other.children_ids &&
               orthogonal_regions == other.orthogonal_regions && entry_actions == other.entry_actions &&
               exit_actions == other.exit_actions && do_activity == other.do_activity &&
               deferred_events == other.deferred_events && traceability_reqs == other.traceability_reqs &&
               description == other.description;
    }
};

// ============================================================================
// TransitionEdge in the Formal IR
// ============================================================================

struct TransitionEdge {
    std::string id;
    std::string source;
    std::string target;
    std::string source_id;
    std::string target_id;
    std::string event;
    std::optional<std::string> guard;
    std::optional<std::string> action;
    TriggerVariant trigger{AnonymousTrigger{}};
    std::optional<GuardAstNode> guard_ast;
    std::optional<ActionSignature> action_sig;
    TransitionEdgeKind kind{TransitionEdgeKind::External};
    std::string description;
    bool target_is_history{false};
    bool target_is_deep_history{false};
    std::string parent_scope;

    TransitionEdge() = default;
    TransitionEdge(std::string src, std::string dst, std::string evt, std::optional<std::string> grd = std::nullopt,
                   std::optional<std::string> act = std::nullopt, std::string desc = "",
                   TransitionEdgeKind transition_kind = TransitionEdgeKind::External)
        : source(std::move(src)),
          target(std::move(dst)),
          source_id(source),
          target_id(target),
          event(std::move(evt)),
          guard(std::move(grd)),
          action(std::move(act)),
          kind(transition_kind),
          description(std::move(desc)) {
        id = compute_deterministic_id(source + "->" + target + ":" + event + "[" + (guard ? *guard : "") + "]");
        if (!event.empty()) {
            trigger = SignalTrigger{event, "payload"};
        }
        if (guard.has_value()) {
            guard_ast = GuardAstNode(*guard);
        }
        if (action.has_value()) {
            action_sig = ActionSignature(*action, *action);
        }
    }
    TransitionEdge(std::string edge_id, std::string src_id, std::string dst_id,
                   TriggerVariant edge_trigger = AnonymousTrigger{})
        : id(std::move(edge_id)),
          source(src_id),
          target(dst_id),
          source_id(std::move(src_id)),
          target_id(std::move(dst_id)),
          trigger(std::move(edge_trigger)) {
        event = get_trigger_name();
    }

    [[nodiscard]] std::string get_trigger_name() const {
        if (!event.empty())
            return event;
        if (std::holds_alternative<SignalTrigger>(trigger)) {
            return std::get<SignalTrigger>(trigger).signal_name;
        }
        if (std::holds_alternative<TimeTrigger>(trigger)) {
            const auto& t = std::get<TimeTrigger>(trigger);
            return (t.periodic ? "every_" : "after_") + std::to_string(t.duration_ms) + "ms";
        }
        return "";
    }

    bool operator==(const TransitionEdge& other) const noexcept {
        return id == other.id && source_id == other.source_id && target_id == other.target_id &&
               trigger == other.trigger && guard_ast == other.guard_ast && action_sig == other.action_sig &&
               kind == other.kind && description == other.description;
    }
};

// ============================================================================
// FsmIr: Strongly-Typed Formal Intermediate Representation Root
// ============================================================================

struct EventModel {
    std::string name;
    std::string description;

    explicit EventModel(std::string event_name = "", std::string event_desc = "")
        : name(std::move(event_name)), description(std::move(event_desc)) {}

    bool operator<(const EventModel& other) const noexcept { return name < other.name; }
};

struct GuardModel {
    std::string name;
    std::string description;

    explicit GuardModel(std::string guard_name = "", std::string guard_desc = "")
        : name(std::move(guard_name)), description(std::move(guard_desc)) {}

    bool operator<(const GuardModel& other) const noexcept { return name < other.name; }
};

struct ActionModel {
    std::string name;
    std::string description;

    explicit ActionModel(std::string action_name = "", std::string action_desc = "")
        : name(std::move(action_name)), description(std::move(action_desc)) {}

    bool operator<(const ActionModel& other) const noexcept { return name < other.name; }
};

struct ChoiceNodeModel {
    std::string name;
    std::vector<TransitionEdge> outgoing_branches;

    explicit ChoiceNodeModel(std::string choice_name = "") : name(std::move(choice_name)) {}
};

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

    StateNode& add_or_get_state(const std::string& state_name, const std::string& parent_fqn = "",
                                StateKind kind = StateKind::Atomic) {
        for (auto& s : states) {
            if (s.name == state_name) {
                if (!parent_fqn.empty() && s.parent_state.empty()) {
                    s.parent_state = parent_fqn;
                    s.fqn = parent_fqn;
                    s.fqn += '.';
                    s.fqn += state_name;
                    s.id = compute_deterministic_id(s.fqn);
                    if (const auto* p = find_state_by_name(parent_fqn)) {
                        s.parent_id = p->id;
                    }
                }
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

    void add_signal(SignalDefinition sig) {
        for (auto& existing : signals) {
            if (existing.name == sig.name) {
                existing = std::move(sig);
                return;
            }
        }
        signals.push_back(std::move(sig));
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

        // Recompute is_composite for all states
        for (auto& p : states) {
            bool has_children = false;
            for (const auto& c : states) {
                if (c.parent_state == p.name) {
                    has_children = true;
                    break;
                }
            }
            p.is_composite = has_children;
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
    }

    bool operator==(const FsmIr& other) const noexcept {
        return name == other.name && ns == other.ns && context_type == other.context_type &&
               initial_state_id == other.initial_state_id && thread_safe == other.thread_safe &&
               satisfies_reqs == other.satisfies_reqs && states == other.states && transitions == other.transitions &&
               signals == other.signals;
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
using SignalTrigger = fsm::codegen::SignalTrigger;
using TimeTrigger = fsm::codegen::TimeTrigger;
using AnonymousTrigger = fsm::codegen::AnonymousTrigger;
using TriggerType = fsm::codegen::TriggerType;
using GuardAstNode = fsm::codegen::GuardAstNode;
using GuardOp = fsm::codegen::GuardOp;
using ActionSignature = fsm::codegen::ActionSignature;
using OrthogonalRegion = fsm::codegen::OrthogonalRegion;
using fsm::codegen::compute_deterministic_id;
}  // namespace fsm
