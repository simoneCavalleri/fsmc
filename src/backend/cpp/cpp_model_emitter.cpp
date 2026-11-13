#include "fsm/backend/cpp/cpp_model_emitter.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::backend::cpp {

using namespace fsm::ir;
using fsm::frontend::escape_cpp_keyword;

namespace {

bool is_ident_start(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_ident_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string qualify_expression(std::string expr, const FsmIr& model) {
    std::string out;
    out.reserve(expr.size() + 16);

    std::size_t i = 0;
    const std::size_t n = expr.size();
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            char quote = expr[i];
            out.push_back(expr[i++]);
            while (i < n) {
                char c = expr[i++];
                out.push_back(c);
                if (c == '\\' && i < n) {
                    out.push_back(expr[i++]);
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }

        if (is_ident_start(expr[i])) {
            std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            std::string tok = expr.substr(start, i - start);

            // Check preceding non-whitespace characters for '.', '->', or '::'
            std::size_t prev = start;
            while (prev > 0 && std::isspace(static_cast<unsigned char>(expr[prev - 1])) != 0) {
                --prev;
            }
            bool preceded_by_member_or_scope = false;
            if (prev > 0 && expr[prev - 1] == '.') {
                preceded_by_member_or_scope = true;
            } else if (prev >= 2 && expr[prev - 2] == '-' && expr[prev - 1] == '>') {
                preceded_by_member_or_scope = true;
            } else if (prev >= 2 && expr[prev - 2] == ':' && expr[prev - 1] == ':') {
                preceded_by_member_or_scope = true;
            }

            // Check following non-whitespace characters for '::' or '.'
            std::size_t next = i;
            while (next < n && std::isspace(static_cast<unsigned char>(expr[next])) != 0) {
                ++next;
            }
            bool followed_by_scope = (next + 1 < n && expr[next] == ':' && expr[next + 1] == ':');
            bool followed_by_dot = (next < n && expr[next] == '.');

            if (!preceded_by_member_or_scope && !followed_by_scope) {
                if (followed_by_dot &&
                    (tok == "f" || tok == "cmd" || tok == "event" || tok == "sig" || tok == "ev" || tok == "payload")) {
                    out += "cmd";
                    continue;
                }
                const std::string escaped_tok = escape_cpp_keyword(tok);
                if (model.find_variable(tok) != nullptr ||
                    (escaped_tok != tok && model.find_variable(escaped_tok) != nullptr)) {
                    out += "reg." + escaped_tok;
                    continue;
                }
                const auto* port = model.find_port(tok);
                if (port == nullptr && escaped_tok != tok) {
                    port = model.find_port(escaped_tok);
                }
                if (port != nullptr) {
                    if (port->is_in()) {
                        out += "in." + escaped_tok;
                        continue;
                    }
                    if (port->is_out()) {
                        out += "out." + escaped_tok;
                        continue;
                    }
                }
            }

            if (prev > 0 && expr[prev - 1] == '.') {
                out += escape_cpp_keyword(tok);
                continue;
            }

            out += tok;
            continue;
        }

        out.push_back(expr[i++]);
    }
    return out;
}

bool has_member_access_prefix(const std::string& expr, std::initializer_list<std::string_view> prefixes) {
    std::size_t i = 0;
    const std::size_t n = expr.size();
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            char quote = expr[i++];
            while (i < n) {
                char c = expr[i++];
                if (c == '\\' && i < n) {
                    ++i;
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }
        if (is_ident_start(expr[i])) {
            std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            std::string_view tok(expr.data() + start, i - start);
            bool preceded_by_dot_or_arrow = (start > 0 && expr[start - 1] == '.') ||
                                            (start >= 2 && expr[start - 2] == '-' && expr[start - 1] == '>') ||
                                            (start >= 2 && expr[start - 2] == ':' && expr[start - 1] == ':');
            if (!preceded_by_dot_or_arrow) {
                bool followed_by_member =
                    (i < n && expr[i] == '.') || (i + 1 < n && expr[i] == '-' && expr[i + 1] == '>');
                if (followed_by_member) {
                    for (auto p : prefixes) {
                        if (tok == p) {
                            return true;
                        }
                    }
                }
            }
            continue;
        }
        ++i;
    }
    return false;
}

}  // namespace

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
        out << "/**\n";
        out << " * @enum " << enum_name << "\n";
        out << " * @brief Enumeration definition for '" << en.name << "'.\n";
        out << " */\n";
        out << "enum class " << enum_name << " : " << (en.underlying_type.empty() ? "uint8_t" : en.underlying_type)
            << " {\n";
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
                out << " // " << lit.description;
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
        out << "/**\n";
        out << " * @struct " << st_name << "\n";
        if (!st.description.empty()) {
            out << " * @brief " << st.description << "\n";
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
                out << " // " << field.description;
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
        out << " * @struct " << model.name << "InPorts\n";
        out << " * @brief Immutable input sensor snapshot for '" << model.name << "'.\n";
        out << " */\n";
        out << "struct " << model.name << "InPorts {\n";
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
        out << "/** @typedef " << model.name << "InPorts\n * @brief Empty input ports sentinel. */\n";
        out << "using " << model.name << "InPorts = ::fsm::no_ports;\n\n";
    }

