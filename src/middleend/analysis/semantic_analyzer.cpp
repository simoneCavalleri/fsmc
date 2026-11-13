#include "fsm/middleend/analysis/semantic_analyzer.hpp"

#include <cctype>
#include <unordered_set>

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool SemanticAnalyzer::validate(const FsmIr& ir, std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    bool valid = true;

    // 0. Concurrency Semantics Validation
    if (!ir.concurrency.is_valid()) {
        errors.push_back(
            "Semantic contradiction in concurrency configuration: SynchronousReactive clock semantics incompatible "
            "with Interleaved orthogonal resolution.");
        valid = false;
    }

    auto is_known_data_type = [&](const DataType& dt) -> bool {
        if (dt.is_primitive()) {
            return true;
        }
        const auto& name = dt.custom_name();
        if (name.empty()) {
            return true;
        }
        if (name.rfind("std::", 0) == 0 || name.find('<') != std::string::npos) {
            return true;
        }
        return ir.has_type(name);
    };

    // 1. Verify Variable Types
    for (const auto& var : ir.variables) {
        if (!is_known_data_type(var.type)) {
            errors.push_back("Variable '" + var.name + "' references unknown type '" + var.type.to_canonical_string() +
                             "'");
            valid = false;
        }
    }

    // 2. Verify Port Types
    for (const auto& port : ir.ports) {
        if (!is_known_data_type(port.type)) {
            errors.push_back("Port '" + port.name + "' references unknown type '" + port.type.to_canonical_string() +
                             "'");
            valid = false;
        }
    }

    // 3. Verify Signal Attribute Types & SPSC/RealTime Trivially-Copyable Constraint
    auto is_trivially_copyable_type = [&](auto& self, const DataType& dt,
                                          std::unordered_set<std::string>& visited) -> bool {
        if (dt.is_primitive()) {
            return !dt.is_string();
        }
        const auto& cname = dt.custom_name();
        if (cname.rfind("std::string", 0) == 0 || cname.rfind("std::vector", 0) == 0 ||
            cname.rfind("std::map", 0) == 0 || cname.rfind("std::unordered_", 0) == 0) {
            return false;
        }
        if (visited.count(cname) > 0) {
            return true;
        }
        visited.insert(cname);
        if (const auto* ct = ir.find_type(cname); ct != nullptr && ct->kind == TypeKind::Struct) {
            for (const auto& field : ct->fields) {
                if (!self(self, field.type, visited)) {
                    return false;
                }
            }
        }
        return true;
    };

    const bool require_trivially_copyable_events =
        (ir.concurrency.dispatch == EventDispatchSemantics::ActiveObjectAsynchronous) ||
        (ir.attributes.count("spsc") > 0) || (ir.attributes.count("realtime") > 0) ||
        (ir.attributes.find("execution_policy") != ir.attributes.end() &&
         (ir.attributes.at("execution_policy") == "SPSC" || ir.attributes.at("execution_policy") == "RealTime"));

    for (const auto& sig : ir.signals) {
        for (const auto& attr : sig.attributes) {
            if (!is_known_data_type(attr.type)) {
                errors.push_back("Signal '" + sig.name + "' attribute '" + attr.name + "' references unknown type '" +
                                 attr.type.to_canonical_string() + "'");
                valid = false;
            }
            if (require_trivially_copyable_events) {
                std::unordered_set<std::string> visited;
                if (!is_trivially_copyable_type(is_trivially_copyable_type, attr.type, visited)) {
                    errors.push_back("Signal '" + sig.name + "' attribute '" + attr.name + "' of type '" +
                                     attr.type.to_canonical_string() +
                                     "' is not trivially copyable, which is mandatory under SPSC / RealTime policy");
                    valid = false;
                }
            }
        }
    }

    // 4. Verify Struct Field Types in custom_types
    for (const auto& ct : ir.custom_types) {
        if (ct.kind == TypeKind::Struct) {
            for (const auto& f : ct.fields) {
                if (!is_known_data_type(f.type)) {
                    errors.push_back("Type '" + ct.name + "' field '" + f.name + "' references unknown type '" +
                                     f.type.to_canonical_string() + "'");
                    valid = false;
                }
            }
        }
    }

    // 5. Verify Assignments in transitions
    auto check_assignment = [&](const ActionAssignment& assign, const std::string& ctx) {
        if (assign.target.name.empty())
            return;

        const auto* var = ir.find_variable(assign.target.name);
        const auto* port = ir.find_port(assign.target.name);

        if (var == nullptr && port == nullptr) {
            errors.push_back("Assignment target '" + assign.target.name + "' in " + ctx +
                             " not found in variables or ports");
            valid = false;
            return;
        }

        if (port != nullptr && port->is_in()) {
            errors.push_back("Cannot assign to read-only InPort '" + assign.target.name + "' in " + ctx);
            valid = false;
            return;
        }

        // Elementary type mismatch check if expr_ast is present
        if (assign.expr_ast.has_value()) {
            const DataType* target_type = var ? &var->type : (port ? &port->type : nullptr);
            if (target_type != nullptr) {
                const auto& ast = *assign.expr_ast;
                bool is_target_bool = target_type->is_boolean();
                bool is_target_numeric = target_type->is_integer() || target_type->is_floating_point();

                if (is_target_bool) {
                    if (ast.kind == ExpressionKind::IntegerLiteral || ast.kind == ExpressionKind::FloatLiteral) {
                        warnings.push_back("Type mismatch in assignment to boolean target '" + assign.target.name +
                                           "': assigned numeric literal");
                    } else if (ast.kind == ExpressionKind::EnumLiteral) {
                        warnings.push_back("Type mismatch in assignment to boolean target '" + assign.target.name +
                                           "': assigned enum literal");
                    }
                } else if (is_target_numeric) {
                    if (ast.kind == ExpressionKind::BooleanLiteral) {
                        warnings.push_back("Type mismatch in assignment to numeric target '" + assign.target.name +
                                           "': assigned boolean literal");
                    }
                }
            }
        }
    };

    for (const auto& tr : ir.transitions) {
        if (tr.condition_action.has_value()) {
            for (const auto& assign : tr.condition_action->assignments) {
                check_assignment(assign, "transition '" + tr.source + " -> " + tr.target + "'");
            }
        }
        if (tr.transition_action.has_value()) {
            for (const auto& assign : tr.transition_action->assignments) {
                check_assignment(assign, "transition '" + tr.source + " -> " + tr.target + "'");
            }
        }
    }

    for (const auto& st : ir.states) {
        for (const auto& act : st.entry_actions) {
            for (const auto& assign : act.assignments) {
                check_assignment(assign, "state '" + st.name + "' entry action");
            }
        }
        for (const auto& act : st.exit_actions) {
            for (const auto& assign : act.assignments) {
                check_assignment(assign, "state '" + st.name + "' exit action");
            }
        }
    }

    // 6. Verify Guard Purity (Prohibit mutating operators ++, --, assignments in guards)
    auto check_guard_purity = [&](std::string_view g_str, const std::string& ctx) {
        if (g_str.empty())
            return;
        std::string mut_op;
        if (g_str.find("++") != std::string_view::npos) {
            mut_op = "++";
        } else if (g_str.find("--") != std::string_view::npos) {
            mut_op = "--";
        } else {
            static const std::string compound_ops[] = {"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
            for (const auto& op : compound_ops) {
                if (g_str.find(op) != std::string_view::npos) {
                    mut_op = op;
                    break;
                }
            }
            if (mut_op.empty()) {
                for (size_t i = 0; i < g_str.size(); ++i) {
                    if (g_str[i] == '=') {
                        bool prev_op = (i > 0 && (g_str[i - 1] == '=' || g_str[i - 1] == '<' || g_str[i - 1] == '>' ||
                                                  g_str[i - 1] == '!'));
                        bool next_op = (i + 1 < g_str.size() && g_str[i + 1] == '=');
                        if (!prev_op && !next_op) {
                            mut_op = "=";
                            break;
                        }
                    }
                }
            }
        }
        if (!mut_op.empty()) {
            errors.push_back("Guard expression in " + ctx + " contains side-effect mutating operator '" + mut_op + "'");
            valid = false;
        }
    };

    for (const auto& tr : ir.transitions) {
        if (tr.guard.has_value()) {
            check_guard_purity(*tr.guard, "transition '" + tr.source + " -> " + tr.target + "'");
        }
    }
    for (const auto& gm : ir.guards) {
        if (gm.raw_expression.has_value()) {
            check_guard_purity(*gm.raw_expression, "guard '" + gm.name + "'");
        }
    }

    // 7. Verify Typed Event Payload Member References
    auto is_ident_start = [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    };
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };

    auto check_payload_references = [&](std::string_view expr, const TransitionEdge* edge, const std::string& ctx) {
        if (expr.empty()) {
            return;
        }
        std::string custom_binding;
        const SignalDefinition* bound_signal = nullptr;
        bool has_signal_trigger = false;
        if (edge != nullptr && std::holds_alternative<SignalTrigger>(edge->trigger)) {
            has_signal_trigger = true;
            const auto& trig = std::get<SignalTrigger>(edge->trigger);
            if (!trig.payload_binding.empty()) {
                custom_binding = trig.payload_binding;
            }
            bound_signal = ir.find_signal(trig.signal_name);
        }

        const std::size_t n = expr.size();
        for (std::size_t i = 0; i < n;) {
            if (expr[i] == '"' || expr[i] == '\'') {
                const char q = expr[i++];
                while (i < n) {
                    if (expr[i] == '\\' && i + 1 < n) {
                        i += 2;
                    } else if (expr[i++] == q) {
                        break;
                    }
                }
                continue;
            }
            if (is_ident_start(expr[i])) {
                const std::size_t start = i;
                while (i < n && is_ident_char(expr[i])) {
                    ++i;
                }
                std::string_view prefix = expr.substr(start, i - start);
                std::size_t dot_pos = i;
                while (dot_pos < n && std::isspace(static_cast<unsigned char>(expr[dot_pos])) != 0) {
                    ++dot_pos;
                }
                if (dot_pos < n && expr[dot_pos] == '.') {
                    std::size_t m_start = dot_pos + 1;
                    while (m_start < n && std::isspace(static_cast<unsigned char>(expr[m_start])) != 0) {
                        ++m_start;
                    }
                    if (m_start < n && is_ident_start(expr[m_start])) {
                        std::size_t m_end = m_start;
                        while (m_end < n && is_ident_char(expr[m_end])) {
                            ++m_end;
                        }
                        std::string_view member = expr.substr(m_start, m_end - m_start);
                        const bool is_payload_prefix =
                            (prefix == "payload" || prefix == "cmd" || prefix == "event" || prefix == "ev" ||
                             prefix == "sig" || (!custom_binding.empty() && prefix == custom_binding));
                        if (is_payload_prefix) {
                            if (!has_signal_trigger) {
                                errors.push_back("Payload/event member access '" + std::string(prefix) + "." +
                                                 std::string(member) + "' in " + ctx + " which has no signal trigger");
                                valid = false;
                            } else if (bound_signal != nullptr && !bound_signal->attributes.empty() &&
                                       member != "is_valid") {
                                bool found_attr = false;
                                for (const auto& attr : bound_signal->attributes) {
                                    if (attr.name == member) {
                                        found_attr = true;
                                        break;
                                    }
                                }
                                if (!found_attr) {
                                    errors.push_back("Signal '" + bound_signal->name + "' has no payload attribute '" +
                                                     std::string(member) + "' referenced in " + ctx);
                                    valid = false;
                                }
                            }
                        }
                    }
                }
            } else {
                ++i;
            }
        }
    };

    auto check_action_payloads = [&](const ActionSignature& act, const TransitionEdge* edge, const std::string& ctx) {
        for (const auto& assign : act.assignments) {
            check_payload_references(assign.expression, edge, ctx);
        }
        for (const auto& inst : act.instructions) {
            if (const auto* st = std::get_if<StoreOp>(&inst.op)) {
                check_payload_references(st->expression, edge, ctx);
            } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                check_payload_references(pw->expression, edge, ctx);
            } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
                for (const auto& arg : ac->arguments) {
                    check_payload_references(arg, edge, ctx);
                }
            } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                for (const auto& arg : se->arguments) {
                    check_payload_references(arg, edge, ctx);
                }
            }
        }
    };

    for (const auto& tr : ir.transitions) {
        const std::string ctx = "transition '" + tr.source + " -> " + tr.target + "'";
        if (tr.guard.has_value()) {
            check_payload_references(*tr.guard, &tr, ctx);
        }
        if (tr.condition_action.has_value()) {
            check_action_payloads(*tr.condition_action, &tr, ctx);
        }
        if (tr.transition_action.has_value()) {
            check_action_payloads(*tr.transition_action, &tr, ctx);
        }
    }

    for (const auto& st : ir.states) {
        for (const auto& act : st.entry_actions) {
            check_action_payloads(act, nullptr, "state '" + st.name + "' entry action");
        }
        for (const auto& act : st.exit_actions) {
            check_action_payloads(act, nullptr, "state '" + st.name + "' exit action");
        }
    }

    return valid;
}

SemanticAnalysisResult SemanticAnalyzer::analyze(const FsmIr& ir) {
    SemanticAnalysisResult res;
    res.valid = validate(ir, res.errors, res.warnings);
    return res;
}

bool SemanticAnalyzer::validate(const FsmIr& ir, DiagnosticEngine& diag) {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok = validate(ir, errors, warnings);
    for (const auto& err : errors) {
        diag.report(Diagnostic::error("E0501", err));
    }
    for (const auto& warn : warnings) {
        diag.report(Diagnostic::warning("W0501", warn));
    }
    return ok;
}

}  // namespace fsm::middleend::analysis
