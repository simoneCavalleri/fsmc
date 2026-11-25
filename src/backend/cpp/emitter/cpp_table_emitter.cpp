#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "cpp_emitter_utils.hpp"
#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::backend::cpp {

using namespace fsm::ir;
using namespace emitter_detail;

void CppModelEmitter::emit_transition_table(std::ostream& out, const FsmIr& model,
                                            const GeneratorOptions& /*options*/) {
    out << "// ============================================================================\n";
    out << "// Transition Table\n";
    out << "// ============================================================================\n\n";

    std::string table_type_name = sanitize_cpp_model_name(model.name) + "Table";

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

    auto get_all_substates = [&](auto& self, const std::string& parent_name, bool deep) -> std::vector<std::string> {
        std::vector<std::string> subs;
        for (const auto& s : model.states) {
            if (s.parent_state == parent_name) {
                if (deep && s.is_composite) {
                    auto nested = self(self, s.name, deep);
                    subs.insert(subs.end(), nested.begin(), nested.end());
                } else {
                    subs.push_back(s.name);
                }
            }
        }
        return subs;
    };

    auto escape_type = [](const std::string& token) -> std::string {
        if (token.empty() || token.rfind("::", 0) == 0) {
            return token;
        }
        return escape_cpp_keyword(token);
    };

    bool first_row = true;
    auto emit_single_row = [&](const std::string& src, const std::string& evt, const std::string& tgt,
                               const std::string& act, const std::string& grd) {
        std::string s = escape_type(src);
        std::string e = escape_type(evt);
        std::string t = escape_type(tgt);
        std::string a = (act.rfind("::", 0) == 0) ? act : resolve_action_cpp_name(act, model);
        std::string g = (grd.rfind("::", 0) == 0) ? grd : escape_cpp_keyword(grd);
        if (!first_row) {
            out << ",\n    ::fsm::row<" << s << ", " << e << ", " << t << ">";
        } else {
            out << "\n    ::fsm::row<" << s << ", " << e << ", " << t << ">";
            first_row = false;
        }
        if (g != "::fsm::no_guard") {
            out << "::when<" << g << ">";
        }
        if (a != "::fsm::no_action") {
            out << "::then<" << a << ">";
        }
    };

    auto emit_internal_row = [&](const std::string& src, const std::string& evt, const std::string& act,
                                 const std::string& grd) {
        std::string s = escape_type(src);
        std::string e = escape_type(evt);
        std::string a = (act.rfind("::", 0) == 0) ? act : resolve_action_cpp_name(act, model);
        std::string g = (grd.rfind("::", 0) == 0) ? grd : escape_cpp_keyword(grd);
        if (!first_row) {
            out << ",\n    ::fsm::internal_row<" << s << ", " << e << ">";
        } else {
            out << "\n    ::fsm::internal_row<" << s << ", " << e << ">";
            first_row = false;
        }
        if (g != "::fsm::no_guard") {
            out << "::when<" << g << ">";
        }
        if (a != "::fsm::no_action") {
            out << "::then<" << a << ">";
        }
    };

    out << "/**\n";
    out << " * @typedef " << table_type_name << "\n";
    out << " * @brief Static transition table specification for '" << sanitize_cpp_comment(model.name) << "'.\n";
    out << " */\n";
    out << "using " << table_type_name << " = ::fsm::transition_table<";

    std::vector<TransitionEdge> transitions = model.transitions;
    std::stable_sort(transitions.begin(), transitions.end(), [](const auto& a, const auto& b) {
        auto pa = (a.priority == 0) ? std::numeric_limits<std::uint32_t>::max() : a.priority;
        auto pb = (b.priority == 0) ? std::numeric_limits<std::uint32_t>::max() : b.priority;
        return pa < pb;
    });

    for (const auto& t : transitions) {
        std::string event_type = t.event;
        if (std::holds_alternative<TimeTrigger>(t.trigger)) {
            const auto& time_trigger = std::get<TimeTrigger>(t.trigger);
            const auto duration_ms = time_trigger.duration_in_ms();
            if (duration_ms > 0) {
                event_type = time_trigger.kind == TimeTriggerKind::Every
                                 ? "::fsm::every_ms<" + std::to_string(duration_ms) + ">"
                                 : "::fsm::after_ms<" + std::to_string(duration_ms) + ">";
            } else if (!time_trigger.dynamic_expression.empty()) {
                event_type = "::fsm::after_ms<1000>";
            } else {
                event_type = "::fsm::after_ms<0>";
            }
        } else if (event_type.empty()) {
            event_type = "::fsm::anonymous_event";
        }

        std::string action_type;
        if (t.condition_action.has_value() && !t.condition_action->name.empty() && t.transition_action.has_value() &&
            !t.transition_action->name.empty()) {
            action_type = "::fsm::seq_<" + resolve_action_cpp_name(t.condition_action->name, model) + ", " +
                          resolve_action_cpp_name(t.transition_action->name, model) + ">";
        } else {
            action_type = t.get_action();
            if (action_type.empty()) {
                action_type = "::fsm::no_action";
            }
        }

        auto render_cpp_guard_type = [](auto& self, const ir::GuardAstNode& node) -> std::optional<std::string> {
            if (node.op == ir::GuardOp::None) {
                const std::string& expr = node.expression;
                if (expr.empty() || expr == "else" || expr == "otherwise" || expr == "default") {
                    return std::string("::fsm::no_guard");
                }
                // Accept plain C++ identifiers or already-qualified ::fsm:: / fsm:: combinators
                bool is_ident = (std::isalpha(static_cast<unsigned char>(expr[0])) != 0 || expr[0] == '_');
                for (std::size_t i = 1; is_ident && i < expr.size(); ++i) {
                    if (std::isalnum(static_cast<unsigned char>(expr[i])) == 0 && expr[i] != '_') {
                        is_ident = false;
                    }
                }
                if (is_ident) {
                    return escape_cpp_keyword(expr);
                }
                if (expr.rfind("::fsm::", 0) == 0) {
                    return expr;
                }
                if (expr.rfind("fsm::", 0) == 0) {
                    return "::" + expr;
                }
                return std::nullopt;
            }
            if (node.op == ir::GuardOp::Not && node.children.size() == 1) {
                auto inner = self(self, node.children[0]);
                if (!inner.has_value()) {
                    return std::nullopt;
                }
                return "::fsm::not_<" + *inner + ">";
            }
            if ((node.op == ir::GuardOp::And || node.op == ir::GuardOp::Or) && !node.children.empty()) {
                std::string out_type = (node.op == ir::GuardOp::And) ? "::fsm::and_<" : "::fsm::or_<";
                for (std::size_t i = 0; i < node.children.size(); ++i) {
                    auto child = self(self, node.children[i]);
                    if (!child.has_value()) {
                        return std::nullopt;
                    }
                    if (i > 0) {
                        out_type += ", ";
                    }
                    out_type += *child;
                }
                out_type += ">";
                return out_type;
            }
            return std::nullopt;
        };

        std::string guard_type = t.guard.value_or("::fsm::no_guard");
        if (t.guard_ast.has_value() && t.guard_ast->op != ir::GuardOp::None) {
            if (auto ast_type = render_cpp_guard_type(render_cpp_guard_type, *t.guard_ast); ast_type.has_value()) {
                guard_type = std::move(*ast_type);
            }
        }
        if (guard_type.empty() || guard_type == "else" || guard_type == "otherwise" || guard_type == "default") {
            guard_type = "::fsm::no_guard";
        } else if (guard_type.rfind("::fsm::", 0) != 0 && guard_type.rfind("fsm::", 0) != 0 &&
                   (guard_type.find('&') != std::string::npos || guard_type.find('|') != std::string::npos ||
                    guard_type.find('!') != std::string::npos || guard_type.find('(') != std::string::npos)) {
            auto parsed = ir::GuardExpressionParser::parse(guard_type);
            if (parsed.ast.has_value()) {
                if (auto ast_type = render_cpp_guard_type(render_cpp_guard_type, *parsed.ast); ast_type.has_value()) {
                    guard_type = std::move(*ast_type);
                }
            }
        }

        const auto* target_node = find_state_by_name(t.target);
        if (target_node != nullptr && target_node->is_composite) {
            std::vector<std::string> sub_states =
                get_all_substates(get_all_substates, target_node->name, target_node->has_deep_history);

            if (target_node->has_history && !sub_states.empty()) {
                for (const auto& sub : sub_states) {
                    std::string composite_guard;
                    std::string escaped_target = escape_cpp_keyword(target_node->name);
                    std::string escaped_sub = escape_cpp_keyword(sub);
                    if (guard_type != "::fsm::no_guard") {
                        std::string escaped_guard =
                            (guard_type.rfind("::", 0) == 0) ? guard_type : escape_cpp_keyword(guard_type);
                        composite_guard = "::fsm::and_<";
                        composite_guard += escaped_guard;
                        composite_guard += ", ::fsm::history_is<";
                        composite_guard += escaped_target;
                        composite_guard += ", ";
                        composite_guard += escaped_sub;
                        composite_guard += ">>";
                    } else {
                        composite_guard = "::fsm::history_is<";
                        composite_guard += escaped_target;
                        composite_guard += ", ";
                        composite_guard += escaped_sub;
                        composite_guard += ">";
                    }
                    emit_single_row(t.source, event_type, sub, action_type, composite_guard);
                }
                std::string default_sub =
                    !target_node->initial_sub_state.empty() ? target_node->initial_sub_state : sub_states[0];
                default_sub = find_initial_leaf_substate(find_initial_leaf_substate, default_sub);
                emit_single_row(t.source, event_type, default_sub, action_type, guard_type);
            } else if (!target_node->initial_sub_state.empty()) {
                std::string leaf_sub =
                    find_initial_leaf_substate(find_initial_leaf_substate, target_node->initial_sub_state);
                emit_single_row(t.source, event_type, leaf_sub, action_type, guard_type);
            } else {
                emit_single_row(t.source, event_type, t.target, action_type, guard_type);
            }
        } else {
            if (t.kind == TransitionEdgeKind::Internal) {
                emit_internal_row(t.source, event_type, action_type, guard_type);
            } else {
                emit_single_row(t.source, event_type, t.target, action_type, guard_type);
            }
        }
    }
    out << ">;\n\n";
}