    // 2. OutPorts
    bool has_out_ports = false;
    for (const auto& port : model.ports) {
        if (port.is_out()) {
            has_out_ports = true;
            break;
        }
    }
    if (has_out_ports) {
        out << "/**\n";
        out << " * @struct " << model.name << "OutPorts\n";
        out << " * @brief Actuator output command write buffer for '" << model.name << "'.\n";
        out << " */\n";
        out << "struct " << model.name << "OutPorts {\n";
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
        out << "/** @typedef " << model.name << "OutPorts\n * @brief Empty output ports sentinel. */\n";
        out << "using " << model.name << "OutPorts = ::fsm::no_ports;\n\n";
    }

    // 3. Registers (State machine internal memory)
    if (!model.variables.empty()) {
        out << "/**\n";
        out << " * @struct " << model.name << "Registers\n";
        out << " * @brief Persistent internal datapath memory for '" << model.name << "'.\n";
        out << " */\n";
        out << "struct " << model.name << "Registers {\n";
        for (const auto& var : model.variables) {
            out << "    " << var.type.to_cpp_type() << " " << escape_cpp_keyword(var.name);
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
    } else {
        out << "/** @typedef " << model.name << "Registers\n * @brief Empty registers sentinel. */\n";
        out << "using " << model.name << "Registers = ::fsm::no_registers;\n\n";
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

    if (!external_actions.empty()) {
        out << "/**\n";
        out << " * @struct " << model.name << "Services\n";
        out << " * @brief External environment and hardware driver interface for '" << model.name << "'.\n";
        out << " */\n";
        out << "struct " << model.name << "Services {\n";
        out << "    virtual ~" << model.name << "Services() = default;\n";
        for (const auto& act_name : external_actions) {
            out << "    virtual void " << escape_cpp_keyword(act_name) << "() {}\n";
        }
        out << "};\n\n";
    } else {
        out << "/** @typedef " << model.name << "Services\n * @brief Empty services sentinel. */\n";
        out << "using " << model.name << "Services = ::fsm::no_services;\n\n";
    }
}

void CppModelEmitter::emit_events(std::ostream& out, const FsmIr& model) {
    if (model.signals.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// Events & Signals\n";
    out << "// ============================================================================\n\n";

    for (const auto& sig : model.signals) {
        if (sig.name == "anonymous_event" || sig.name == "completion_event") {
            continue;
        }
        std::string event_type_name = escape_cpp_keyword(sig.name);
        if (sig.attributes.empty()) {
            out << "/**\n";
            out << " * @struct " << event_type_name << "\n";
            out << " * @brief Signal trigger '" << sig.name << "'.\n";
            if (!sig.description.empty()) {
                out << " * @details " << sig.description << "\n";
            }
            out << " */\n";
            out << "struct " << event_type_name << " {\n";
            out << "    static constexpr std::string_view name = \"" << sig.name << "\";\n";

            if (!sig.description.empty()) {
                out << "    // Description: " << sig.description << "\n";
            }
            out << "};\n\n";
        } else {
            out << "/**\n";
            out << " * @struct " << event_type_name << "\n";
            out << " * @brief Parameterized signal payload for '" << sig.name << "'.\n";
            if (!sig.description.empty()) {
                out << " * @details " << sig.description << "\n";
            }
            out << " */\n";
            out << "struct " << event_type_name << " {\n";
            out << "    static constexpr std::string_view name = \"" << sig.name << "\";\n";
            for (const auto& attr : sig.attributes) {
                if (attr.default_value.empty()) {
                    out << "    " << attr.type.to_cpp_type() << " " << attr.name << "{};\n";
                } else {
                    out << "    " << attr.type.to_cpp_type() << " " << attr.name << "{" << attr.default_value << "};\n";
                }
            }
            if (!sig.attributes.empty()) {
                out << "\n    constexpr " << event_type_name << "() = default;\n";
                out << "\n    constexpr explicit " << event_type_name << "(";
                for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].type.to_cpp_type() << " " << sig.attributes[i].name << "_";
                }
                out << ") : ";
                for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].name << "(std::move(" << sig.attributes[i].name << "_))";
                }
                out << " {}\n";
            }
            if (!sig.validators.empty()) {
                out << "\n    /**\n";
                out << "     * @brief Validates signal payload attributes against constraints.\n";
                out << "     * @return true if payload is valid.\n";
                out << "     */\n";
                out << "    [[nodiscard]] constexpr bool is_valid() const noexcept {\n";
                out << "        return ";
                for (std::size_t i = 0; i < sig.validators.size(); ++i) {
                    if (i > 0)
                        out << " && ";
                    out << "(" << sig.validators[i] << ")";
                }
                out << ";\n";
                out << "    }\n";
            }
            out << "};\n\n";
        }
    }
}

