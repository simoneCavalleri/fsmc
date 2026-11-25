#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
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

void CppModelEmitter::emit_guards(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    if (model.guards.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// Guards\n";
    out << "// ============================================================================\n\n";

    std::set<std::string> forward_declared_guards;
    static const std::regex ident_re(R"([A-Za-z_][A-Za-z0-9_]*)");

    for (const auto& guard_item : model.guards) {
        if (guard_item.name == "else" || guard_item.name == "default" ||
            guard_item.name.find("::") != std::string::npos || guard_item.name.find('<') != std::string::npos ||
            guard_item.name.find('>') != std::string::npos || guard_item.name.find(' ') != std::string::npos ||
            guard_item.name.find('&') != std::string::npos || guard_item.name.find('|') != std::string::npos ||
            guard_item.name.find('!') != std::string::npos) {
            if (!options.include_stubs) {
                for (std::sregex_iterator it(guard_item.name.begin(), guard_item.name.end(), ident_re), end; it != end;
                     ++it) {
                    std::string id = it->str();
                    if (id != "fsm" && id != "and_" && id != "or_" && id != "not_" && id != "xor_" &&
                        id != "no_guard" && id != "else" && id != "default") {
                        forward_declared_guards.insert(id);
                    }
                }
            }
            continue;
        }
        const std::string guard_struct_name = escape_cpp_keyword(guard_item.name);
        const bool has_expr = guard_item.normalized_expression.has_value() &&
                              !guard_item.normalized_expression->empty() &&
                              *guard_item.normalized_expression != guard_item.name &&
                              (guard_item.name != (*guard_item.normalized_expression + "Guard"));
        std::string expr;
        bool expr_unresolved = false;
        bool is_self_call_predicate = false;
        bool is_domain_symbol = false;
        if (has_expr) {
            expr = qualify_expression(*guard_item.normalized_expression, model);
            is_self_call_predicate = has_self_constructor_call(expr, guard_struct_name);
            if (is_self_call_predicate || has_unresolved_identifiers(expr)) {
                expr_unresolved = true;
            }
        } else {
            const auto* p = model.find_port(guard_item.name);
            const auto* v = model.find_variable(guard_item.name);
            if (p != nullptr && p->is_in()) {
                expr = "in." + guard_struct_name;
                is_domain_symbol = true;
            } else if (p != nullptr && p->is_out()) {
                expr = "out." + guard_struct_name;
                is_domain_symbol = true;
            } else if (v != nullptr) {
                expr = "reg." + guard_struct_name;
                is_domain_symbol = true;
            }
        }

        const bool has_model_impl = (has_expr && !expr_unresolved) || is_domain_symbol;
        const bool is_symbolic_service_guard = (!has_expr && !is_domain_symbol) || is_self_call_predicate;

        if (!options.include_stubs && !has_model_impl) {
            forward_declared_guards.insert(guard_item.name);
            continue;
        }

        // Fail-closed (ADR-08): when synthesizing stubs, an expression with unresolved identifiers must fail.
        if (has_expr && expr_unresolved && !is_self_call_predicate) {
            throw std::invalid_argument("ECPP011: Unresolved identifier in guard expression '" + guard_item.name +
                                        "': " + expr);
        }

        out << "/**\n";
        out << " * @struct " << guard_struct_name << "\n";
        out << " * @brief Transition guard predicate for '" << sanitize_cpp_comment(guard_item.name) << "'.\n";
        if (has_model_impl) {
            out << " * @note Evaluates: `" << sanitize_cpp_comment(expr) << "`\n";
        } else {
            out << " * @note Symbolic predicate delegated to Services::" << guard_struct_name << "()\n";
        }
        out << " */\n";
        out << "struct " << guard_struct_name << " {\n";

        if (is_symbolic_service_guard) {
            out << "    // Symbolic predicate delegated to Services (fail-closed default if unimplemented)\n";
            out << "    template <typename Event, typename SrcState, typename InPorts, typename Registers, "
                   "typename Services, typename Fsm>\n";
            out << "    [[nodiscard]] constexpr bool operator()(const Event& /*cmd*/, const SrcState& /*src*/, const "
                   "InPorts& /*in*/, const Registers& /*reg*/, Services& srv, const Fsm& /*fsm*/) const {\n";
            out << "        return srv." << guard_struct_name << "();\n";
            out << "    }\n";
            out << "};\n\n";
            continue;
        }

        const bool references_event = has_member_access_prefix(expr, {"cmd", "event", "payload"});
        const bool references_reg = has_member_access_prefix(expr, {"reg"});

        out << "    // Domain guard evaluation (InPorts, Registers, Event payload)\n";
        if (!references_event) {
            out << "    template <typename InPorts, typename Registers>\n";
            out << "    [[nodiscard]] constexpr bool operator()(const InPorts& in, const Registers& reg) const "
                   "noexcept {\n";
            out << "        (void)in; (void)reg;\n";
            out << "        return " << expr << ";\n";
            out << "    }\n\n";
        }

        out << "    template <typename Event, typename InPorts, typename Registers>\n";
        out << "    [[nodiscard]] constexpr bool operator()(const Event& cmd, const InPorts& in, const "
               "Registers& reg) const noexcept {\n";
        out << "        (void)cmd; (void)in; (void)reg;\n";
        out << "        return " << expr << ";\n";
        out << "    }\n\n";

        if (!references_event && !references_reg) {
            out << "    template <typename InPorts>\n";
            out << "    [[nodiscard]] constexpr bool operator()(const InPorts& in) const noexcept {\n";
            out << "        (void)in;\n";
            out << "        return " << expr << ";\n";
            out << "    }\n\n";
        }

        out << "    template <typename Event, typename SrcState, typename InPorts, typename Registers, "
               "typename Services, typename Fsm>\n";
        out << "    [[nodiscard]] constexpr bool operator()(const Event& cmd, const SrcState& /*src*/, const "
               "InPorts& in, const Registers& reg, Services& /*srv*/, const Fsm& /*fsm*/) const noexcept {\n";
        out << "        (void)cmd; (void)in; (void)reg;\n";
        out << "        return " << expr << ";\n";
        out << "    }\n";
        out << "};\n\n";
    }

    if (!forward_declared_guards.empty()) {
        out << "// Forward declaration of custom Guards\n";
        for (const auto& g : forward_declared_guards) {
            out << "struct " << escape_cpp_keyword(g) << ";\n";
        }
        out << "\n";
    }
}

void CppModelEmitter::emit_actions(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    std::vector<ActionModel> effective_actions = model.actions;
    auto has_action = [&](const std::string& name) {
        for (const auto& a : effective_actions) {
            if (a.name == name)
                return true;
        }
        return false;
    };
    for (const auto& st : model.states) {
        if (st.do_activity.has_value() && !st.do_activity->empty() && !has_action(*st.do_activity)) {
            effective_actions.push_back(ActionModel(*st.do_activity, "Activity for state " + st.name));
        }
        for (const auto& a : st.entry_actions) {
            if (!a.name.empty() && !has_action(a.name)) {
                effective_actions.push_back(ActionModel(a.name, "Entry action"));
            }
        }
        for (const auto& a : st.exit_actions) {
            if (!a.name.empty() && !has_action(a.name)) {
                effective_actions.push_back(ActionModel(a.name, "Exit action"));
            }
        }
    }
    for (const auto& t : model.transitions) {
        if (t.condition_action.has_value() && !t.condition_action->name.empty() &&
            !has_action(t.condition_action->name)) {
            effective_actions.push_back(ActionModel(t.condition_action->name, "Condition action"));
        }
        if (t.transition_action.has_value() && !t.transition_action->name.empty() &&
            !has_action(t.transition_action->name)) {
            effective_actions.push_back(ActionModel(t.transition_action->name, "Transition action"));
        }
    }

    if (effective_actions.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// Actions\n";
    out << "// ============================================================================\n\n";

    std::set<std::string> forward_declared_actions;
    static const std::regex act_ident_re(R"([A-Za-z_][A-Za-z0-9_]*)");

    for (const auto& action_item : effective_actions) {
        std::vector<ActionAssignment> assignments;
        std::vector<ActionAstNode> instructions;
        bool body_collected = false;
        auto collect_action_body = [&](const ActionSignature& act) {
            if (body_collected || (act.assignments.empty() && act.instructions.empty())) {
                return;
            }
            for (const auto& a : act.assignments) {
                assignments.push_back(a);
            }
            for (const auto& inst : act.instructions) {
                instructions.push_back(inst);
            }
            body_collected = true;
        };
        for (const auto& t : model.transitions) {
            if (t.condition_action.has_value() && t.condition_action->name == action_item.name) {
                collect_action_body(*t.condition_action);
            }
            if (t.transition_action.has_value() && t.transition_action->name == action_item.name) {
                collect_action_body(*t.transition_action);
            }
        }
        for (const auto& st : model.states) {
            for (const auto& a : st.entry_actions) {
                if (a.name == action_item.name) {
                    collect_action_body(a);
                }
            }
            for (const auto& a : st.exit_actions) {
                if (a.name == action_item.name) {
                    collect_action_body(a);
                }
            }
        }

        if (!options.include_stubs && assignments.empty() && instructions.empty()) {
            for (std::sregex_iterator it(action_item.name.begin(), action_item.name.end(), act_ident_re), end;
                 it != end; ++it) {
                std::string id = it->str();
                if (id != "fsm" && id != "and_" && id != "or_" && id != "not_" && id != "seq_" && id != "no_action") {
                    forward_declared_actions.insert(resolve_action_cpp_name(id, model));
                }
            }
            continue;
        }

        std::string struct_name = resolve_action_cpp_name(action_item.name, model);
        out << "/**\n";
        out << " * @struct " << struct_name << "\n";
        out << " * @brief Transition action effect for '" << action_item.name << "'.\n";
        out << " */\n";
        out << "struct " << struct_name << " {\n";

        bool has_datapath_instructions = false;
        for (const auto& inst : instructions) {
            if (std::holds_alternative<StoreOp>(inst.op) || std::holds_alternative<PortWriteOp>(inst.op) ||
                std::holds_alternative<PortReadOp>(inst.op) || std::holds_alternative<SignalEmitOp>(inst.op)) {
                has_datapath_instructions = true;
                break;
            }
            if (const auto* call = std::get_if<ActionCallOp>(&inst.op)) {
                if (instructions.size() > 1 || !call->arguments.empty() || !call->read_set.empty() ||
                    !call->write_set.empty()) {
                    has_datapath_instructions = true;
                    break;
                }
            }
        }

        if (!assignments.empty() || has_datapath_instructions) {
            bool uses_out = false;
            bool uses_reg = false;
            bool uses_in = false;
            bool uses_cmd = false;
            bool uses_srv = false;

            auto scan_rhs_usage = [&](const std::string& qualified_rhs) {
                if (has_member_access_prefix(qualified_rhs, {"in"})) {
                    uses_in = true;
                }
                if (has_member_access_prefix(qualified_rhs, {"out"})) {
                    uses_out = true;
                }
                if (has_member_access_prefix(qualified_rhs, {"reg"})) {
                    uses_reg = true;
                }
                if (has_member_access_prefix(qualified_rhs, {"cmd", "event", "payload"})) {
                    uses_cmd = true;
                }
                if (has_member_access_prefix(qualified_rhs, {"srv", "service"})) {
                    uses_srv = true;
                }
            };

            for (const auto& assign : assignments) {
                const auto* p = model.find_port(assign.target.name);
                bool is_out = (assign.target.scope == LValueScope::OutPort) || (p != nullptr && p->is_out());
                if (is_out) {
                    uses_out = true;
                } else {
                    uses_reg = true;
                }
                scan_rhs_usage(qualify_expression(assign.expression, model));
            }

            for (const auto& inst : instructions) {
                if (const auto* st = std::get_if<StoreOp>(&inst.op)) {
                    const auto* p = model.find_port(st->target.name);
                    bool is_out = (st->target.scope == LValueScope::OutPort) || (p != nullptr && p->is_out());
                    if (is_out) {
                        uses_out = true;
                    } else {
                        uses_reg = true;
                    }
                    scan_rhs_usage(qualify_expression(st->expression, model));
                } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                    uses_out = true;
                    scan_rhs_usage(qualify_expression(pw->expression, model));
                } else if (const auto* pr = std::get_if<PortReadOp>(&inst.op)) {
                    if (!pr->destination.name.empty()) {
                        uses_in = true;
                        uses_reg = true;
                    }
                } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
                    uses_srv = true;
                    if (ac->result_target.has_value() && !ac->result_target->name.empty()) {
                        const auto* p = model.find_port(ac->result_target->name);
                        const bool is_out =
                            (ac->result_target->scope == LValueScope::OutPort) || (p != nullptr && p->is_out());
                        if (is_out) {
                            uses_out = true;
                        } else {
                            uses_reg = true;
                        }
                    }
                    for (const auto& arg : ac->arguments) {
                        scan_rhs_usage(qualify_expression(arg, model));
                    }
                } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                    uses_out = true;
                    const auto* sig_def = model.find_signal(se->signal_name);
                    if (sig_def != nullptr && !sig_def->attributes.empty()) {
                        for (std::size_t ai = 0; ai < se->arguments.size() && ai < sig_def->attributes.size(); ++ai) {
                            scan_rhs_usage(qualify_expression(se->arguments[ai], model));
                        }
                    }
                }
            }

            std::string in_param = uses_in ? "const InPorts& in" : "const InPorts& /*in*/";
            std::string out_param = uses_out ? "OutPorts& out" : "OutPorts& /*out*/";
            std::string reg_param = uses_reg ? "Registers& reg" : "Registers& /*reg*/";
            std::string cmd_param = uses_cmd ? "const Event& cmd" : "const Event& /*cmd*/";
            std::string srv_param = uses_srv ? "Services& srv" : "Services& /*srv*/";

            auto is_float32_target = [&](const std::string& target_name) -> bool {
                if (const auto* p = model.find_port(target_name)) {
                    return p->type.is_primitive() && p->type.primitive() == PrimitiveTypeKind::Float32;
                }
                if (const auto* v = model.find_variable(target_name)) {
                    return v->type.is_primitive() && v->type.primitive() == PrimitiveTypeKind::Float32;
                }
                return false;
            };

            auto suffix_float32_literals = [](const std::string& expr_in) -> std::string {
                std::string res;
                res.reserve(expr_in.size() + 8);
                for (std::size_t i = 0; i < expr_in.size(); ++i) {
                    if (std::isdigit(static_cast<unsigned char>(expr_in[i])) != 0 &&
                        (i == 0 ||
                         (std::isalnum(static_cast<unsigned char>(expr_in[i - 1])) == 0 && expr_in[i - 1] != '_'))) {
                        std::size_t j = i;
                        bool has_dot = false;
                        while (j < expr_in.size() &&
                               (std::isdigit(static_cast<unsigned char>(expr_in[j])) != 0 || expr_in[j] == '.')) {
                            if (expr_in[j] == '.') {
                                has_dot = true;
                            }
                            ++j;
                        }
                        if (has_dot && j > i &&
                            (j == expr_in.size() || (expr_in[j] != 'f' && expr_in[j] != 'F' && expr_in[j] != 'l' &&
                                                     expr_in[j] != 'L' && expr_in[j] != 'e' && expr_in[j] != 'E'))) {
                            res.append(expr_in, i, j - i);
                            res.push_back('f');
                            i = j - 1;
                            continue;
                        }
                    }
                    res.push_back(expr_in[i]);
                }
                return res;
            };

            auto escape_member_path = [](const std::string& path) -> std::string {
                std::string res;
                std::size_t start = 0;
                while (start <= path.size()) {
                    const std::size_t dot = path.find('.', start);
                    if (!res.empty()) {
                        res.push_back('.');
                    }
                    if (dot == std::string::npos) {
                        res += escape_cpp_keyword(path.substr(start));
                        break;
                    }
                    res += escape_cpp_keyword(path.substr(start, dot - start));
                    start = dot + 1;
                }
                return res;
            };

            auto emit_assignment_lines = [&](std::string_view indent) {
                for (const auto& assign : assignments) {
                    std::string expr = qualify_expression(assign.expression, model);
                    if (is_float32_target(assign.target.name)) {
                        expr = suffix_float32_literals(expr);
                    }
                    const auto* p = model.find_port(assign.target.name);
                    bool is_out = (assign.target.scope == LValueScope::OutPort) || (p != nullptr && p->is_out());
                    std::string_view op_str = assignment_op_to_string(assign.op);
                    const std::string tgt = escape_member_path(assign.target.full_path());
                    if (is_out) {
                        out << indent << "out." << tgt << " " << op_str << " " << expr << ";\n";
                    } else {
                        out << indent << "reg." << tgt << " " << op_str << " " << expr << ";\n";
                    }
                }
                for (const auto& inst : instructions) {
                    if (const auto* st = std::get_if<StoreOp>(&inst.op)) {
                        std::string expr = qualify_expression(st->expression, model);
                        if (is_float32_target(st->target.name)) {
                            expr = suffix_float32_literals(expr);
                        }
                        const auto* p = model.find_port(st->target.name);
                        bool is_out = (st->target.scope == LValueScope::OutPort) || (p != nullptr && p->is_out());
                        std::string_view op_str = assignment_op_to_string(st->op);
                        const std::string tgt = escape_member_path(st->target.full_path());
                        if (is_out) {
                            out << indent << "out." << tgt << " " << op_str << " " << expr << ";\n";
                        } else {
                            out << indent << "reg." << tgt << " " << op_str << " " << expr << ";\n";
                        }
                    } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                        std::string expr = qualify_expression(pw->expression, model);
                        if (is_float32_target(pw->port_name)) {
                            expr = suffix_float32_literals(expr);
                        }
                        out << indent << "out." << escape_cpp_keyword(pw->port_name) << " = " << expr << ";\n";
                    } else if (const auto* pr = std::get_if<PortReadOp>(&inst.op)) {
                        if (!pr->destination.name.empty()) {
                            out << indent << "reg." << escape_member_path(pr->destination.full_path()) << " = in."
                                << escape_cpp_keyword(pr->port_name) << ";\n";
                        }
                    } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
                        out << indent;
                        if (ac->result_target.has_value() && !ac->result_target->empty()) {
                            const std::string tgt = escape_member_path(ac->result_target->full_path());
                            const bool is_out_port =
                                (ac->result_target->scope == LValueScope::OutPort) ||
                                (model.find_port(ac->result_target->name) != nullptr &&
                                 model.find_port(ac->result_target->name)->direction != PortDirection::In);
                            out << (is_out_port ? "out." : "reg.") << tgt << " = ";
                        }
                        out << "srv." << escape_cpp_keyword(ac->function_name) << "(";
                        for (std::size_t i = 0; i < ac->arguments.size(); ++i) {
                            if (i > 0)
                                out << ", ";
                            out << qualify_expression(ac->arguments[i], model);
                        }
                        out << ");\n";
                    } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                        const std::string safe_model = sanitize_cpp_model_name(model.name);
                        const std::string safe_sig = escape_cpp_keyword(se->signal_name);
                        out << indent << "// emit signal: " << se->signal_name << "\n";
                        out << indent << "{\n";
                        out << indent << "    " << safe_model << "EmittedEvent _em{};\n";
                        out << indent << "    _em.tag = " << safe_model << "EmittedEventTag::" << safe_sig << ";\n";
                        out << indent << "    _em.port = \"" << escape_cpp_string_literal(se->target_port.value_or(""))
                            << "\";\n";
                        if (const auto* sig_def = model.find_signal(se->signal_name);
                            sig_def != nullptr && !sig_def->attributes.empty()) {
                            for (std::size_t ai = 0; ai < se->arguments.size() && ai < sig_def->attributes.size();
                                 ++ai) {
                                const auto& attr = sig_def->attributes[ai];
                                out << indent << "    _em." << safe_sig << "_payload." << escape_cpp_keyword(attr.name)
                                    << " = static_cast<" << attr.type.to_cpp_type() << ">("
                                    << qualify_expression(se->arguments[ai], model) << ");\n";
                            }
                        }
                        out << indent << "    out.push_outbox(_em);\n";
                        out << indent << "}\n";
                    }
                }
            };

            if (!uses_cmd && !uses_srv) {
                // Overload 1: (const InPorts& in, OutPorts& out, Registers& reg)
                out << "    template <typename InPorts, typename OutPorts, typename Registers>\n";
                out << "    void operator()(" << in_param << ", " << out_param << ", " << reg_param << ") const {\n";
                emit_assignment_lines("        ");
                out << "    }\n\n";
            }

            if (!uses_srv) {
                // Overload 2: (const Event& cmd, const InPorts& in, OutPorts& out, Registers& reg)
                out << "    template <typename Event, typename InPorts, typename OutPorts, typename Registers>\n";
                out << "    void operator()(" << cmd_param << ", " << in_param << ", " << out_param << ", " << reg_param
                    << ") const {\n";
                emit_assignment_lines("        ");
                out << "    }\n\n";
            }

            // Overload 3: Full 5-domain (const Event& cmd, const InPorts& in, OutPorts& out, Registers& reg,
            // Services& srv)
            out << "    template <typename Event, typename InPorts, typename OutPorts, typename Registers, "
                   "typename Services>\n";
            out << "    void operator()(" << cmd_param << ", " << in_param << ", " << out_param << ", " << reg_param
                << ", " << srv_param << ") const {\n";
            emit_assignment_lines("        ");
            out << "    }\n\n";

            if (!uses_cmd) {
                // Overload 4: (const InPorts& in, OutPorts& out, Registers& reg, Services& srv) for lifecycle
                // hooks
                out << "    template <typename InPorts, typename OutPorts, typename Registers, typename "
                       "Services>\n";
                out << "    void operator()(" << in_param << ", " << out_param << ", " << reg_param << ", " << srv_param
                    << ") const {\n";
                emit_assignment_lines("        ");
                out << "    }\n";
            }
        } else {
            // External service invocation
            std::string srv_call_name = escape_cpp_keyword(action_item.name);
            out << "    template <typename Services>\n";
            out << "    auto operator()(Services& srv) const -> decltype(srv." << srv_call_name << "()) {\n";
            out << "        srv." << srv_call_name << "();\n";
            out << "    }\n\n";

            out << "    template <typename Event, typename Services>\n";
            out << "    auto operator()(const Event& /*cmd*/, Services& srv) const -> decltype(srv." << srv_call_name
                << "()) {\n";
            out << "        srv." << srv_call_name << "();\n";
            out << "    }\n\n";

            out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services>\n";
            out << "    auto operator()(const InPorts& /*in*/, OutPorts& /*out*/, Registers& /*reg*/, "
                   "Services& srv) const -> decltype(srv."
                << srv_call_name << "()) {\n";
            out << "        srv." << srv_call_name << "();\n";
            out << "    }\n\n";

            out << "    template <typename Event, typename InPorts, typename OutPorts, typename Registers, "
                   "typename Services>\n";
            out << "    auto operator()(const Event& /*cmd*/, const InPorts& /*in*/, OutPorts& /*out*/, "
                   "Registers& /*reg*/, Services& srv) const -> decltype(srv."
                << srv_call_name << "()) {\n";
            out << "        srv." << srv_call_name << "();\n";
            out << "    }\n";
        }
        out << "};\n\n";
    }

    if (!forward_declared_actions.empty()) {
        out << "// Forward declaration of custom Actions\n";
        for (const auto& a : forward_declared_actions) {
            out << "struct " << escape_cpp_keyword(a) << ";\n";
        }
        out << "\n";
    }
}

}  // namespace fsm::backend::cpp