void CppModelEmitter::emit_fsm_aliases(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    out << "// ============================================================================\n";
    out << "// State Machine Type Aliases\n";
    out << "// ============================================================================\n\n";

    const std::string safe_model_name = sanitize_cpp_model_name(model.name);
    std::string table_type_name = safe_model_name + "Table";

    auto find_state_by_name = [&](const std::string& name) -> const StateNode* {
        for (const auto& s : model.states) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    };

    std::unordered_set<std::string> visited_initial_states;
    auto find_initial_leaf_substate = [&](auto& self, const std::string& state_name) -> std::string {
        if (!visited_initial_states.insert(state_name).second) {
            return state_name;
        }
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
    if (!init_state.empty() && init_state.rfind("::", 0) != 0) {
        init_state = escape_cpp_keyword(init_state);
    }

    const std::string fsm_alias_name =
        (model.find_state(safe_model_name) != nullptr) ? (safe_model_name + "Fsm") : safe_model_name;
    out << "/**\n";
    out << " * @typedef " << fsm_alias_name << "\n";
    out << " * @brief Primary Synchronous Core State Machine instance for '" << sanitize_cpp_comment(model.name)
        << "' on caller stack.\n";
    for (const auto& req : model.satisfies_reqs) {
        out << " * @trace " << sanitize_cpp_comment(req) << "\n";
        out << " * @satisfies " << sanitize_cpp_comment(req) << "\n";
    }
    out << " */\n";
    out << "using " << fsm_alias_name << " = ::fsm::make_fsm<\n";
    out << "    " << table_type_name << ",\n";
    out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
    out << "    ::fsm::with_ports<" << safe_model_name << "InPorts, " << safe_model_name << "OutPorts>,\n";
    out << "    ::fsm::with_registers<" << safe_model_name << "Registers>,\n";
    out << "    ::fsm::with_services<" << safe_model_name << "Services>,\n";
    out << "    ::fsm::with_observer<::fsm::no_observer>\n";
    out << ">;\n\n";

    if (!model.services.empty()) {
        out << "/**\n";
        out << " * @typedef " << fsm_alias_name << "WithServices\n";
        out << " * @brief Zero-vtable FSM alias accepting any user Services struct satisfying " << safe_model_name
            << "ServicesContract.\n";
        out << " */\n";
        out << "template <typename CustomServices>\n";
        out << "using " << fsm_alias_name << "WithServices = std::enable_if_t<\n";
        out << "    " << safe_model_name << "ServicesContract<CustomServices>::satisfied,\n";
        out << "    ::fsm::make_fsm<\n";
        out << "        " << table_type_name << ",\n";
        out << "        ::fsm::with_initial_state<" << init_state << ">,\n";
        out << "        ::fsm::with_ports<" << safe_model_name << "InPorts, " << safe_model_name << "OutPorts>,\n";
        out << "        ::fsm::with_registers<" << safe_model_name << "Registers>,\n";
        out << "        ::fsm::with_services<CustomServices>,\n";
        out << "        ::fsm::with_observer<::fsm::no_observer>\n";
        out << "    >>;\n\n";
    }

    if (options.thread_safe && !options.freestanding) {
        out << "/**\n";
        out << " * @typedef ThreadSafe" << safe_model_name << "\n";
        out << " * @brief Thread-Safe Active Object State Machine for '" << sanitize_cpp_comment(model.name) << "'.\n";
        out << " */\n";
        out << "using ThreadSafe" << safe_model_name << " = ::fsm::make_thread_safe_fsm<\n";
        out << "    " << table_type_name << ",\n";
        out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
        out << "    ::fsm::with_ports<" << safe_model_name << "InPorts, " << safe_model_name << "OutPorts>,\n";
        out << "    ::fsm::with_registers<" << safe_model_name << "Registers>,\n";
        out << "    ::fsm::with_services<" << safe_model_name << "Services>\n";
        out << ">;\n\n";

        bool registers_trivially_copyable = true;
        const auto structs = model.get_structs();
        auto is_type_trivially_copyable = [&](auto& self, const DataType& type,
                                              std::unordered_set<std::string>& visited_structs) -> bool {
            if (type.is_string()) {
                return false;
            }
            if (type.is_struct()) {
                if (!visited_structs.insert(type.custom_name()).second) {
                    return false;
                }
                for (const auto& st : structs) {
                    if (st.name == type.custom_name()) {
                        for (const auto& f : st.fields) {
                            if (!self(self, f.type, visited_structs)) {
                                return false;
                            }
                        }
                    }
                }
                visited_structs.erase(type.custom_name());
            }
            return true;
        };
        for (const auto& var : model.variables) {
            std::unordered_set<std::string> visited_structs;
            if (!is_type_trivially_copyable(is_type_trivially_copyable, var.type, visited_structs)) {
                registers_trivially_copyable = false;
                break;
            }
        }
        bool events_trivially_copyable = true;
        for (const auto& sig : model.signals) {
            for (const auto& attr : sig.attributes) {
                std::unordered_set<std::string> visited_structs;
                if (!is_type_trivially_copyable(is_type_trivially_copyable, attr.type, visited_structs)) {
                    events_trivially_copyable = false;
                    break;
                }
            }
            if (!events_trivially_copyable) {
                break;
            }
        }

        if (registers_trivially_copyable && events_trivially_copyable) {
            out << "/**\n";
            out << " * @typedef Spsc" << safe_model_name << "\n";
            out << " * @brief Lock-Free Single-Producer Single-Consumer State Machine for '"
                << sanitize_cpp_comment(model.name) << "'.\n";
            out << " */\n";
            out << "using Spsc" << safe_model_name << " = ::fsm::make_spsc_fsm<\n";
            out << "    " << table_type_name << ",\n";
            out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
            out << "    ::fsm::with_ports<" << safe_model_name << "InPorts, " << safe_model_name << "OutPorts>,\n";
            out << "    ::fsm::with_registers<" << safe_model_name << "Registers>,\n";
            out << "    ::fsm::with_services<" << safe_model_name << "Services>,\n";
            out << "    ::fsm::with_queue_capacity<64>\n";
            out << ">;\n";
        }
    }
}

void CppModelEmitter::emit_external_includes(std::ostream& out, const FsmIr& model) {
    std::set<std::string> includes;
    for (const auto& td : model.custom_types) {
        if (td.external_ref.has_value() && !td.external_ref->module.empty()) {
            includes.insert(resolve_external_cpp_include(td.external_ref->module));
        }
    }
    if (includes.empty()) {
        return;
    }
    for (const auto& inc : includes) {
        out << "#include " << inc << "\n";
    }
    out << "\n";
}

void CppModelEmitter::emit_ports_interface(std::ostream& out, const FsmIr& model) {
    emit_domain_structures(out, model);
    emit_events(out, model);
}

void CppModelEmitter::emit_fsm_engine(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    emit_guards(out, model, options);
    emit_actions(out, model, options);
    emit_states(out, model, options);
    emit_transition_table(out, model, options);
    emit_fsm_aliases(out, model, options);
}

void CppModelEmitter::emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    emit_ports_interface(out, model);
    emit_fsm_engine(out, model, options);
}

}  // namespace fsm::backend::cpp