void CppModelEmitter::emit_states(std::ostream& out, const FsmIr& model) {
    if (model.states.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// States\n";
    out << "// ============================================================================\n\n";

    // Forward declare all states for recursive parent_type support
    for (const auto& state_item : model.states) {
        out << "struct " << escape_cpp_keyword(state_item.name) << ";\n";
    }
    out << "\n";

    for (const auto& state_item : model.states) {
        if (state_item.is_composite) {
            std::vector<std::string> sub_states;
            for (const auto& s : model.states) {
                if (s.parent_state == state_item.name) {
                    sub_states.push_back(s.name);
                }
            }
            out << "/// @brief Composite State: " << state_item.name << "\n";
            if (!sub_states.empty()) {
                out << "/// Contains substates: ";
                for (std::size_t i = 0; i < sub_states.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sub_states[i];
                }
                out << "\n";
            }
            if (!state_item.initial_sub_state.empty()) {
                out << "/// Initial substate: " << state_item.initial_sub_state << "\n";
            }
            out << "\n";
        }
        if (!state_item.traceability_reqs.empty()) {
            out << "/// @satisfies ";
            for (std::size_t i = 0; i < state_item.traceability_reqs.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << state_item.traceability_reqs[i];
            }
            out << "\n";
        }
        std::string state_struct_name = escape_cpp_keyword(state_item.name);
        out << "/**\n";
        out << " * @struct " << state_struct_name << "\n";
        out << " * @brief State representation for '" << state_item.name << "'.\n";
        if (!state_item.description.empty()) {
            out << " * @details " << state_item.description << "\n";
        }
        for (const auto& req : state_item.traceability_reqs) {
            out << " * @trace " << req << "\n";
            out << " * @satisfies " << req << "\n";
        }
        out << " */\n";
        out << "struct " << state_struct_name << " {\n";
        out << "    static constexpr std::string_view name = \"" << state_item.name << "\";\n";
        if (!state_item.parent_state.empty()) {
            out << "    static constexpr std::string_view parent = \"" << state_item.parent_state << "\";\n";
            out << "    using parent_type = " << escape_cpp_keyword(state_item.parent_state) << ";\n";
        }
        if (!state_item.deferred_events.empty()) {
            out << "    using deferred_events = ::fsm::type_list<";
            for (std::size_t i = 0; i < state_item.deferred_events.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << escape_cpp_keyword(state_item.deferred_events[i]);
            }
            out << ">;\n";
        }

        // Entry lifecycle actions
        if (!state_item.entry_actions.empty()) {
            out << "\n    /** @brief State entry lifecycle hook */\n";
            out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services>\n";
            out << "    void on_enter(const InPorts& in, OutPorts& out, Registers& reg, Services& srv) const {\n";
            for (std::size_t i = 0; i < state_item.entry_actions.size(); ++i) {
                const auto& act = state_item.entry_actions[i];
                std::string act_type = escape_cpp_keyword(act.name);
                out << "        auto entry_action_" << i << " = " << act_type << "{};\n";
                out << "        ::fsm::call_action(entry_action_" << i << ", in, out, reg, srv);\n";
            }
            out << "    }\n";
        }

        // Exit lifecycle actions
        if (!state_item.exit_actions.empty()) {
            out << "\n    /** @brief State exit lifecycle hook */\n";
            out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services>\n";
            out << "    void on_exit(const InPorts& in, OutPorts& out, Registers& reg, Services& srv) const {\n";
            for (std::size_t i = 0; i < state_item.exit_actions.size(); ++i) {
                const auto& act = state_item.exit_actions[i];
                std::string act_type = escape_cpp_keyword(act.name);
                out << "        auto exit_action_" << i << " = " << act_type << "{};\n";
                out << "        ::fsm::call_action(exit_action_" << i << ", in, out, reg, srv);\n";
            }
            out << "    }\n";
        }

        // Continuous do_activity hook
        if (state_item.do_activity.has_value() && !state_item.do_activity->empty()) {
            out << "\n    /** @brief State continuous do_activity hook */\n";
            out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services>\n";
            out << "    void do_activity(const InPorts& in, OutPorts& out, Registers& reg, Services& srv) const {\n";
            out << "        auto activity = " << escape_cpp_keyword(*state_item.do_activity) << "{};\n";
            out << "        ::fsm::call_action(activity, in, out, reg, srv);\n";
            out << "    }\n";
        }

        if (state_item.kind == StateKind::EntryPoint) {
            out << "    static constexpr bool is_entry_point = true;\n";
        } else if (state_item.kind == StateKind::ExitPoint) {
            out << "    static constexpr bool is_exit_point = true;\n";
        }
        if (state_item.time_invariant.has_value() && !state_item.time_invariant->empty()) {
            out << "    /// @invariant " << *state_item.time_invariant << "\n";
            out << "    static constexpr std::string_view time_invariant = \"" << *state_item.time_invariant << "\";\n";
            std::uint64_t max_ms =
                to_milliseconds(state_item.time_invariant->duration, state_item.time_invariant->unit);
            out << "    static constexpr std::uint64_t max_stay_duration_ms = " << max_ms << "ULL;\n";
        }

        if (!state_item.description.empty()) {
            out << "    // Description: " << state_item.description << "\n";
        }
        out << "};\n\n";
    }
}

void CppModelEmitter::emit_guards(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    if (model.guards.empty()) {
        return;
    }

    if (options.include_stubs) {
        out << "// ============================================================================\n";
        out << "// Guards\n";
        out << "// ============================================================================\n\n";

        for (const auto& guard_item : model.guards) {
            if (guard_item.name == "else" || guard_item.name == "default" ||
                guard_item.name.find("::") != std::string::npos || guard_item.name.find('<') != std::string::npos ||
                guard_item.name.find('>') != std::string::npos || guard_item.name.find(' ') != std::string::npos ||
                guard_item.name.find('&') != std::string::npos || guard_item.name.find('|') != std::string::npos ||
                guard_item.name.find('!') != std::string::npos) {
                continue;
            }
            const bool has_expr = guard_item.normalized_expression.has_value() &&
                                  !guard_item.normalized_expression->empty() &&
                                  *guard_item.normalized_expression != guard_item.name &&
                                  (guard_item.name != (*guard_item.normalized_expression + "Guard"));
            std::string expr;
            if (has_expr) {
                expr = qualify_expression(*guard_item.normalized_expression, model);
            } else {
                const auto* p = model.find_port(guard_item.name);
                const auto* v = model.find_variable(guard_item.name);
                if (p != nullptr && p->is_in()) {
                    expr = "in." + escape_cpp_keyword(guard_item.name);
                } else if (p != nullptr && p->is_out()) {
                    expr = "out." + escape_cpp_keyword(guard_item.name);
                } else if (v != nullptr) {
                    expr = "reg." + escape_cpp_keyword(guard_item.name);
                } else {
                    expr = "true";
                }
            }
            bool references_event = has_member_access_prefix(expr, {"cmd", "event", "payload"});
            bool references_reg = has_member_access_prefix(expr, {"reg"});
            std::string non_event_expr = references_event ? "true" : expr;
            std::string in_only_expr = (references_event || references_reg) ? "true" : expr;

            std::string guard_struct_name = escape_cpp_keyword(guard_item.name);
            out << "/**\n";
            out << " * @struct " << guard_struct_name << "\n";
            out << " * @brief Transition guard predicate for '" << guard_item.name << "'.\n";
            if (has_expr) {
                out << " * @note Evaluates: `" << expr << "`\n";
            }
            out << " */\n";
            out << "struct " << guard_struct_name << " {\n";

            out << "    // Domain guard evaluation (InPorts, Registers, Event payload)\n";
            if (!references_event) {
                out << "    template <typename InPorts, typename Registers>\n";
                out << "    [[nodiscard]] constexpr bool operator()(const InPorts& in, const Registers& reg) const "
                       "noexcept {\n";
                out << "        (void)in; (void)reg;\n";
                out << "        return " << non_event_expr << ";\n";
                out << "    }\n\n";
            }

            out << "    template <typename Event, typename InPorts, typename Registers>\n";
            out << "    [[nodiscard]] constexpr bool operator()(const Event& cmd, const InPorts& in, const "
                   "Registers& reg) const noexcept {\n";
            out << "        (void)cmd; (void)in; (void)reg;\n";
            out << "        return " << expr << ";\n";
            out << "    }\n\n";

            if (!references_event) {
                out << "    template <typename InPorts>\n";
                out << "    [[nodiscard]] constexpr bool operator()(const InPorts& in) const noexcept {\n";
                out << "        (void)in;\n";
                out << "        return " << in_only_expr << ";\n";
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

    } else {
        out << "// Forward declaration of custom Guards\n";
        std::set<std::string> declared_guards;
        for (const auto& guard_item : model.guards) {
            std::regex ident_re(R"([A-Za-z_][A-Za-z0-9_]*)");
            for (std::sregex_iterator it(guard_item.name.begin(), guard_item.name.end(), ident_re), end; it != end;
                 ++it) {
                std::string id = it->str();
                if (id != "fsm" && id != "and_" && id != "or_" && id != "not_" && id != "xor_" && id != "no_guard" &&
                    id != "else" && id != "default") {
                    declared_guards.insert(id);
                }
            }
        }
        for (const auto& g : declared_guards) {
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

    if (options.include_stubs) {
        for (const auto& action_item : effective_actions) {
            std::string struct_name = escape_cpp_keyword(action_item.name);
            out << "/**\n";
            out << " * @struct " << struct_name << "\n";
            out << " * @brief Transition action effect for '" << action_item.name << "'.\n";
            out << " */\n";
            out << "struct " << struct_name << " {\n";
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

            bool has_datapath_instructions = false;
            for (const auto& inst : instructions) {
                if (std::holds_alternative<StoreOp>(inst.op) || std::holds_alternative<PortWriteOp>(inst.op) ||
                    std::holds_alternative<PortReadOp>(inst.op)) {
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
                    } else if (std::holds_alternative<PortReadOp>(inst.op)) {
                        uses_in = true;
                        uses_reg = true;
                    } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
                        uses_srv = true;
                        for (const auto& arg : ac->arguments) {
                            scan_rhs_usage(qualify_expression(arg, model));
                        }
                    } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                        uses_srv = true;
                        for (const auto& arg : se->arguments) {
                            scan_rhs_usage(qualify_expression(arg, model));
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
                            (i == 0 || (std::isalnum(static_cast<unsigned char>(expr_in[i - 1])) == 0 &&
                                        expr_in[i - 1] != '_'))) {
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
                                (j == expr_in.size() ||
                                 (expr_in[j] != 'f' && expr_in[j] != 'F' && expr_in[j] != 'l' && expr_in[j] != 'L' &&
                                  expr_in[j] != 'e' && expr_in[j] != 'E'))) {
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

                auto emit_assignment_lines = [&](std::string_view indent) {
                    for (const auto& assign : assignments) {
                        std::string expr = qualify_expression(assign.expression, model);
                        if (is_float32_target(assign.target.name)) {
                            expr = suffix_float32_literals(expr);
                        }
                        const auto* p = model.find_port(assign.target.name);
                        bool is_out = (assign.target.scope == LValueScope::OutPort) || (p != nullptr && p->is_out());
                        std::string_view op_str = assignment_op_to_string(assign.op);
                        if (is_out) {
                            out << indent << "out." << assign.target.full_path() << " " << op_str << " " << expr
                                << ";\n";
                        } else {
                            out << indent << "reg." << assign.target.full_path() << " " << op_str << " " << expr
                                << ";\n";
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
                            if (is_out) {
                                out << indent << "out." << st->target.full_path() << " " << op_str << " " << expr
                                    << ";\n";
                            } else {
                                out << indent << "reg." << st->target.full_path() << " " << op_str << " " << expr
                                    << ";\n";
                            }
                        } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                            std::string expr = qualify_expression(pw->expression, model);
                            if (is_float32_target(pw->port_name)) {
                                expr = suffix_float32_literals(expr);
                            }
                            out << indent << "out." << escape_cpp_keyword(pw->port_name) << " = " << expr << ";\n";
                        } else if (const auto* pr = std::get_if<PortReadOp>(&inst.op)) {
                            if (!pr->destination.name.empty()) {
                                out << indent << "reg." << pr->destination.full_path() << " = in."
                                    << escape_cpp_keyword(pr->port_name) << ";\n";
                            }
                        } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
                            out << indent << "srv." << escape_cpp_keyword(ac->function_name) << "(";
                            for (std::size_t i = 0; i < ac->arguments.size(); ++i) {
                                if (i > 0)
                                    out << ", ";
                                out << qualify_expression(ac->arguments[i], model);
                            }
                            out << ");\n";
                        } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                            out << indent << "// emit signal: " << se->signal_name << "\n";
                        }
                    }
                };

                if (!uses_cmd) {
                    // Overload 1: (const InPorts& in, OutPorts& out, Registers& reg)
                    out << "    template <typename InPorts, typename OutPorts, typename Registers>\n";
                    out << "    void operator()(" << in_param << ", " << out_param << ", " << reg_param
                        << ") const {\n";
                    emit_assignment_lines("        ");
                    out << "    }\n\n";
                }

                // Overload 2: (const Event& cmd, const InPorts& in, OutPorts& out, Registers& reg)
                out << "    template <typename Event, typename InPorts, typename OutPorts, typename Registers>\n";
                out << "    void operator()(" << cmd_param << ", " << in_param << ", " << out_param << ", " << reg_param
                    << ") const {\n";
                emit_assignment_lines("        ");
                out << "    }\n\n";

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
                    out << "    void operator()(" << in_param << ", " << out_param << ", " << reg_param << ", "
                        << srv_param << ") const {\n";
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
                out << "    auto operator()(const Event& /*cmd*/, Services& srv) const -> decltype(srv."
                    << srv_call_name << "()) {\n";
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
    } else {
        out << "// Forward declaration of custom Actions\n";
        std::set<std::string> declared_actions;
        for (const auto& action_item : model.actions) {
            std::regex ident_re(R"([A-Za-z_][A-Za-z0-9_]*)");
            for (std::sregex_iterator it(action_item.name.begin(), action_item.name.end(), ident_re), end; it != end;
                 ++it) {
                std::string id = it->str();
                if (id != "fsm" && id != "and_" && id != "or_" && id != "not_" && id != "seq_" && id != "no_action") {
                    declared_actions.insert(id);
                }
            }
        }
        for (const auto& a : declared_actions) {
            out << "struct " << escape_cpp_keyword(a) << ";\n";
        }
        out << "\n";
    }
}

void CppModelEmitter::emit_transition_table(std::ostream& out, const FsmIr& model,
                                            const GeneratorOptions& /*options*/) {
    out << "// ============================================================================\n";
    out << "// Transition Table\n";
    out << "// ============================================================================\n\n";

    std::string table_type_name = model.name + "Table";

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
        std::string a = (act.rfind("::", 0) == 0) ? act : escape_cpp_keyword(act);
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
        std::string a = (act.rfind("::", 0) == 0) ? act : escape_cpp_keyword(act);
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
    out << " * @brief Static transition table specification for '" << model.name << "'.\n";
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
            action_type = "::fsm::seq_<" + escape_cpp_keyword(t.condition_action->name) + ", " +
                          escape_cpp_keyword(t.transition_action->name) + ">";
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

    std::string table_type_name = model.name + "Table";

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
    if (!init_state.empty() && init_state.rfind("::", 0) != 0) {
        init_state = escape_cpp_keyword(init_state);
    }

    out << "/**\n";
    out << " * @typedef " << model.name << "\n";
    out << " * @brief Primary Synchronous Core State Machine instance for '" << model.name << "' on caller stack.\n";
    for (const auto& req : model.satisfies_reqs) {
        out << " * @trace " << req << "\n";
        out << " * @satisfies " << req << "\n";
    }
    out << " */\n";
    out << "using " << model.name << " = ::fsm::make_fsm<\n";
    out << "    " << table_type_name << ",\n";
    out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
    out << "    ::fsm::with_ports<" << model.name << "InPorts, " << model.name << "OutPorts>,\n";
    out << "    ::fsm::with_registers<" << model.name << "Registers>,\n";
    out << "    ::fsm::with_services<" << model.name << "Services>,\n";
    out << "    ::fsm::with_observer<::fsm::dynamic_observer>\n";
    out << ">;\n\n";

    if (options.thread_safe) {
        out << "/**\n";
        out << " * @typedef ThreadSafe" << model.name << "\n";
        out << " * @brief Thread-Safe Active Object State Machine for '" << model.name << "'.\n";
        out << " */\n";
        out << "using ThreadSafe" << model.name << " = ::fsm::make_thread_safe_fsm<\n";
        out << "    " << table_type_name << ",\n";
        out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
        out << "    ::fsm::with_ports<" << model.name << "InPorts, " << model.name << "OutPorts>,\n";
        out << "    ::fsm::with_registers<" << model.name << "Registers>,\n";
        out << "    ::fsm::with_services<" << model.name << "Services>\n";
        out << ">;\n\n";

        bool registers_trivially_copyable = true;
        const auto structs = model.get_structs();
        auto is_type_trivially_copyable = [&](auto& self, const DataType& type) -> bool {
            if (type.is_string()) {
                return false;
            }
            if (type.is_struct()) {
                for (const auto& st : structs) {
                    if (st.name == type.custom_name()) {
                        for (const auto& f : st.fields) {
                            if (!self(self, f.type)) {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        };
        for (const auto& var : model.variables) {
            if (!is_type_trivially_copyable(is_type_trivially_copyable, var.type)) {
                registers_trivially_copyable = false;
                break;
            }
        }
        bool events_trivially_copyable = true;
        for (const auto& sig : model.signals) {
            for (const auto& attr : sig.attributes) {
                if (!is_type_trivially_copyable(is_type_trivially_copyable, attr.type)) {
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
            out << " * @typedef Spsc" << model.name << "\n";
            out << " * @brief Lock-Free Single-Producer Single-Consumer State Machine for '" << model.name << "'.\n";
            out << " */\n";
            out << "using Spsc" << model.name << " = ::fsm::make_spsc_fsm<\n";
            out << "    " << table_type_name << ",\n";
            out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
            out << "    ::fsm::with_ports<" << model.name << "InPorts, " << model.name << "OutPorts>,\n";
            out << "    ::fsm::with_registers<" << model.name << "Registers>,\n";
            out << "    ::fsm::with_services<" << model.name << "Services>,\n";
            out << "    ::fsm::with_queue_capacity<64>\n";
            out << ">;\n";
        }
    }
}

void CppModelEmitter::emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    emit_domain_structures(out, model);
    emit_events(out, model);
    emit_guards(out, model, options);
    emit_actions(out, model, options);
    emit_states(out, model);
    emit_transition_table(out, model, options);
    emit_fsm_aliases(out, model, options);
}

}  // namespace fsm::backend::cpp
