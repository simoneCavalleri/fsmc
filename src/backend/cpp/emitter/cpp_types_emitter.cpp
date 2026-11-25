#include <algorithm>
#include <cctype>
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

void CppModelEmitter::emit_enums(std::ostream& out, const FsmIr& model) {
    const auto enums = model.get_enums();
    if (enums.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// Strongly-Typed Enumerations (SysML v2 / Formal IR)\n";
    out << "// ============================================================================\n\n";

    for (const auto& en : enums) {
        std::string enum_name = escape_cpp_keyword(en.name);
        if (en.is_external()) {
            out << "/** @brief External enumeration alias for '" << sanitize_cpp_comment(en.name) << "'. */\n";
            out << "using " << enum_name << " = " << resolve_external_cpp_symbol(en.external_ref->symbol) << ";\n\n";
            continue;
        }
        std::string underlying = en.underlying_type.empty() ? "uint8_t" : en.underlying_type;
        bool has_negative = false;
        int64_t min_val = 0;
        int64_t max_val = 0;
        bool has_explicit = false;
        for (const auto& lit : en.literals) {
            if (lit.value.has_value()) {
                if (!has_explicit) {
                    min_val = *lit.value;
                    max_val = *lit.value;
                    has_explicit = true;
                } else {
                    min_val = std::min(min_val, *lit.value);
                    max_val = std::max(max_val, *lit.value);
                }
                if (*lit.value < 0) {
                    has_negative = true;
                }
            }
        }
        const bool is_default_unsigned = en.underlying_type.empty() || underlying == "uint8_t" ||
                                         underlying == "uint16_t" || underlying == "uint32_t" ||
                                         underlying == "uint64_t" || underlying == "unsigned";
        if (has_negative && is_default_unsigned) {
            if (min_val >= -128 && max_val <= 127) {
                underlying = "int8_t";
            } else if (min_val >= -32768 && max_val <= 32767) {
                underlying = "int16_t";
            } else {
                underlying = "int32_t";
            }
        } else if (en.underlying_type.empty() && has_explicit && max_val > 255) {
            underlying = (max_val <= 65535) ? "uint16_t" : "uint32_t";
        }
        out << "/**\n";
        out << " * @enum " << enum_name << "\n";
        out << " * @brief Enumeration definition for '" << en.name << "'.\n";
        out << " */\n";
        out << "enum class " << enum_name << " : " << underlying << " {\n";
        for (size_t i = 0; i < en.literals.size(); ++i) {
            const auto& lit = en.literals[i];
            out << "    " << escape_cpp_keyword(lit.name);
            if (lit.value.has_value()) {
                out << " = " << *lit.value;
            }
            if (i + 1 < en.literals.size()) {
                out << ",";
            }
            if (!lit.description.empty()) {
                out << " // " << sanitize_cpp_comment(lit.description);
            }
            out << "\n";
        }
        out << "};\n\n";

        out << "[[nodiscard]] constexpr std::string_view to_string(" << enum_name << " val) noexcept {\n";
        out << "    switch (val) {\n";
        for (const auto& lit : en.literals) {
            out << "        case " << enum_name << "::" << escape_cpp_keyword(lit.name) << ": return \"" << lit.name
                << "\";\n";
        }
        out << "        default: return \"unknown\";\n";
        out << "    }\n";
        out << "}\n\n";
    }
}

void CppModelEmitter::emit_structs(std::ostream& out, const FsmIr& model) {
    const auto structs = model.get_structs();
    if (structs.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// Structured Data Definitions (SysML v2 struct / datatype def)\n";
    out << "// ============================================================================\n\n";

    for (const auto& st : structs) {
        std::string st_name = escape_cpp_keyword(st.name);
        if (st.is_external()) {
            out << "/** @brief External POD type alias for '" << sanitize_cpp_comment(st.name) << "'. */\n";
            out << "using " << st_name << " = " << resolve_external_cpp_symbol(st.external_ref->symbol) << ";\n\n";
            continue;
        }
        out << "/**\n";
        out << " * @struct " << st_name << "\n";
        if (!st.description.empty()) {
            out << " * @brief " << sanitize_cpp_comment(st.description) << "\n";
        } else {
            out << " * @brief Structured datatype '" << st.name << "'.\n";
        }
        out << " */\n";
        out << "struct " << st_name << " {\n";
        for (const auto& field : st.fields) {
            out << "    " << field.type.to_cpp_type() << " " << escape_cpp_keyword(field.name);
            if (!field.default_value.empty()) {
                out << "{" << field.default_value << "}";
            } else if (field.type.is_boolean()) {
                out << "{false}";
            } else if (field.type.is_floating_point()) {
                out << "{0.0}";
            } else if (field.type.is_integer()) {
                out << "{0}";
            } else {
                out << "{}";
            }
            out << ";";
            if (!field.description.empty()) {
                out << " // " << sanitize_cpp_comment(field.description);
            }
            out << "\n";
        }

        if (!st.fields.empty()) {
            out << "\n    bool operator==(const " << st_name << "& other) const noexcept {\n";
            out << "        return ";
            for (size_t i = 0; i < st.fields.size(); ++i) {
                if (i > 0)
                    out << " &&\n               ";
                std::string fn = escape_cpp_keyword(st.fields[i].name);
                out << fn << " == other." << fn;
            }
            out << ";\n";
            out << "    }\n";
            out << "    bool operator!=(const " << st_name << "& other) const noexcept {\n";
            out << "        return !(*this == other);\n";
            out << "    }\n";
        } else {
            out << "    bool operator==(const " << st_name << "&) const noexcept { return true; }\n";
            out << "    bool operator!=(const " << st_name << "&) const noexcept { return false; }\n";
        }
        out << "};\n\n";
    }
}

void CppModelEmitter::emit_domain_structures(std::ostream& out, const FsmIr& model) {
    emit_enums(out, model);
    emit_structs(out, model);

    out << "// ============================================================================\n";
    out << "// Partitioned I/O Ports, Internal Registers & Environment Services\n";
    out << "// ============================================================================\n\n";

    const std::string safe_model_name = sanitize_cpp_model_name(model.name);
    const std::string safe_model_comment = sanitize_cpp_comment(model.name);

    // 1. InPorts
    bool has_in_ports = false;
    for (const auto& port : model.ports) {
        if (port.is_in()) {
            has_in_ports = true;
            break;
        }
    }
    if (has_in_ports) {
        out << "/**\n";
        out << " * @struct " << safe_model_name << "InPorts\n";
        out << " * @brief Immutable input sensor snapshot for '" << safe_model_comment << "'.\n";
        out << " */\n";
        out << "struct " << safe_model_name << "InPorts {\n";
        for (const auto& port : model.ports) {
            if (port.is_in()) {
                std::string pname = escape_cpp_keyword(port.name);
                out << "    " << port.type.to_cpp_type() << " " << pname;
                if (!port.default_value.empty()) {
                    out << "{" << port.default_value << "}";
                } else if (port.type.is_boolean()) {
                    out << "{false}";
                } else if (port.type.is_floating_point()) {
                    out << "{0.0}";
                } else if (port.type.is_integer()) {
                    out << "{0}";
                }
                out << ";";
                if (!port.constraint.empty()) {
                    out << " // assert: " << port.constraint;
                }
                out << "\n";
            }
        }
        std::vector<std::string> in_validations;
        for (const auto& port : model.ports) {
            if (port.is_in()) {
                std::string pname = escape_cpp_keyword(port.name);
                if (port.min_value.has_value() && port.max_value.has_value()) {
                    std::ostringstream v;
                    v << "(" << pname << " >= " << *port.min_value << " && " << pname << " <= " << *port.max_value
                      << ")";
                    in_validations.push_back(v.str());
                } else if (port.min_value.has_value()) {
                    std::ostringstream v;
                    v << "(" << pname << " >= " << *port.min_value << ")";
                    in_validations.push_back(v.str());
                } else if (port.max_value.has_value()) {
                    std::ostringstream v;
                    v << "(" << pname << " <= " << *port.max_value << ")";
                    in_validations.push_back(v.str());
                }
            }
        }

        out << "\n    /**\n";
        out << "     * @brief Validates input port contracts and numeric bounds.\n";
        out << "     * @return true if all sensor values satisfy model constraints.\n";
        out << "     */\n";
        out << "    [[nodiscard]] constexpr bool validate_contracts() const noexcept {\n";
        if (in_validations.empty()) {
            out << "        return true;\n";
        } else {
            out << "        return ";
            for (size_t i = 0; i < in_validations.size(); ++i) {
                if (i > 0)
                    out << " &&\n               ";
                out << in_validations[i];
            }
            out << ";\n";
        }
        out << "    }\n";
        out << "};\n\n";
    } else {
        out << "/** @struct " << safe_model_name << "InPorts\n * @brief Empty input ports sentinel. */\n";
        out << "struct " << safe_model_name << "InPorts {};\n\n";
    }

    // 2. OutPorts (plus zero-heap Static Outbox when the model emits signals via SignalEmitOp)
    const auto emitted_summary = collect_emitted_signals(model);
    const bool has_emitted_signals = !emitted_summary.unique_signals.empty();
    if (has_emitted_signals) {
        out << "/**\n";
        out << " * @enum " << safe_model_name << "EmittedEventTag\n";
        out << " * @brief Discriminator for zero-heap emitted signals from '" << safe_model_comment << "'.\n";
        out << " */\n";
        out << "enum class " << safe_model_name << "EmittedEventTag : std::uint16_t {\n";
        out << "    None = 0,\n";
        for (const auto& sig_name : emitted_summary.unique_signals) {
            out << "    " << escape_cpp_keyword(sig_name) << ",\n";
        }
        out << "};\n\n";

        out << "/**\n";
        out << " * @struct " << safe_model_name << "EmittedEvent\n";
        out << " * @brief Static zero-heap outbox record for emitted signals.\n";
        out << " */\n";
        out << "struct " << safe_model_name << "EmittedEvent {\n";
        out << "    " << safe_model_name << "EmittedEventTag tag{" << safe_model_name << "EmittedEventTag::None};\n";
        out << "    const char* port{\"\"};\n";
        for (const auto& sig_name : emitted_summary.unique_signals) {
            const auto* sig_def = model.find_signal(sig_name);
            if (sig_def != nullptr && !sig_def->attributes.empty()) {
                const std::string safe_sig = escape_cpp_keyword(sig_name);
                out << "    struct " << safe_sig << "Payload {\n";
                for (const auto& attr : sig_def->attributes) {
                    out << "        " << attr.type.to_cpp_type() << " " << escape_cpp_keyword(attr.name) << "{};\n";
                }
                out << "    } " << safe_sig << "_payload{};\n";
            }
        }
        out << "};\n\n";
    }

    bool has_out_ports = false;
    for (const auto& port : model.ports) {
        if (port.is_out()) {
            has_out_ports = true;
            break;
        }
    }
    if (has_out_ports || has_emitted_signals) {
        out << "/**\n";
        out << " * @struct " << safe_model_name << "OutPorts\n";
        out << " * @brief Actuator output command write buffer for '" << safe_model_comment << "'.\n";
        out << " */\n";
        out << "struct " << safe_model_name << "OutPorts {\n";
        for (const auto& port : model.ports) {
            if (port.is_out()) {
                std::string pname = escape_cpp_keyword(port.name);
                out << "    " << port.type.to_cpp_type() << " " << pname;
                if (!port.default_value.empty()) {
                    out << "{" << port.default_value << "}";
                } else if (port.type.is_boolean()) {
                    out << "{false}";
                } else if (port.type.is_floating_point()) {
                    out << "{0.0}";
                } else if (port.type.is_integer()) {
                    out << "{0}";
                }
                out << ";";
                if (!port.constraint.empty()) {
                    out << " // assert: " << port.constraint;
                }
                out << "\n";
            }
        }
        if (has_emitted_signals) {
            out << "    static constexpr std::size_t kMaxOutbox = " << emitted_summary.max_outbox_capacity << "U;\n";
            out << "    std::array<" << safe_model_name << "EmittedEvent, kMaxOutbox> outbox{};\n";
            out << "    std::size_t outbox_count{0};\n\n";
            out << "    constexpr void clear_outbox() noexcept { outbox_count = 0; }\n";
            out << "    constexpr bool push_outbox(const " << safe_model_name << "EmittedEvent& ev) noexcept {\n";
            out << "        if (outbox_count < kMaxOutbox) {\n";
            out << "            outbox[outbox_count++] = ev;\n";
            out << "            return true;\n";
            out << "        }\n";
            out << "        return false;\n";
            out << "    }\n";
        }

        std::vector<std::string> out_validations;
        for (const auto& port : model.ports) {
            if (port.is_out()) {
                std::string pname = escape_cpp_keyword(port.name);
                if (port.min_value.has_value() && port.max_value.has_value()) {
                    std::ostringstream v;
                    v << "(" << pname << " >= " << *port.min_value << " && " << pname << " <= " << *port.max_value
                      << ")";
                    out_validations.push_back(v.str());
                } else if (port.min_value.has_value()) {
                    std::ostringstream v;
                    v << "(" << pname << " >= " << *port.min_value << ")";
                    out_validations.push_back(v.str());
                } else if (port.max_value.has_value()) {
                    std::ostringstream v;
                    v << "(" << pname << " <= " << *port.max_value << ")";
                    out_validations.push_back(v.str());
                }
            }
        }

        out << "\n    /**\n";
        out << "     * @brief Validates output port contracts and numeric bounds.\n";
        out << "     * @return true if all output actuator values satisfy model constraints.\n";
        out << "     */\n";
        out << "    [[nodiscard]] constexpr bool validate_contracts() const noexcept {\n";
        if (out_validations.empty()) {
            out << "        return true;\n";
        } else {
            out << "        return ";
            for (size_t i = 0; i < out_validations.size(); ++i) {
                if (i > 0)
                    out << " &&\n               ";
                out << out_validations[i];
            }
            out << ";\n";
        }
        out << "    }\n";
        out << "};\n\n";
    } else {
        out << "/** @struct " << safe_model_name << "OutPorts\n * @brief Empty output ports sentinel. */\n";
        out << "struct " << safe_model_name << "OutPorts {};\n\n";
    }

    // 3. Registers (State machine internal memory)
    if (!model.variables.empty()) {
        out << "/**\n";
        out << " * @struct " << safe_model_name << "Registers\n";
        out << " * @brief Persistent internal datapath memory for '" << safe_model_comment << "'.\n";
        out << " */\n";
        out << "struct " << safe_model_name << "Registers {\n";
        for (const auto& var : model.variables) {
            out << "    " << var.type.to_cpp_type() << " " << escape_cpp_keyword(var.name);
            if (!var.initial_value.empty()) {
                std::string qualified_init = qualify_enum_literal(var.initial_value, model, var.type.to_cpp_type());
                out << "{" << (qualified_init.empty() ? var.initial_value : qualified_init) << "}";
            }
            out << ";";
            if (!var.description.empty()) {
                out << " // " << sanitize_cpp_comment(var.description);
            }
            out << "\n";
        }
        out << "};\n\n";
    } else {
        out << "/** @struct " << safe_model_name << "Registers\n * @brief Empty registers sentinel. */\n";
        out << "struct " << safe_model_name << "Registers {};\n\n";
    }

    // 4. Services Interface
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

    std::vector<std::string> external_actions;
    std::set<std::string> seen_external_actions;
    for (const auto& act : effective_actions) {
        if (act.name.empty()) {
            continue;
        }
        bool has_assign = false;
        if (!has_assign) {
            for (const auto& t : model.transitions) {
                if ((t.condition_action.has_value() && t.condition_action->name == act.name &&
                     !t.condition_action->assignments.empty()) ||
                    (t.transition_action.has_value() && t.transition_action->name == act.name &&
                     !t.transition_action->assignments.empty())) {
                    has_assign = true;
                    break;
                }
            }
        }
        if (!has_assign) {
            for (const auto& st : model.states) {
                for (const auto& a : st.entry_actions) {
                    if (a.name == act.name && !a.assignments.empty()) {
                        has_assign = true;
                        break;
                    }
                }
                if (has_assign)
                    break;
                for (const auto& a : st.exit_actions) {
                    if (a.name == act.name && !a.assignments.empty()) {
                        has_assign = true;
                        break;
                    }
                }
                if (has_assign)
                    break;
            }
        }
        if (!has_assign && seen_external_actions.insert(act.name).second) {
            external_actions.push_back(act.name);
        }
    }

    std::vector<std::string> symbolic_guards;
    std::unordered_set<std::string> seen_symbolic_guards;
    for (const auto& guard_item : model.guards) {
        if (guard_item.name == "else" || guard_item.name == "default" ||
            guard_item.name.find("::") != std::string::npos || guard_item.name.find('<') != std::string::npos ||
            guard_item.name.find('>') != std::string::npos || guard_item.name.find(' ') != std::string::npos ||
            guard_item.name.find('&') != std::string::npos || guard_item.name.find('|') != std::string::npos ||
            guard_item.name.find('!') != std::string::npos) {
            continue;
        }
        const std::string guard_struct_name = escape_cpp_keyword(guard_item.name);
        const bool has_expr = guard_item.normalized_expression.has_value() &&
                              !guard_item.normalized_expression->empty() &&
                              *guard_item.normalized_expression != guard_item.name &&
                              (guard_item.name != (*guard_item.normalized_expression + "Guard"));
        bool is_domain_symbol = false;
        bool is_self_call_predicate = false;
        if (has_expr) {
            const std::string expr = qualify_expression(*guard_item.normalized_expression, model);
            is_self_call_predicate = has_self_constructor_call(expr, guard_struct_name);
        } else {
            is_domain_symbol =
                (model.find_port(guard_item.name) != nullptr || model.find_variable(guard_item.name) != nullptr);
        }
        if (((!has_expr && !is_domain_symbol) || is_self_call_predicate) &&
            seen_symbolic_guards.insert(guard_struct_name).second) {
            symbolic_guards.push_back(guard_struct_name);
        }
    }

    if (!model.services.empty() || !external_actions.empty() || !symbolic_guards.empty()) {
        std::unordered_set<std::string> emitted_typed_services;
        out << "/**\n";
        out << " * @struct " << safe_model_name << "Services\n";
        out << " * @brief External environment and hardware driver interface for '" << sanitize_cpp_comment(model.name)
            << "'.\n";
        out << " */\n";
        out << "struct " << safe_model_name << "Services {\n";
        out << "    virtual ~" << safe_model_name << "Services() = default;\n";
        for (const auto& svc : model.services) {
            const std::string m_name = escape_cpp_keyword(svc.name);
            emitted_typed_services.insert(svc.name);
            const std::string ret_cpp = svc.is_void_return() ? "void" : svc.return_type.to_cpp_type();
            out << "    ";
            if (!svc.is_void_return()) {
                out << "[[nodiscard]] ";
            }
            out << "virtual " << ret_cpp << " " << m_name << "(";
            for (std::size_t pi = 0; pi < svc.parameters.size(); ++pi) {
                if (pi > 0)
                    out << ", ";
                out << svc.parameters[pi].type.to_cpp_type() << " /*" << escape_cpp_keyword(svc.parameters[pi].name)
                    << "*/";
            }
            out << ")";
            if (svc.is_noexcept) {
                out << " noexcept";
            }
            if (svc.is_void_return()) {
                out << " {}\n";
            } else {
                out << " { return " << ret_cpp << "{}; }\n";
            }
        }
        for (const auto& act_name : external_actions) {
            if (emitted_typed_services.find(act_name) == emitted_typed_services.end()) {
                out << "    virtual void " << escape_cpp_keyword(act_name) << "() {}\n";
            }
        }
        for (const auto& g_name : symbolic_guards) {
            if (seen_external_actions.find(g_name) == seen_external_actions.end() &&
                emitted_typed_services.find(g_name) == emitted_typed_services.end()) {
                out << "    [[nodiscard]] virtual bool " << g_name << "() const { return false; }\n";
            }
        }
        out << "};\n\n";

        if (!model.services.empty()) {
            out << "namespace detail {\n";
            for (const auto& svc : model.services) {
                const std::string m_name = escape_cpp_keyword(svc.name);
                const std::string trait_name = "has_service_" + safe_model_name + "_" + m_name;
                std::string arg_declvals;
                for (std::size_t pi = 0; pi < svc.parameters.size(); ++pi) {
                    if (pi > 0)
                        arg_declvals += ", ";
                    arg_declvals += "std::declval<" + svc.parameters[pi].type.to_cpp_type() + ">()";
                }
                out << "template <typename Srv, typename = void>\n";
                out << "struct " << trait_name << " : std::false_type {};\n";
                out << "template <typename Srv>\n";
                if (svc.is_void_return()) {
                    out << "struct " << trait_name << "<Srv, std::void_t<decltype(std::declval<Srv&>()." << m_name
                        << "(" << arg_declvals << "))>> : std::true_type {};\n";
                } else {
                    const std::string ret_cpp = svc.return_type.to_cpp_type();
                    out << "struct " << trait_name
                        << "<Srv, std::enable_if_t<std::is_convertible_v<decltype(std::declval<Srv&>()." << m_name
                        << "(" << arg_declvals << ")), " << ret_cpp << ">>> : std::true_type {};\n";
                }
            }
            out << "}  // namespace detail\n\n";

            out << "/**\n";
            out << " * @struct " << safe_model_name << "ServicesContract\n";
            out << " * @brief Zero-vtable C++17 compile-time contract validator for user-injected Services.\n";
            out << " */\n";
            out << "template <typename Srv>\n";
            out << "struct " << safe_model_name << "ServicesContract {\n";
            for (const auto& svc : model.services) {
                const std::string m_name = escape_cpp_keyword(svc.name);
                const std::string trait_name = "detail::has_service_" + safe_model_name + "_" + m_name;
                const std::string ret_cpp = svc.is_void_return() ? "void" : svc.return_type.to_cpp_type();
                std::string sig_str = ret_cpp + " " + m_name + "(";
                for (std::size_t pi = 0; pi < svc.parameters.size(); ++pi) {
                    if (pi > 0)
                        sig_str += ", ";
                    sig_str += svc.parameters[pi].type.to_cpp_type() + " " + svc.parameters[pi].name;
                }
                sig_str += ")";
                out << "    static_assert(" << trait_name << "<Srv>::value,\n";
                out << "                  \"Services type must implement '" << escape_cpp_string_literal(sig_str)
                    << "'\");\n";
            }
            out << "    static constexpr bool satisfied = true;\n";
            out << "};\n\n";
        }
    } else {
        out << "/** @struct " << safe_model_name << "Services\n * @brief Empty services sentinel. */\n";
        out << "struct " << safe_model_name << "Services {};\n\n";
    }
}

}  // namespace fsm::backend::cpp
