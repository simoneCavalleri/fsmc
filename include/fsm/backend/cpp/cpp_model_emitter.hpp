#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <ostream>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class CppModelEmitter {
  public:
    static void emit_enums(std::ostream& out, const FsmIr& model) {
        if (model.enums.empty()) {
            return;
        }

        out << "// ============================================================================\n";
        out << "// Strongly-Typed Enumerations (SysML v2 / Formal IR)\n";
        out << "// ============================================================================\n\n";

        for (const auto& en : model.enums) {
            out << "/**\n";
            out << " * @enum " << en.name << "\n";
            out << " * @brief Enumeration definition for '" << en.name << "'.\n";
            out << " */\n";
            out << "enum class " << en.name << " : " << (en.underlying_type.empty() ? "uint8_t" : en.underlying_type)
                << " {\n";
            for (size_t i = 0; i < en.literals.size(); ++i) {
                const auto& lit = en.literals[i];
                out << "    " << lit.name;
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

            out << "[[nodiscard]] constexpr std::string_view to_string(" << en.name << " val) noexcept {\n";
            out << "    switch (val) {\n";
            for (const auto& lit : en.literals) {
                out << "        case " << en.name << "::" << lit.name << ": return \"" << lit.name << "\";\n";
            }
            out << "        default: return \"unknown\";\n";
            out << "    }\n";
            out << "}\n\n";
        }
    }

    static void emit_structs(std::ostream& out, const FsmIr& model) {
        if (model.structs.empty()) {
            return;
        }

        out << "// ============================================================================\n";
        out << "// Structured Data Definitions (SysML v2 struct / datatype def)\n";
        out << "// ============================================================================\n\n";

        for (const auto& st : model.structs) {
            out << "/**\n";
            out << " * @struct " << st.name << "\n";
            if (!st.description.empty()) {
                out << " * @brief " << st.description << "\n";
            } else {
                out << " * @brief Structured datatype '" << st.name << "'.\n";
            }
            out << " */\n";
            out << "struct " << st.name << " {\n";
            for (const auto& field : st.fields) {
                out << "    " << field.type << " " << field.name;
                if (!field.default_value.empty()) {
                    out << "{" << field.default_value << "}";
                } else if (field.type == "bool" || field.type == "Boolean") {
                    out << "{false}";
                } else if (field.type == "double" || field.type == "float" || field.type == "Real") {
                    out << "{0.0}";
                } else if (field.type == "int" || field.type == "int32_t" || field.type == "uint32_t" ||
                           field.type == "uint8_t" || field.type == "Integer") {
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
                out << "\n    bool operator==(const " << st.name << "& other) const noexcept {\n";
                out << "        return ";
                for (size_t i = 0; i < st.fields.size(); ++i) {
                    if (i > 0)
                        out << " &&\n               ";
                    out << st.fields[i].name << " == other." << st.fields[i].name;
                }
                out << ";\n";
                out << "    }\n";
                out << "    bool operator!=(const " << st.name << "& other) const noexcept {\n";
                out << "        return !(*this == other);\n";
                out << "    }\n";
            } else {
                out << "    bool operator==(const " << st.name << "&) const noexcept { return true; }\n";
                out << "    bool operator!=(const " << st.name << "&) const noexcept { return false; }\n";
            }
            out << "};\n\n";
        }
    }

    static void emit_domain_structures(std::ostream& out, const FsmIr& model) {
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
                    out << "    " << port.type << " " << port.name;
                    if (!port.default_value.empty()) {
                        out << "{" << port.default_value << "}";
                    } else if (port.type == "bool" || port.type == "Boolean") {
                        out << "{false}";
                    } else if (port.type == "double" || port.type == "float" || port.type == "Real") {
                        out << "{0.0}";
                    } else if (port.type == "int" || port.type == "int32_t" || port.type == "Integer") {
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
                    if (port.min_value.has_value() && port.max_value.has_value()) {
                        std::ostringstream v;
                        v << "(" << port.name << " >= " << *port.min_value << " && " << port.name
                          << " <= " << *port.max_value << ")";
                        in_validations.push_back(v.str());
                    } else if (port.min_value.has_value()) {
                        std::ostringstream v;
                        v << "(" << port.name << " >= " << *port.min_value << ")";
                        in_validations.push_back(v.str());
                    } else if (port.max_value.has_value()) {
                        std::ostringstream v;
                        v << "(" << port.name << " <= " << *port.max_value << ")";
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
            out << "using " << model.name << "InPorts = fsm::no_ports;\n\n";
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
                    out << "    " << port.type << " " << port.name;
                    if (!port.default_value.empty()) {
                        out << "{" << port.default_value << "}";
                    } else if (port.type == "bool" || port.type == "Boolean") {
                        out << "{false}";
                    } else if (port.type == "double" || port.type == "float" || port.type == "Real") {
                        out << "{0.0}";
                    } else if (port.type == "int" || port.type == "int32_t" || port.type == "Integer") {
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
                    if (port.min_value.has_value() && port.max_value.has_value()) {
                        std::ostringstream v;
                        v << "(" << port.name << " >= " << *port.min_value << " && " << port.name
                          << " <= " << *port.max_value << ")";
                        out_validations.push_back(v.str());
                    } else if (port.min_value.has_value()) {
                        std::ostringstream v;
                        v << "(" << port.name << " >= " << *port.min_value << ")";
                        out_validations.push_back(v.str());
                    } else if (port.max_value.has_value()) {
                        std::ostringstream v;
                        v << "(" << port.name << " <= " << *port.max_value << ")";
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
            out << "using " << model.name << "OutPorts = fsm::no_ports;\n\n";
        }

        // 3. Registers (State machine internal memory)
        if (!model.variables.empty()) {
            out << "/**\n";
            out << " * @struct " << model.name << "Registers\n";
            out << " * @brief Persistent internal datapath memory for '" << model.name << "'.\n";
            out << " */\n";
            out << "struct " << model.name << "Registers {\n";
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
        } else {
            out << "/** @typedef " << model.name << "Registers\n * @brief Empty registers sentinel. */\n";
            out << "using " << model.name << "Registers = fsm::no_registers;\n\n";
        }

        // 4. Services Interface
        std::vector<std::string> external_actions;
        for (const auto& act : model.actions) {
            bool has_assign = false;
            for (const auto& t : model.transitions) {
                if (t.action_sig.has_value() && t.action_sig->name == act.name && !t.action_sig->assignments.empty()) {
                    has_assign = true;
                    break;
                }
            }
            if (!has_assign) {
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
                out << "    virtual void " << act_name << "() {}\n";
            }
            out << "};\n\n";
        } else {
            out << "/** @typedef " << model.name << "Services\n * @brief Empty services sentinel. */\n";
            out << "using " << model.name << "Services = fsm::no_services;\n\n";
        }
    }

    static void emit_events(std::ostream& out, const FsmIr& model) {
        if (model.events.empty() && model.signals.empty()) {
            return;
        }

        out << "// ============================================================================\n";
        out << "// Events & Signals\n";
        out << "// ============================================================================\n\n";

        for (const auto& event_item : model.events) {
            if (event_item.name == "anonymous_event" || event_item.name == "completion_event") {
                continue;
            }
            out << "/**\n";
            out << " * @struct " << event_item.name << "\n";
            out << " * @brief Signal trigger '" << event_item.name << "'.\n";
            if (!event_item.description.empty()) {
                out << " * @details " << event_item.description << "\n";
            }
            out << " */\n";
            out << "struct " << event_item.name << " {\n";
            out << "    static constexpr std::string_view name = \"" << event_item.name << "\";\n";

            if (!event_item.description.empty()) {
                out << "    // Description: " << event_item.description << "\n";
            }
            out << "};\n\n";
        }

        for (const auto& sig : model.signals) {
            bool already_emitted = false;
            for (const auto& ev : model.events) {
                if (ev.name == sig.name) {
                    already_emitted = true;
                    break;
                }
            }
            if (!already_emitted) {
                out << "/**\n";
                out << " * @struct " << sig.name << "\n";
                out << " * @brief Parameterized signal payload for '" << sig.name << "'.\n";
                out << " */\n";
                out << "struct " << sig.name << " {\n";
                out << "    static constexpr std::string_view name = \"" << sig.name << "\";\n";
                for (const auto& attr : sig.attributes) {
                    if (attr.default_value.empty()) {
                        out << "    " << attr.type << " " << attr.name << "{};\n";
                    } else {
                        out << "    " << attr.type << " " << attr.name << "{" << attr.default_value << "};\n";
                    }
                }
                if (!sig.attributes.empty()) {
                    out << "\n    constexpr explicit " << sig.name << "(";
                    for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                        if (i > 0)
                            out << ", ";
                        out << sig.attributes[i].type << " " << sig.attributes[i].name << "_";
                    }
                    out << ") : ";
                    for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                        if (i > 0)
                            out << ", ";
                        out << sig.attributes[i].name << "(" << sig.attributes[i].name << "_)";
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

    static void emit_states(std::ostream& out, const FsmIr& model) {
        if (model.states.empty()) {
            return;
        }

        out << "// ============================================================================\n";
        out << "// States\n";
        out << "// ============================================================================\n\n";

        // Forward declare all states for recursive parent_type support
        for (const auto& state_item : model.states) {
            out << "struct " << state_item.name << ";\n";
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
            out << "/**\n";
            out << " * @struct " << state_item.name << "\n";
            out << " * @brief State representation for '" << state_item.name << "'.\n";
            if (!state_item.description.empty()) {
                out << " * @details " << state_item.description << "\n";
            }
            out << " */\n";
            out << "struct " << state_item.name << " {\n";
            out << "    static constexpr std::string_view name = \"" << state_item.name << "\";\n";
            if (!state_item.parent_state.empty()) {
                out << "    static constexpr std::string_view parent = \"" << state_item.parent_state << "\";\n";
                out << "    using parent_type = " << state_item.parent_state << ";\n";
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
                out << "\n    /** @brief State entry lifecycle hook */\n";
                out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services>\n";
                out << "    void on_entry(const InPorts& /*in*/, OutPorts& /*out*/, Registers& /*reg*/, Services& "
                       "/*srv*/) const {\n";
                for (const auto& act : state_item.entry_actions) {
                    out << "        // Entry action: " << act.name << "\n";
                }
                out << "    }\n";
            }

            // Exit lifecycle actions
            if (!state_item.exit_actions.empty()) {
                out << "\n    /** @brief State exit lifecycle hook */\n";
                out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services>\n";
                out << "    void on_exit(const InPorts& /*in*/, OutPorts& /*out*/, Registers& /*reg*/, Services& "
                       "/*srv*/) const {\n";
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
                const bool has_expr = guard_item.cpp_expression.has_value() && !guard_item.cpp_expression->empty();
                std::string expr = has_expr ? *guard_item.cpp_expression : "true";
                bool references_event = expr.find("cmd") != std::string::npos ||
                                        expr.find("event") != std::string::npos ||
                                        expr.find("payload") != std::string::npos;
                std::string non_event_expr = references_event ? "true" : expr;

                out << "/**\n";
                out << " * @struct " << guard_item.name << "\n";
                out << " * @brief Transition guard predicate for '" << guard_item.name << "'.\n";
                if (has_expr) {
                    out << " * @note Evaluates: `" << expr << "`\n";
                }
                out << " */\n";
                out << "struct " << guard_item.name << " {\n";

                out << "    // Domain guard evaluation (InPorts, Registers, Event payload)\n";
                out << "    template <typename InPorts, typename Registers>\n";
                out << "    [[nodiscard]] constexpr bool operator()(const InPorts& in, const Registers& reg) const "
                       "noexcept {\n";
                out << "        (void)in; (void)reg;\n";
                out << "        return " << non_event_expr << ";\n";
                out << "    }\n\n";

                out << "    template <typename Event, typename InPorts, typename Registers>\n";
                out << "    [[nodiscard]] constexpr bool operator()(const Event& cmd, const InPorts& in, const "
                       "Registers& reg) const noexcept {\n";
                out << "        (void)cmd; (void)in; (void)reg;\n";
                out << "        return " << expr << ";\n";
                out << "    }\n\n";

                out << "    template <typename InPorts>\n";
                out << "    [[nodiscard]] constexpr bool operator()(const InPorts& in) const noexcept {\n";
                out << "        (void)in;\n";
                out << "        return " << non_event_expr << ";\n";
                out << "    }\n\n";

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
                    if (id != "fsm" && id != "and_" && id != "or_" && id != "not_" && id != "xor_" &&
                        id != "no_guard") {
                        declared_guards.insert(id);
                    }
                }
            }
            for (const auto& g : declared_guards) {
                out << "struct " << g << ";\n";
            }
            out << "\n";
        }
    }

    static void emit_actions(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        if (model.actions.empty()) {
            return;
        }

        out << "// ============================================================================\n";
        out << "// Actions\n";
        out << "// ============================================================================\n\n";

        if (options.include_stubs) {
            for (const auto& action_item : model.actions) {
                out << "/**\n";
                out << " * @struct " << action_item.name << "\n";
                out << " * @brief Transition action effect for '" << action_item.name << "'.\n";
                out << " */\n";
                out << "struct " << action_item.name << " {\n";
                std::vector<ActionAssignment> assignments;
                for (const auto& t : model.transitions) {
                    if (t.action_sig.has_value() && t.action_sig->name == action_item.name) {
                        for (const auto& a : t.action_sig->assignments) {
                            assignments.push_back(a);
                        }
                    }
                }

                if (!assignments.empty()) {
                    bool uses_out = false;
                    bool uses_reg = false;
                    for (const auto& assign : assignments) {
                        const auto* p = model.find_port(assign.target_variable);
                        if (p != nullptr && p->is_out()) {
                            uses_out = true;
                        } else {
                            uses_reg = true;
                        }
                    }

                    std::string out_param = uses_out ? "OutPorts& out" : "OutPorts& /*out*/";
                    std::string reg_param = uses_reg ? "Registers& reg" : "Registers& /*reg*/";

                    out << "    template <typename Event, typename OutPorts, typename Registers>\n";
                    out << "    void operator()(const Event& /*cmd*/, " << out_param << ", " << reg_param
                        << ") const {\n";
                    for (const auto& assign : assignments) {
                        const auto* p = model.find_port(assign.target_variable);
                        if (p != nullptr && p->is_out()) {
                            out << "        out." << assign.target_variable << " = " << assign.expression << ";\n";
                        } else {
                            out << "        reg." << assign.target_variable << " = " << assign.expression << ";\n";
                        }
                    }
                    out << "    }\n\n";

                    out << "    template <typename OutPorts, typename Registers>\n";
                    out << "    void operator()(" << out_param << ", " << reg_param << ") const {\n";
                    for (const auto& assign : assignments) {
                        const auto* p = model.find_port(assign.target_variable);
                        if (p != nullptr && p->is_out()) {
                            out << "        out." << assign.target_variable << " = " << assign.expression << ";\n";
                        } else {
                            out << "        reg." << assign.target_variable << " = " << assign.expression << ";\n";
                        }
                    }
                    out << "    }\n\n";

                    out << "    template <typename Event, typename InPorts, typename OutPorts, typename Registers, "
                           "typename Services>\n";
                    out << "    void operator()(const Event& /*cmd*/, const InPorts& /*in*/, " << out_param << ", "
                        << reg_param << ", Services& /*srv*/) const {\n";
                    for (const auto& assign : assignments) {
                        const auto* p = model.find_port(assign.target_variable);
                        if (p != nullptr && p->is_out()) {
                            out << "        out." << assign.target_variable << " = " << assign.expression << ";\n";
                        } else {
                            out << "        reg." << assign.target_variable << " = " << assign.expression << ";\n";
                        }
                    }
                    out << "    }\n";
                } else {
                    // External service invocation
                    out << "    template <typename Services>\n";
                    out << "    auto operator()(Services& srv) const -> decltype(srv." << action_item.name << "()) {\n";
                    out << "        srv." << action_item.name << "();\n";
                    out << "    }\n\n";

                    out << "    template <typename Event, typename Services>\n";
                    out << "    auto operator()(const Event& /*cmd*/, Services& srv) const -> decltype(srv."
                        << action_item.name << "()) {\n";
                    out << "        srv." << action_item.name << "();\n";
                    out << "    }\n\n";

                    out << "    template <typename Event, typename InPorts, typename OutPorts, typename Registers, "
                           "typename Services>\n";
                    out << "    auto operator()(const Event& /*cmd*/, const InPorts& /*in*/, OutPorts& /*out*/, "
                           "Registers& /*reg*/, Services& srv) const -> decltype(srv."
                        << action_item.name << "()) {\n";
                    out << "        srv." << action_item.name << "();\n";
                    out << "    }\n";
                }
                out << "};\n\n";
            }
        } else {
            out << "// Forward declaration of custom Actions\n";
            std::set<std::string> declared_actions;
            for (const auto& action_item : model.actions) {
                std::regex ident_re(R"([A-Za-z_][A-Za-z0-9_]*)");
                for (std::sregex_iterator it(action_item.name.begin(), action_item.name.end(), ident_re), end;
                     it != end; ++it) {
                    std::string id = it->str();
                    if (id != "fsm" && id != "and_" && id != "or_" && id != "not_" && id != "seq_" &&
                        id != "no_action") {
                        declared_actions.insert(id);
                    }
                }
            }
            for (const auto& a : declared_actions) {
                out << "struct " << a << ";\n";
            }
            out << "\n";
        }
    }

    static void emit_transition_table(std::ostream& out, const FsmIr& model, const GeneratorOptions& /*options*/) {
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

        auto get_all_substates = [&](auto& self, const std::string& parent_name,
                                     bool deep) -> std::vector<std::string> {
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

        bool first_row = true;
        auto emit_single_row = [&](const std::string& src, const std::string& evt, const std::string& tgt,
                                   const std::string& act, const std::string& grd) {
            if (!first_row) {
                out << ",\n    ::fsm::row<" << src << ", " << evt << ", " << tgt << ">";
            } else {
                out << "\n    ::fsm::row<" << src << ", " << evt << ", " << tgt << ">";
                first_row = false;
            }
            if (grd != "::fsm::no_guard") {
                out << "::when<" << grd << ">";
            }
            if (act != "::fsm::no_action") {
                out << "::then<" << act << ">";
            }
        };

        auto emit_internal_row = [&](const std::string& src, const std::string& evt, const std::string& act,
                                     const std::string& grd) {
            if (!first_row) {
                out << ",\n    ::fsm::internal_row<" << src << ", " << evt << ">";
            } else {
                out << "\n    ::fsm::internal_row<" << src << ", " << evt << ">";
                first_row = false;
            }
            if (grd != "::fsm::no_guard") {
                out << "::when<" << grd << ">";
            }
            if (act != "::fsm::no_action") {
                out << "::then<" << act << ">";
            }
        };

        out << "/**\n";
        out << " * @typedef " << table_type_name << "\n";
        out << " * @brief Static transition table specification for '" << model.name << "'.\n";
        out << " */\n";
        out << "using " << table_type_name << " = ::fsm::transition_table<";

        std::vector<TransitionEdge> transitions = model.transitions;
        std::stable_sort(transitions.begin(), transitions.end(),
                         [](const auto& a, const auto& b) { return a.priority > b.priority; });

        for (const auto& t : transitions) {
            std::string event_type = t.event;
            if (event_type.empty()) {
                event_type = "::fsm::anonymous_event";
            }

            std::string action_type = t.action.value_or("::fsm::no_action");
            if (action_type.empty()) {
                action_type = "::fsm::no_action";
            }

            std::string guard_type = t.guard.value_or("::fsm::no_guard");
            if (guard_type.empty()) {
                guard_type = "::fsm::no_guard";
            }

            const auto* target_node = find_state_by_name(t.target);
            if (target_node != nullptr && target_node->is_composite) {
                std::vector<std::string> sub_states =
                    get_all_substates(get_all_substates, target_node->name, target_node->has_deep_history);

                if (target_node->has_history && !sub_states.empty()) {
                    for (const auto& sub : sub_states) {
                        std::string composite_guard = (guard_type != "::fsm::no_guard")
                                                          ? "::fsm::and_<" + guard_type + ", ::fsm::history_is<" +
                                                                target_node->name + ", " + sub + ">>"
                                                          : "::fsm::history_is<" + target_node->name + ", " + sub + ">";
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

    static void emit_fsm_aliases(std::ostream& out, const FsmIr& model, const GeneratorOptions& /*options*/) {
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

        out << "/**\n";
        out << " * @typedef " << model.name << "\n";
        out << " * @brief Primary Synchronous Core State Machine instance for '" << model.name
            << "' on caller stack.\n";
        out << " */\n";
        out << "using " << model.name << " = ::fsm::make_fsm<\n";
        out << "    " << table_type_name << ",\n";
        out << "    ::fsm::with_initial_state<" << init_state << ">,\n";
        out << "    ::fsm::with_ports<" << model.name << "InPorts, " << model.name << "OutPorts>,\n";
        out << "    ::fsm::with_registers<" << model.name << "Registers>,\n";
        out << "    ::fsm::with_services<" << model.name << "Services>,\n";
        out << "    ::fsm::with_observer<::fsm::dynamic_observer>\n";
        out << ">;\n\n";

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

    static void emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
        emit_domain_structures(out, model);
        emit_events(out, model);
        emit_states(out, model);
        emit_guards(out, model, options);
        emit_actions(out, model, options);
        emit_transition_table(out, model, options);
        emit_fsm_aliases(out, model, options);
    }
};

}  // namespace fsm::codegen
