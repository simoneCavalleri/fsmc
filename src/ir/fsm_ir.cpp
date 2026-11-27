#include "fsm/ir/fsm_ir.hpp"

#include "fsm/ir/fsm_graph_ops.hpp"

namespace fsm::ir {

void FsmIr::invalidate_state_index() const noexcept {
    state_index_valid_ = false;
    indexed_state_count_ = 0;
}

void FsmIr::rebuild_state_index() const noexcept {
    try {
        state_name_index_.clear();
        state_id_index_.clear();
        state_name_index_.reserve(states.size());
        state_id_index_.reserve(states.size());
        for (std::size_t i = 0; i < states.size(); ++i) {
            if (!states[i].name.empty()) {
                state_name_index_.try_emplace(states[i].name, i);
            }
            if (!states[i].id.empty()) {
                state_id_index_.try_emplace(states[i].id, i);
            }
        }
        indexed_state_count_ = states.size();
        state_index_valid_ = true;
    } catch (...) {
        state_name_index_.clear();
        state_id_index_.clear();
        indexed_state_count_ = 0;
        state_index_valid_ = false;
    }
}

void FsmIr::ensure_state_index() const noexcept {
    if (!state_index_valid_ || indexed_state_count_ != states.size()) {
        rebuild_state_index();
    }
}

const StateNode* FsmIr::find_state_by_id(std::string_view state_id) const noexcept {
    if (state_id.empty()) {
        return nullptr;
    }
    ensure_state_index();
    if (state_index_valid_) {
        if (auto it = state_id_index_.find(state_id); it != state_id_index_.end()) {
            const std::size_t idx = it->second;
            if (idx < states.size() && states[idx].id == state_id) {
                return &states[idx];
            }
            rebuild_state_index();
            if (auto it2 = state_id_index_.find(state_id);
                it2 != state_id_index_.end() && it2->second < states.size()) {
                return &states[it2->second];
            }
            return nullptr;
        }
        // Verify cache wasn't invalidated by in-place id mutation without size change
        for (const auto& s : states) {
            if (s.id == state_id) {
                rebuild_state_index();
                return &s;
            }
        }
        return nullptr;
    }
    for (const auto& s : states) {
        if (s.id == state_id)
            return &s;
    }
    return nullptr;
}

StateNode* FsmIr::find_state_by_id(std::string_view state_id) noexcept {
    return const_cast<StateNode*>(static_cast<const FsmIr*>(this)->find_state_by_id(state_id));
}

const StateNode* FsmIr::find_state_by_name(std::string_view state_name) const noexcept {
    if (state_name.empty()) {
        return nullptr;
    }
    ensure_state_index();
    if (state_index_valid_) {
        if (auto it = state_name_index_.find(state_name); it != state_name_index_.end()) {
            const std::size_t idx = it->second;
            if (idx < states.size() && states[idx].name == state_name) {
                return &states[idx];
            }
            rebuild_state_index();
            if (auto it2 = state_name_index_.find(state_name);
                it2 != state_name_index_.end() && it2->second < states.size()) {
                return &states[it2->second];
            }
            return nullptr;
        }
        for (const auto& s : states) {
            if (s.name == state_name) {
                rebuild_state_index();
                return &s;
            }
        }
        return nullptr;
    }
    for (const auto& s : states) {
        if (s.name == state_name)
            return &s;
    }
    return nullptr;
}

StateNode* FsmIr::find_state_by_name(std::string_view state_name) noexcept {
    return const_cast<StateNode*>(static_cast<const FsmIr*>(this)->find_state_by_name(state_name));
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

const GuardModel* FsmIr::find_guard(std::string_view guard_name) const noexcept {
    for (const auto& gm : guards) {
        if (gm.name == guard_name) {
            return &gm;
        }
    }
    return nullptr;
}

std::string FsmIr::resolve_guard_expression(std::string_view guard_name_or_expr) const {
    if (const auto* gm = find_guard(guard_name_or_expr); gm != nullptr) {
        if (gm->normalized_expression.has_value() && !gm->normalized_expression->empty()) {
            return *gm->normalized_expression;
        }
        if (gm->raw_expression.has_value() && !gm->raw_expression->empty()) {
            return *gm->raw_expression;
        }
    }
    return std::string(guard_name_or_expr);
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
            if (existing.kind == type.kind && type.kind == TypeKind::Struct) {
                if (type.description.empty() && !existing.description.empty()) {
                    type.description = existing.description;
                }
                for (auto& field : type.fields) {
                    for (const auto& old_field : existing.fields) {
                        if (old_field.name == field.name) {
                            if (field.default_value.empty() && !old_field.default_value.empty()) {
                                field.default_value = old_field.default_value;
                            }
                            if ((!field.physical_unit.has_value() || field.physical_unit->empty()) &&
                                old_field.physical_unit.has_value()) {
                                field.physical_unit = old_field.physical_unit;
                            }
                            if (!field.min_value.has_value() && old_field.min_value.has_value()) {
                                field.min_value = old_field.min_value;
                            }
                            if (!field.max_value.has_value() && old_field.max_value.has_value()) {
                                field.max_value = old_field.max_value;
                            }
                            if (field.description.empty() && !old_field.description.empty()) {
                                field.description = old_field.description;
                            }
                            break;
                        }
                    }
                }
            }
            existing = std::move(type);
            return;
        }
    }
    custom_types.push_back(std::move(type));
}

