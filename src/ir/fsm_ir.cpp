#include "fsm/ir/fsm_ir.hpp"

#include "fsm/ir/fsm_graph_ops.hpp"

namespace fsm::ir {

const StateNode* FsmIr::find_state_by_id(std::string_view state_id) const noexcept {
    for (const auto& s : states) {
        if (s.id == state_id)
            return &s;
    }
    return nullptr;
}

StateNode* FsmIr::find_state_by_id(std::string_view state_id) noexcept {
    for (auto& s : states) {
        if (s.id == state_id)
            return &s;
    }
    return nullptr;
}

const StateNode* FsmIr::find_state_by_name(std::string_view state_name) const noexcept {
    for (const auto& s : states) {
        if (s.name == state_name)
            return &s;
    }
    return nullptr;
}

StateNode* FsmIr::find_state_by_name(std::string_view state_name) noexcept {
    for (auto& s : states) {
        if (s.name == state_name)
            return &s;
    }
    return nullptr;
}

const StateNode* FsmIr::find_state_by_fqn(std::string_view state_fqn) const noexcept {
    for (const auto& s : states) {
        if (s.fqn == state_fqn)
            return &s;
    }
    return nullptr;
}

StateNode* FsmIr::find_state_by_fqn(std::string_view state_fqn) noexcept {
    for (auto& s : states) {
        if (s.fqn == state_fqn)
            return &s;
    }
    return nullptr;
}

const StateNode* FsmIr::find_state(std::string_view state_name) const noexcept {
    return find_state_by_name(state_name);
}

StateNode* FsmIr::find_state(std::string_view state_name) noexcept {
    return find_state_by_name(state_name);
}

StateNode* FsmIr::find_state_mut(std::string_view state_name) noexcept {
    return find_state_by_name(state_name);
}

const PortDefinition* FsmIr::find_port(std::string_view port_name) const noexcept {
    for (const auto& port : ports) {
        if (port.name == port_name)
            return &port;
    }
    return nullptr;
}

PortDefinition* FsmIr::find_port_mut(std::string_view port_name) noexcept {
    for (auto& port : ports) {
        if (port.name == port_name)
            return &port;
    }
    return nullptr;
}

std::vector<PortDefinition> FsmIr::get_in_ports() const {
    std::vector<PortDefinition> res;
    for (const auto& p : ports) {
        if (p.is_in())
            res.push_back(p);
    }
    return res;
}

std::vector<PortDefinition> FsmIr::get_out_ports() const {
    std::vector<PortDefinition> res;
    for (const auto& p : ports) {
        if (p.is_out())
            res.push_back(p);
    }
    return res;
}

const SignalDefinition* FsmIr::find_signal(std::string_view sig_name) const noexcept {
    for (const auto& sig : signals) {
        if (sig.name == sig_name)
            return &sig;
    }
    return nullptr;
}

const VariableDefinition* FsmIr::find_variable(std::string_view var_name) const noexcept {
    for (const auto& var : variables) {
        if (var.name == var_name)
            return &var;
    }
    return nullptr;
}

std::vector<TypeDefinition> FsmIr::get_enums() const {
    std::vector<TypeDefinition> res;
    for (const auto& ct : custom_types) {
        if (ct.is_enum())
            res.push_back(ct);
    }
    return res;
}

std::vector<TypeDefinition> FsmIr::get_structs() const {
    std::vector<TypeDefinition> res;
    for (const auto& ct : custom_types) {
        if (ct.is_struct())
            res.push_back(ct);
    }
    return res;
}

std::vector<TypeDefinition> FsmIr::get_aliases() const {
    std::vector<TypeDefinition> res;
    for (const auto& ct : custom_types) {
        if (ct.is_alias())
            res.push_back(ct);
    }
    return res;
}

const TypeDefinition* FsmIr::find_type(std::string_view type_name) const noexcept {
    for (const auto& t : custom_types) {
        if (t.name == type_name)
            return &t;
    }
    return nullptr;
}

TypeDefinition* FsmIr::find_type_mut(std::string_view type_name) noexcept {
    for (auto& t : custom_types) {
        if (t.name == type_name)
            return &t;
    }
    return nullptr;
}

const TypeDefinition* FsmIr::find_enum(std::string_view enum_name) const noexcept {
    const auto* t = find_type(enum_name);
    return (t != nullptr && t->is_enum()) ? t : nullptr;
}

TypeDefinition* FsmIr::find_enum_mut(std::string_view enum_name) noexcept {
    auto* t = find_type_mut(enum_name);
    return (t != nullptr && t->is_enum()) ? t : nullptr;
}

const TypeDefinition* FsmIr::find_struct(std::string_view struct_name) const noexcept {
    const auto* t = find_type(struct_name);
    return (t != nullptr && t->is_struct()) ? t : nullptr;
}

TypeDefinition* FsmIr::find_struct_mut(std::string_view struct_name) noexcept {
    auto* t = find_type_mut(struct_name);
    return (t != nullptr && t->is_struct()) ? t : nullptr;
}

bool FsmIr::has_type(std::string_view type_name) const noexcept {
    return find_type(type_name) != nullptr;
}

const FormalProperty* FsmIr::find_property(std::string_view prop_name) const noexcept {
    for (const auto& prop : properties) {
        if (prop.name == prop_name)
            return &prop;
    }
    return nullptr;
}

std::vector<std::string> FsmIr::get_event_names() const {
    std::vector<std::string> names;
    names.reserve(signals.size());
    for (const auto& sig : signals) {
        names.push_back(sig.name);
    }
    return names;
}

std::vector<EventModel> FsmIr::get_events() const {
    std::vector<EventModel> evs;
    evs.reserve(signals.size());
    for (const auto& sig : signals) {
        evs.emplace_back(sig.name, sig.description);
    }
    return evs;
}

bool FsmIr::is_choice_node(const std::string& node_name) const noexcept {
    for (const auto& c : choice_nodes) {
        if (c.name == node_name)
            return true;
    }
    const auto* s = find_state_by_name(node_name);
    return s != nullptr && s->kind == StateKind::Choice;
}

void FsmIr::add_type(TypeDefinition type) {
    for (auto& existing : custom_types) {
        if (existing.name == type.name) {
            existing = std::move(type);
            return;
        }
    }
    custom_types.push_back(std::move(type));
}

StateNode& FsmIr::add_or_get_state(const std::string& state_name, const std::string& parent_fqn, StateKind kind) {
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

StateNode& FsmIr::add_state(const std::string& state_name, const std::string& parent, StateKind kind) {
    return add_or_get_state(state_name, parent, kind);
}

StateNode& FsmIr::add_state(StateNode node) {
    for (auto& existing : states) {
        if (existing.name == node.name) {
            existing = std::move(node);
            return existing;
        }
    }
    states.push_back(std::move(node));
    return states.back();
}

void FsmIr::add_port(PortDefinition port) {
    for (auto& existing : ports) {
        if (existing.name == port.name) {
            existing = std::move(port);
            return;
        }
    }
    ports.push_back(std::move(port));
}

void FsmIr::add_signal(SignalDefinition sig) {
    for (auto& existing : signals) {
        if (existing.name == sig.name) {
            existing = std::move(sig);
            return;
        }
    }
    signals.push_back(std::move(sig));
}

void FsmIr::add_variable(VariableDefinition var) {
    for (auto& existing : variables) {
        if (existing.name == var.name) {
            existing = std::move(var);
            return;
        }
    }
    variables.push_back(std::move(var));
}

void FsmIr::add_enum(EnumDefinition def) {
    add_type(TypeDefinition(std::move(def)));
}

void FsmIr::add_struct(StructDefinition def) {
    add_type(TypeDefinition(std::move(def)));
}

void FsmIr::add_property(FormalProperty prop) {
    for (auto& existing : properties) {
        if (existing.name == prop.name) {
            existing = std::move(prop);
            return;
        }
    }
    properties.push_back(std::move(prop));
}

void FsmIr::add_event(const std::string& event_name, std::string desc) {
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

void FsmIr::add_guard(const std::string& guard_name, std::string desc, std::optional<std::string> raw_expr,
                      std::optional<std::string> expr) {
    if (guard_name.empty())
        return;
    for (auto& g : guards) {
        if (g.name == guard_name) {
            if (raw_expr.has_value() && !g.raw_expression.has_value()) {
                g.raw_expression = raw_expr;
            }
            if (expr.has_value() && !g.normalized_expression.has_value()) {
                g.normalized_expression = expr;
            }
            return;
        }
    }
    guards.emplace_back(guard_name, std::move(desc), std::move(raw_expr), std::move(expr));
}

void FsmIr::add_action(const std::string& action_name) {
    if (action_name.empty())
        return;
    for (const auto& a : actions) {
        if (a.name == action_name)
            return;
    }
    actions.emplace_back(action_name);
}

void FsmIr::add_choice_node(const std::string& choice_name) {
    if (choice_name.empty())
        return;
    for (const auto& c : choice_nodes) {
        if (c.name == choice_name)
            return;
    }
    choice_nodes.emplace_back(choice_name);
}

void FsmIr::add_transition(TransitionEdge edge) {
    transitions.push_back(std::move(edge));
}

TransitionEdge& FsmIr::add_transition(const std::string& src_id, const std::string& dst_id, TriggerVariant trigger,
                                      std::optional<GuardAstNode> guard, std::optional<ActionSignature> trans_action,
                                      TransitionEdgeKind edge_kind, std::optional<ActionSignature> cond_action) {
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
    if (trans_action.has_value()) {
        edge.transition_action = std::move(trans_action);
    }
    if (cond_action.has_value()) {
        edge.condition_action = std::move(cond_action);
    }
    edge.kind = edge_kind;
    transitions.push_back(std::move(edge));
    return transitions.back();
}

void FsmIr::normalize_hierarchy() {
    FsmGraphOps::normalize_hierarchy(*this);
}

void FsmIr::sync_interfaces() {
    FsmGraphOps::sync_interfaces(*this);
}

void FsmIr::rebuild_adjacency_indices() {
    FsmGraphOps::rebuild_adjacency_indices(*this);
}

void FsmIr::sort_transitions_by_priority() {
    FsmGraphOps::sort_transitions_by_priority(*this);
}

void FsmIr::canonicalize() {
    FsmGraphOps::canonicalize(*this);
}

bool FsmIr::is_well_formed(std::string& error) const noexcept {
    return FsmGraphOps::is_well_formed(*this, error);
}

}  // namespace fsm::ir
