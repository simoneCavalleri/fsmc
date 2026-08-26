#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class CppModelEmitter {
  public:
    static std::string get_effective_context_type(const FsmIr& model) {
        if (!model.variables.empty()) {
            return (!model.context_type.empty() && model.context_type != "no_context" &&
                    model.context_type != "fsm::no_context" && model.context_type != "void")
                       ? model.context_type
                       : (model.name + "Context");
        }
        if (!model.context_type.empty() && model.context_type != "no_context" &&
            model.context_type != "fsm::no_context" && model.context_type != "void") {
            return model.context_type;
        }
        return "fsm::no_context";
    }

    static void emit_context_definition(std::ostream& out, const FsmIr& model) {
        if (!model.variables.empty()) {
            std::string ctx_name = get_effective_context_type(model);
            out << "// ============================================================================\n";
            out << "// EFSM State Context (Auto-Generated from IR Variables)\n";
            out << "// ============================================================================\n\n";
            out << "struct " << ctx_name << " {\n";
            for (const auto& var : model.variables) {
                out << "    " << var.type << " " << var.name;
                if (!var.initial_value.empty()) {
                    out << "{" << var.initial_value << "}";
                }
                out << ";";
                if (!var.description.empty()) {
                    out << " // " << var.description;
                }
                out << "\n";
            }
            out << "};\n\n";
        } else if (!model.context_type.empty() && model.context_type != "no_context" &&
                   model.context_type != "fsm::no_context" && model.context_type != "void") {
            out << "// Forward declaration of Context (if not already defined)\n";
            out << "struct " << model.context_type << ";\n\n";
        }
    }

    static void emit_events(std::ostream& out, const FsmIr& model) {
        out << "// ============================================================================\n";
        out << "// Events & Signals\n";
        out << "// ============================================================================\n\n";

        // Collect all distinct event/signal names
        std::vector<std::string> emitted_events;

        // 1. Emit signals with typed payloads
        for (const auto& sig : model.signals) {
            if (sig.name.empty() || sig.name == "none" || sig.name == "Anonymous")
                continue;

            emitted_events.push_back(sig.name);
            out << "struct " << sig.name << " {\n";
            out << "    static constexpr std::string_view name = \"" << sig.name << "\";\n";

            if (!sig.attributes.empty()) {
                out << "\n    // Payload Attributes\n";
                for (const auto& attr : sig.attributes) {
                    out << "    " << attr.type << " " << attr.name;
                    if (!attr.default_value.empty()) {
                        out << "{" << attr.default_value << "}";
                    } else {
                        out << "{}";
                    }
                    out << ";\n";
                }

                // Default and parameterized constructors
                out << "\n    constexpr " << sig.name << "() = default;\n";
                out << "    constexpr explicit " << sig.name << "(";
                for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].type << " " << sig.attributes[i].name << "_";
                }
                out << ")\n        : ";
                for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].name << "(" << sig.attributes[i].name << "_)";
                }
                out << " {}\n";
            }

            if (!sig.validators.empty()) {
                out << "\n    [[nodiscard]] constexpr bool is_valid() const noexcept {\n";
                out << "        return ";
                for (std::size_t i = 0; i < sig.validators.size(); ++i) {
                    if (i > 0)
                        out << " && ";
                    out << "(" << sig.validators[i] << ")";
                }
                out << ";\n    }\n";
            }

            out << "};\n\n";
        }

        // 2. Emit regular events
        for (const auto& event_item : model.events) {
            if (event_item.name.empty() || event_item.name == "none" || event_item.name == "Anonymous")
                continue;
            if (std::find(emitted_events.begin(), emitted_events.end(), event_item.name) != emitted_events.end())
                continue;

            emitted_events.push_back(event_item.name);
            out << "struct " << event_item.name << " {\n";
            out << "    static constexpr std::string_view name = \"" << event_item.name << "\";\n";
            if (!event_item.description.empty()) {
                out << "    // Description: " << event_item.description << "\n";
            }
            out << "};\n\n";
        }

        // 3. Emit deferred events if not already declared
        for (const auto& state : model.states) {
            for (const auto& d_evt : state.deferred_events) {
                if (d_evt.empty() || d_evt == "none" || d_evt == "Anonymous")
                    continue;
                if (std::find(emitted_events.begin(), emitted_events.end(), d_evt) != emitted_events.end())
                    continue;

                emitted_events.push_back(d_evt);
                out << "struct " << d_evt << " {\n";
                out << "    static constexpr std::string_view name = \"" << d_evt << "\";\n";
                out << "};\n\n";
            }
        }
    }

    static void emit_states(std::ostream& out, const FsmIr& model) {
        out << "// ============================================================================\n";
        out << "// States\n";
        out << "// ============================================================================\n\n";

        for (const auto& state_item : model.states) {
            if (!state_item.traceability_reqs.empty()) {
                out << "/// @satisfies ";
                for (std::size_t i = 0; i < state_item.traceability_reqs.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << state_item.traceability_reqs[i];
                }
                out << "\n";
            }
            out << "struct " << state_item.name << " {\n";
            out << "    static constexpr std::string_view name = \"" << state_item.name << "\";\n";
            if (!state_item.parent_state.empty()) {
                out << "    static constexpr std::string_view parent = \"" << state_item.parent_state << "\";\n";
            }
            if (!state_item.deferred_events.empty()) {
                out << "    using deferred_events = ::fsm::type_list<";
                for (std::size_t i = 0; i < state_item.deferred_events.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << state_item.deferred_events[i];
                }
                out << ">;\n";
            }

            // Entry lifecycle actions
            if (!state_item.entry_actions.empty()) {
                out << "\n    template <typename Context>\n";
                out << "    void on_entry(Context& /*ctx*/) const {\n";
                for (const auto& act : state_item.entry_actions) {
                    out << "        // Entry action: " << act.name << "\n";
                }
                out << "    }\n";
            }

            // Exit lifecycle actions
            if (!state_item.exit_actions.empty()) {
                out << "\n    template <typename Context>\n";
                out << "    void on_exit(Context& /*ctx*/) const {\n";
                for (const auto& act : state_item.exit_actions) {
                    out << "        // Exit action: " << act.name << "\n";
                }
                out << "    }\n";
            }

            if (state_item.kind == StateKind::EntryPoint) {
                out << "    static constexpr bool is_entry_point = true;\n";
            } else if (state_item.kind == StateKind::ExitPoint) {
                out << "    static constexpr bool is_exit_point = true;\n";
            }
            if (state_item.time_invariant.has_value() && !state_item.time_invariant->empty()) {
                out << "    /// @invariant " << *state_item.time_invariant << "\n";
                out << "    static constexpr std::string_view time_invariant = \"" << *state_item.time_invariant
                    << "\";\n";
            }

            if (!state_item.description.empty()) {
                out << "    // Description: " << state_item.description << "\n";
            }
            out << "};\n\n";
        }
    }

    static void emit_guards(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        if (model.guards.empty()) {
            return;
        }

        if (options.include_stubs) {
            out << "// ============================================================================\n";
            out << "// Guards\n";
            out << "// ============================================================================\n\n";

            for (const auto& guard_item : model.guards) {
                out << "struct " << guard_item.name << " {\n";
                if (options.cpp_standard == CppStandard::Cpp20) {
                    out << "    [[nodiscard]] constexpr bool operator()(const auto& /*evt*/, const auto& /*state*/, "
                           "const auto& /*ctx*/) const noexcept {\n";
                    out << "        // TODO: Implement guard logic for " << guard_item.name << "\n";
                    out << "        return true;\n";
                    out << "    }\n";
                } else {
                    out << "    template <typename Event, typename State, typename Context>\n";
                    out << "    bool operator()(const Event& /*evt*/, const State& /*state*/, const Context& /*ctx*/) "
                           "const {\n";
                    out << "        // TODO: Implement guard logic for " << guard_item.name << "\n";
                    out << "        return true;\n";
                    out << "    }\n";
                }
                out << "};\n\n";
            }
        } else {
            out << "// Forward declaration of custom Guards\n";
            for (const auto& guard_item : model.guards) {
                out << "struct " << guard_item.name << ";\n";
            }
            out << "\n";
        }
    }

    static std::string format_context_expr(const std::string& raw_expr,
                                           const std::vector<VariableDefinition>& variables) {
        std::string expr = raw_expr;
        for (const auto& v : variables) {
            if (v.name.empty())
                continue;
            std::regex re(R"((^|[^A-Za-z0-9_.]))" + v.name + R"((?![A-Za-z0-9_]))");
            expr = std::regex_replace(expr, re, "$1ctx." + v.name);
        }
        return expr;
    }

    static void emit_actions(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        if (model.actions.empty()) {
            return;
        }

        if (options.include_stubs) {
            out << "// ============================================================================\n";
            out << "// Actions\n";
            out << "// ============================================================================\n\n";

            for (const auto& action_item : model.actions) {
                // Find if there are assignments associated with this action
                std::vector<ActionAssignment> assignments;
                for (const auto& t : model.transitions) {
                    if (t.action_sig.has_value() && t.action_sig->name == action_item.name) {
                        for (const auto& a : t.action_sig->assignments) {
                            assignments.push_back(a);
                        }
                    }
                }

                out << "struct " << action_item.name << " {\n";
                if (options.cpp_standard == CppStandard::Cpp20) {
                    if (!assignments.empty()) {
                        out << "    constexpr void operator()(const auto& /*evt*/, auto& /*src*/, auto& /*dst*/, auto& "
                               "ctx) const {\n";
                        for (const auto& assign : assignments) {
                            out << "        ctx." << assign.target_variable << " = "
                                << format_context_expr(assign.expression, model.variables) << ";\n";
                        }
                    } else {
                        out << "    constexpr void operator()(const auto& /*evt*/, auto& /*src*/, auto& /*dst*/, auto& "
                               "/*ctx*/) const {\n";
                        out << "        // TODO: Implement action logic for " << action_item.name << "\n";
                    }
                    out << "    }\n";
                } else {
                    if (!assignments.empty()) {
                        out << "    template <typename Event, typename SrcState, typename DstState, typename "
                               "Context>\n";
                        out << "    void operator()(const Event& /*evt*/, SrcState& /*src*/, DstState& /*dst*/, "
                               "Context& "
                               "ctx) const {\n";
                        for (const auto& assign : assignments) {
                            out << "        ctx." << assign.target_variable << " = "
                                << format_context_expr(assign.expression, model.variables) << ";\n";
                        }
                    } else {
                        out << "    template <typename Event, typename SrcState, typename DstState, typename "
                               "Context>\n";
                        out << "    void operator()(const Event& /*evt*/, SrcState& /*src*/, DstState& /*dst*/, "
                               "Context& "
                               "/*ctx*/) const {\n";
                        out << "        // TODO: Implement action logic for " << action_item.name << "\n";
                    }
                    out << "    }\n";
                }
                out << "};\n\n";
            }
        } else {
            out << "// Forward declaration of custom Actions\n";
            for (const auto& action_item : model.actions) {
                out << "struct " << action_item.name << ";\n";
            }
            out << "\n";
        }
    }

    static void emit_transition_table(std::ostream& out, const FsmIr& model, const GeneratorOptions& /*options*/) {
        out << "// ============================================================================\n";
        out << "// Transition Table (Compile-Time Fluent DSL)\n";
        out << "// ============================================================================\n\n";

        std::string table_type_name = model.name + "Table";
        out << "using " << table_type_name << " = fsm::transition_table<\n";

        auto find_state_by_name = [&](const std::string& name) -> const StateNode* {
            for (const auto& s : model.states) {
                if (s.name == name) {
                    return &s;
                }
            }
            return nullptr;
        };

        auto find_leaf_substates = [&](auto& self, const std::string& parent_name) -> std::vector<std::string> {
            std::vector<std::string> leaves;
            for (const auto& s : model.states) {
                if (s.parent_state == parent_name) {
                    if (s.is_composite) {
                        auto sub_leaves = self(self, s.name);
                        leaves.insert(leaves.end(), sub_leaves.begin(), sub_leaves.end());
                    } else {
                        leaves.push_back(s.name);
                    }
                }
            }
            return leaves;
        };

        auto find_initial_leaf_substate = [&](auto& self, const std::string& state_name) -> std::string {
            const auto* s = find_state_by_name(state_name);
            if (s != nullptr && s->is_composite && !s->initial_sub_state.empty()) {
                return self(self, s->initial_sub_state);
            }
            return state_name;
        };

        struct EffectiveTransition {
            std::string source;
            std::string target;
            std::string event;
            std::optional<std::string> guard;
            std::optional<std::string> action;
            bool is_internal = false;
            std::uint32_t priority = 0;
        };

        struct BaseTransition {
            std::string source;
            std::string target;
            std::string event;
            std::optional<std::string> guard;
            std::optional<std::string> action;
            bool is_internal = false;
            bool target_is_history = false;
            std::uint32_t priority = 0;
        };

        std::vector<BaseTransition> base_transitions;
        constexpr std::size_t kCartesianExpansionThreshold = 16;

        // Map choice nodes to their Cartesian product size
        std::map<std::string, std::pair<std::size_t, std::size_t>> choice_cardinality;
        for (const auto& choice : model.choice_nodes) {
            std::size_t n_in = 0;
            std::size_t n_out = 0;
            for (const auto& t : model.transitions) {
                if (t.target == choice.name || t.target_id == choice.name) {
                    ++n_in;
                }
                if (t.source == choice.name || t.source_id == choice.name) {
                    ++n_out;
                }
            }
            choice_cardinality[choice.name] = {n_in, n_out};
        }

        for (const auto& transition_item : model.transitions) {
            if (model.is_choice_node(transition_item.source)) {
                // If choice exceeded threshold, retain its outgoing branches as direct transitions from choice node
                auto it = choice_cardinality.find(transition_item.source);
                if (it != choice_cardinality.end() &&
                    (it->second.first * it->second.second) > kCartesianExpansionThreshold) {
                    BaseTransition bt;
                    bt.source = transition_item.source;
                    bt.target = transition_item.target;
                    bt.event = transition_item.event.empty() ? "fsm::anonymous_event" : transition_item.event;
                    bt.guard = transition_item.guard;
                    bt.action = transition_item.action;
                    bt.is_internal = false;
                    bt.target_is_history = transition_item.target_is_history;
                    bt.priority = transition_item.priority;
                    base_transitions.push_back(std::move(bt));
                }
                continue;
            }
            if (model.is_choice_node(transition_item.target)) {
                auto it = choice_cardinality.find(transition_item.target);
                std::size_t cartesian_prod =
                    it != choice_cardinality.end() ? (it->second.first * it->second.second) : 0;

                if (cartesian_prod > kCartesianExpansionThreshold) {
                    // Exceeded Cartesian threshold: route directly to choice node to avoid combinatorial template bloat
                    BaseTransition bt;
                    bt.source = transition_item.source;
                    bt.target = transition_item.target;
                    bt.event =
                        transition_item.event.empty() ? transition_item.get_trigger_name() : transition_item.event;
                    if (bt.event.empty() || bt.event == "Anonymous" || bt.event == "none") {
                        bt.event = "fsm::anonymous_event";
                    }
                    bt.guard = transition_item.guard;
                    bt.action = transition_item.action;
                    bt.is_internal = false;
                    bt.target_is_history = transition_item.target_is_history;
                    bt.priority = transition_item.priority;
                    base_transitions.push_back(std::move(bt));
                } else {
                    // Below threshold: expand combinatorial Cartesian product
                    for (const auto& branch : model.transitions) {
                        if (branch.source == transition_item.target) {
                            BaseTransition bt;
                            bt.source = transition_item.source;
                            bt.target = branch.target;
                            bt.event = transition_item.event.empty() ? transition_item.get_trigger_name()
                                                                     : transition_item.event;
                            if (bt.event.empty() || bt.event == "Anonymous" || bt.event == "none") {
                                bt.event = "fsm::anonymous_event";
                            }
                            if (transition_item.guard && branch.guard) {
                                bt.guard = "fsm::and_<" + *transition_item.guard + ", " + *branch.guard + ">";
                            } else if (branch.guard) {
                                bt.guard = branch.guard;
                            } else {
                                bt.guard = transition_item.guard;
                            }
                            bt.action = branch.action ? branch.action : transition_item.action;
                            bt.is_internal = false;
                            bt.target_is_history = branch.target_is_history;
                            bt.priority = std::max(transition_item.priority, branch.priority);
                            base_transitions.push_back(std::move(bt));
                        }
                    }
                }
            } else {
                BaseTransition bt;
                bt.source = transition_item.source;
                bt.target = transition_item.target;
                bt.event = transition_item.event.empty() ? transition_item.get_trigger_name() : transition_item.event;
                if (bt.event.empty() || bt.event == "Anonymous" || bt.event == "none") {
                    bt.event = "fsm::anonymous_event";
                }
                bt.guard = transition_item.guard;
                bt.action = transition_item.action;
                bt.is_internal = (transition_item.kind == TransitionEdgeKind::Internal);
                bt.target_is_history = transition_item.target_is_history;
                bt.priority = transition_item.priority;
                base_transitions.push_back(std::move(bt));
            }
        }

        std::vector<EffectiveTransition> effective_transitions;
        for (const auto& bt : base_transitions) {
            const auto* src_st = find_state_by_name(bt.source);
            std::vector<std::string> actual_sources;
            if (src_st != nullptr && src_st->is_composite) {
                auto leaves = find_leaf_substates(find_leaf_substates, bt.source);
                for (const auto& leaf : leaves) {
                    bool leaf_has_own = false;
                    for (const auto& other : base_transitions) {
                        if (other.source == leaf && other.event == bt.event) {
                            leaf_has_own = true;
                            break;
                        }
                    }
                    if (!leaf_has_own) {
                        actual_sources.push_back(leaf);
                    }
                }
                if (actual_sources.empty()) {
                    actual_sources.push_back(bt.source);
                }
            } else {
                actual_sources.push_back(bt.source);
            }

            for (const auto& src : actual_sources) {
                if (bt.target_is_history) {
                    auto sub_leaves = find_leaf_substates(find_leaf_substates, bt.target);
                    std::string init_leaf = find_initial_leaf_substate(find_initial_leaf_substate, bt.target);

                    for (const auto& sub : sub_leaves) {
                        EffectiveTransition eff;
                        eff.source = src;
                        eff.target = sub;
                        eff.event = bt.event;
                        std::string hist_guard = "fsm::history_is<" + bt.target + ", " + sub + ">";
                        if (bt.guard && !bt.guard->empty()) {
                            eff.guard = "fsm::and_<" + *bt.guard + ", " + hist_guard + ">";
                        } else {
                            eff.guard = hist_guard;
                        }
                        eff.action = bt.action;
                        eff.is_internal = false;
                        eff.priority = bt.priority;
                        effective_transitions.push_back(std::move(eff));
                    }

                    EffectiveTransition fallback;
                    fallback.source = src;
                    fallback.target = init_leaf;
                    fallback.event = bt.event;
                    fallback.guard = bt.guard;
                    fallback.action = bt.action;
                    fallback.is_internal = false;
                    fallback.priority = bt.priority;
                    effective_transitions.push_back(std::move(fallback));
                } else {
                    std::string actual_target = find_initial_leaf_substate(find_initial_leaf_substate, bt.target);
                    EffectiveTransition eff;
                    eff.source = src;
                    eff.target = actual_target;
                    eff.event = bt.event;
                    eff.guard = bt.guard;
                    eff.action = bt.action;
                    eff.is_internal = bt.is_internal;
                    eff.priority = bt.priority;
                    effective_transitions.push_back(std::move(eff));
                }
            }
        }

        std::stable_sort(effective_transitions.begin(), effective_transitions.end(),
                         [](const auto& a, const auto& b) { return a.priority > b.priority; });

        for (std::size_t i = 0; i < effective_transitions.size(); ++i) {
            const auto& transition_item = effective_transitions[i];
            if (transition_item.is_internal) {
                out << "    fsm::internal_row<" << transition_item.source << ", " << transition_item.event << ">";
            } else {
                out << "    fsm::row<" << transition_item.source << ", " << transition_item.event << ", "
                    << transition_item.target << ">";
            }

            if (transition_item.guard && !transition_item.guard->empty()) {
                out << "::when<" << *transition_item.guard << ">";
            }
            if (transition_item.action && !transition_item.action->empty()) {
                out << "::then<" << *transition_item.action << ">";
            }

            if (i + 1 < effective_transitions.size()) {
                out << ",\n";
            } else {
                out << "\n";
            }
        }
        out << ">;\n\n";
    }

    static void emit_fsm_aliases(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        out << "// ============================================================================\n";
        out << "// State Machine Type Aliases\n";
        out << "// ============================================================================\n\n";

        std::string table_type_name = model.name + "Table";
        std::string ctx_type = get_effective_context_type(model);

        auto find_state_by_name = [&](const std::string& name) -> const StateNode* {
            for (const auto& s : model.states) {
                if (s.name == name) {
                    return &s;
                }
            }
            return nullptr;
        };

        auto find_initial_leaf_substate = [&](auto& self, const std::string& state_name) -> std::string {
            const auto* s = find_state_by_name(state_name);
            if (s != nullptr && s->is_composite && !s->initial_sub_state.empty()) {
                return self(self, s->initial_sub_state);
            }
            return state_name;
        };

        std::string raw_init_state = model.initial_state;
        if (raw_init_state.empty()) {
            if (!model.states.empty()) {
                raw_init_state = model.states[0].name;
            } else {
                raw_init_state = "Idle";
            }
        }
        std::string init_state = find_initial_leaf_substate(find_initial_leaf_substate, raw_init_state);

        out << "using " << model.name << " = fsm::fsm<" << table_type_name << ", " << ctx_type << ", " << init_state
            << ", fsm::dynamic_observer>;\n";

        if (options.thread_safe) {
            out << "using ThreadSafe" << model.name << " = fsm::thread_safe_fsm<" << table_type_name << ", " << ctx_type
                << ", " << init_state << ">;\n";
        }
    }

    static void emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        emit_context_definition(out, model);
        emit_events(out, model);
        emit_states(out, model);
        emit_guards(out, model, options);
        emit_actions(out, model, options);
        emit_transition_table(out, model, options);
        emit_fsm_aliases(out, model, options);
    }
};

}  // namespace fsm::codegen
