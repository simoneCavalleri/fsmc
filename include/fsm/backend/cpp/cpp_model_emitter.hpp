#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class CppModelEmitter {
  public:
    static void emit_context_forward_decl(std::ostream& out, const FsmIr& model) {
        if (!model.context_type.empty() && model.context_type != "no_context" &&
            model.context_type != "fsm::no_context" && model.context_type != "void") {
            out << "// Forward declaration of Context (if not already defined)\n";
            out << "struct " << model.context_type << ";\n\n";
        }
    }

    static void emit_events(std::ostream& out, const FsmIr& model) {
        out << "// ============================================================================\n";
        out << "// Events\n";
        out << "// ============================================================================\n\n";

        for (const auto& event_item : model.events) {
            out << "struct " << event_item.name << " {\n";
            out << "    static constexpr std::string_view name = \"" << event_item.name << "\";\n";
            if (!event_item.description.empty()) {
                out << "    // Description: " << event_item.description << "\n";
            }
            out << "};\n\n";
        }
    }

    static void emit_states(std::ostream& out, const FsmIr& model) {
        out << "// ============================================================================\n";
        out << "// States\n";
        out << "// ============================================================================\n\n";

        for (const auto& state_item : model.states) {
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

    static void emit_actions(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        if (model.actions.empty()) {
            return;
        }

        if (options.include_stubs) {
            out << "// ============================================================================\n";
            out << "// Actions\n";
            out << "// ============================================================================\n\n";

            for (const auto& action_item : model.actions) {
                out << "struct " << action_item.name << " {\n";
                if (options.cpp_standard == CppStandard::Cpp20) {
                    out << "    constexpr void operator()(const auto& /*evt*/, auto& /*src*/, auto& /*dst*/, auto& "
                           "/*ctx*/) const {\n";
                    out << "        // TODO: Implement action logic for " << action_item.name << "\n";
                    out << "    }\n";
                } else {
                    out << "    template <typename Event, typename SrcState, typename DstState, typename Context>\n";
                    out << "    void operator()(const Event& /*evt*/, SrcState& /*src*/, DstState& /*dst*/, Context& "
                           "/*ctx*/) const {\n";
                    out << "        // TODO: Implement action logic for " << action_item.name << "\n";
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
        };

        struct BaseTransition {
            std::string source;
            std::string target;
            std::string event;
            std::optional<std::string> guard;
            std::optional<std::string> action;
            bool is_internal = false;
            bool target_is_history = false;
        };

        std::vector<BaseTransition> base_transitions;
        for (const auto& transition_item : model.transitions) {
            if (model.is_choice_node(transition_item.source)) {
                continue;
            }
            if (model.is_choice_node(transition_item.target)) {
                for (const auto& branch : model.transitions) {
                    if (branch.source == transition_item.target) {
                        BaseTransition bt;
                        bt.source = transition_item.source;
                        bt.target = branch.target;
                        bt.event = transition_item.event;
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
                        base_transitions.push_back(std::move(bt));
                    }
                }
            } else {
                BaseTransition bt;
                bt.source = transition_item.source;
                bt.target = transition_item.target;
                bt.event = transition_item.event;
                bt.guard = transition_item.guard;
                bt.action = transition_item.action;
                bt.is_internal = (transition_item.kind == TransitionEdgeKind::Internal);
                bt.target_is_history = transition_item.target_is_history;
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
                        effective_transitions.push_back(std::move(eff));
                    }

                    EffectiveTransition fallback;
                    fallback.source = src;
                    fallback.target = init_leaf;
                    fallback.event = bt.event;
                    fallback.guard = bt.guard;
                    fallback.action = bt.action;
                    fallback.is_internal = false;
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
                    effective_transitions.push_back(std::move(eff));
                }
            }
        }

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
        std::string ctx_type =
            (model.context_type.empty() || model.context_type == "no_context" || model.context_type == "void")
                ? "fsm::no_context"
                : model.context_type;

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
            << ">;\n";

        if (options.thread_safe) {
            out << "using ThreadSafe" << model.name << " = fsm::thread_safe_fsm<" << table_type_name << ", " << ctx_type
                << ", " << init_state << ">;\n";
        }
    }

    static void emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        emit_context_forward_decl(out, model);
        emit_events(out, model);
        emit_states(out, model);
        emit_guards(out, model, options);
        emit_actions(out, model, options);
        emit_transition_table(out, model, options);
        emit_fsm_aliases(out, model, options);
    }
};

}  // namespace fsm::codegen