StateNode& FsmIr::add_or_get_state(const std::string& state_name, const std::string& parent_fqn, StateKind kind) {
    const std::string actual_parent = (parent_fqn == state_name) ? "" : parent_fqn;
    auto is_ancestor_of = [&](const std::string& candidate_ancestor, const std::string& descendant) -> bool {
        if (candidate_ancestor.empty() || descendant.empty()) {
            return false;
        }
        std::string cur = descendant;
        std::size_t depth = 0;
        while (!cur.empty() && depth++ < 64) {
            if (cur == candidate_ancestor) {
                return true;
            }
            const auto* st = find_state_by_name(cur);
            if (st == nullptr || st->parent_state.empty()) {
                break;
            }
            cur = st->parent_state;
        }
        return false;
    };
    for (auto& s : states) {
        if (s.name != state_name) {
            continue;
        }
        if (actual_parent.empty() || s.parent_state.empty() || s.parent_state == actual_parent ||
            s.fqn == actual_parent + "." + state_name) {
            if (!actual_parent.empty() && s.parent_state.empty()) {
                s.parent_state = actual_parent;
                auto* parent = find_state_by_fqn(actual_parent);
                if (parent == nullptr) {
                    parent = find_state_by_name(actual_parent);
                }
                if (parent != nullptr) {
                    s.fqn = parent->fqn + "." + state_name;
                    s.parent_id = parent->id;
                    parent->children_ids.push_back(s.id);
                    parent->is_composite = true;
                    if (parent->kind == StateKind::Atomic) {
                        parent->kind = StateKind::Composite;
                    }
                } else {
                    s.fqn = actual_parent + "." + state_name;
                }
            }
            return s;
        }
        // Existing state is already nested deeper inside actual_parent: reuse it without duplicating.
        if (is_ancestor_of(actual_parent, s.parent_state)) {
            return s;
        }
        // Existing state was previously referenced at an outer ancestor scope and is now explicitly nested deeper:
        // reparent it cleanly instead of creating a duplicate StateNode.
        if (is_ancestor_of(s.parent_state, actual_parent)) {
            if (auto* old_parent = find_state_by_name(s.parent_state)) {
                auto& ch = old_parent->children_ids;
                ch.erase(std::remove(ch.begin(), ch.end(), s.id), ch.end());
            }
            s.parent_state = actual_parent;
            auto* new_parent = find_state_by_fqn(actual_parent);
            if (new_parent == nullptr) {
                new_parent = find_state_by_name(actual_parent);
            }
            if (new_parent != nullptr) {
                s.fqn = new_parent->fqn + "." + state_name;
                s.parent_id = new_parent->id;
                new_parent->children_ids.push_back(s.id);
                new_parent->is_composite = true;
                if (new_parent->kind == StateKind::Atomic) {
                    new_parent->kind = StateKind::Composite;
                }
            } else {
                s.fqn = actual_parent + "." + state_name;
            }
            return s;
        }
    }
    const auto* existing_parent = actual_parent.empty() ? nullptr : find_state_by_fqn(actual_parent);
    if (existing_parent == nullptr && !actual_parent.empty()) {
        existing_parent = find_state_by_name(actual_parent);
    }
    std::string full_fqn = existing_parent != nullptr
                               ? (existing_parent->fqn + "." + state_name)
                               : (actual_parent.empty() ? state_name : (actual_parent + "." + state_name));
    std::string s_id = compute_deterministic_id(full_fqn);
    StateNode node(s_id, state_name, full_fqn, kind);
    node.parent_state = actual_parent;
    if (existing_parent != nullptr) {
        node.parent_id = existing_parent->id;
    }
    states.push_back(node);
    // Link child to parent
    if (!actual_parent.empty()) {
        auto* parent = find_state_by_fqn(actual_parent);
        if (parent == nullptr) {
            parent = find_state_by_name(actual_parent);
        }
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
    auto is_anon = [](const std::string& ev) {
        return ev.empty() || ev == "anonymous_event" || ev == "completion_event" || ev == "AnonymousEvent" ||
               ev == "anonymous" || ev == "none";
    };
    if (!edge.event.empty() && !is_anon(edge.event) && std::holds_alternative<AnonymousTrigger>(edge.trigger)) {
        edge.trigger = SignalTrigger{edge.event, "payload"};
    } else if (edge.event.empty() && std::holds_alternative<SignalTrigger>(edge.trigger)) {
        edge.event = std::get<SignalTrigger>(edge.trigger).signal_name;
    }
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
        const auto& t = std::get<TimeTrigger>(trigger);
        trig_str = std::to_string(t.duration_ms > 0 ? t.duration_ms : t.duration_in_ms());
    }
    std::string guard_str = guard.has_value() ? guard->to_string() : "";
    std::string canonical_sig = src_id + "->" + dst_id + ":" + trig_str + "[" + guard_str + "]";
    std::string edge_id = compute_deterministic_id(canonical_sig);

    TransitionEdge edge(edge_id, src_id, dst_id, std::move(trigger));
    if (std::holds_alternative<SignalTrigger>(edge.trigger)) {
        edge.event = std::get<SignalTrigger>(edge.trigger).signal_name;
    }
    if (guard.has_value()) {
        edge.set_guard_ast(std::move(guard));
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
