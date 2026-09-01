#pragma once

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Serializer for nuXmv / NuSMV / SMV Formal Verification Language.
 *
 * Emits a complete, verifiable SMV module with:
 * - State enumerations (`VAR state : { ... };`)
 * - Event input variables (`VAR event : { ... };`)
 * - Extended state variables with finite integer/boolean domains
 * - Explicit transition case structures (`ASSIGN next(state) := case ... esac;`)
 * - Formal temporal logic specifications (`LTLSPEC`, `CTLSPEC`, `INVARSPEC`)
 */
class SmvSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        std::string model_name = model.name.empty() ? "main" : model.name;

        out << "-- ============================================================================\n";
        out << "-- nuXmv / SMV Formal Verification Model: " << model_name << "\n";
        out << "-- Generated automatically by fsmc (https://github.com/simoneCavalleri/fsmc)\n";
        out << "-- ============================================================================\n\n";

        out << "MODULE main\n\n";

        // 1. VAR declarations
        out << "VAR\n";

        // State enumeration
        if (!model.states.empty()) {
            out << "  state : {";
            for (std::size_t i = 0; i < model.states.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << sanitize_smv_ident(model.states[i].name);
            }
            out << "};\n";
        } else {
            out << "  state : {Idle};\n";
        }

        // Event enumeration
        out << "  event : {none";
        std::vector<std::string> event_names;
        for (const auto& t : model.transitions) {
            std::string evt = t.event.empty() ? t.get_trigger_name() : t.event;
            if (!evt.empty() && evt != "none" && evt != "Anonymous" &&
                std::find(event_names.begin(), event_names.end(), evt) == event_names.end()) {
                event_names.push_back(evt);
            }
        }
        for (const auto& ev : model.events) {
            if (!ev.name.empty() && ev.name != "none" &&
                std::find(event_names.begin(), event_names.end(), ev.name) == event_names.end()) {
                event_names.push_back(ev.name);
            }
        }
        for (const auto& e : event_names) {
            out << ", " << sanitize_smv_ident(e);
        }
        out << "};\n";

        // State variables & physical quantities
        for (const auto& var : model.variables) {
            out << "  " << sanitize_smv_ident(var.name) << " : ";
            if (var.type == "bool" || var.type == "boolean" || var.type_kind == VariableTypeKind::Boolean) {
                out << "boolean;";
            } else if (var.min_value.has_value() && var.max_value.has_value()) {
                out << *var.min_value << ".." << *var.max_value << ";";
            } else {
                out << "0..100;";
            }
            if (var.physical_unit.has_value()) {
                out << " -- physical unit: " << *var.physical_unit;
            }
            out << "\n";
        }

        // Typed Ports (InPorts / OutPorts with bounds)
        for (const auto& port : model.ports) {
            out << "  " << sanitize_smv_ident(port.name) << " : ";
            if (port.type == "bool" || port.type == "boolean" || port.type_kind == VariableTypeKind::Boolean) {
                out << "boolean;";
            } else if (port.min_value.has_value() && port.max_value.has_value()) {
                out << static_cast<long long>(*port.min_value) << ".." << static_cast<long long>(*port.max_value)
                    << ";";
            } else {
                out << "0..100;";
            }
            out << " -- port (" << port_direction_to_string(port.direction) << ")\n";
        }

        // Discrete tick counters for TimeTriggers
        std::map<std::string, std::uint64_t> state_max_timeouts;
        for (const auto& t : model.transitions) {
            if (std::holds_alternative<TimeTrigger>(t.trigger)) {
                const auto& tt = std::get<TimeTrigger>(t.trigger);
                std::string src = t.source.empty() ? t.source_id : t.source;
                if (!src.empty()) {
                    std::uint64_t dur = tt.duration_in_ms();
                    if (dur == 0)
                        dur = 1;
                    state_max_timeouts[src] = std::max(state_max_timeouts[src], dur);
                }
            }
        }

        for (const auto& [st_name, max_dur] : state_max_timeouts) {
            out << "  timer_" << sanitize_smv_ident(st_name) << " : 0.." << max_dur << ";\n";
        }
        out << "\n";

        // 2. ASSIGN declarations
        out << "ASSIGN\n";

        // Initial state
        std::string init_st = model.initial_state_id.empty() ? model.initial_state : model.initial_state_id;
        if (init_st.empty() && !model.states.empty()) {
            init_st = model.states.front().name;
        }
        if (init_st.empty()) {
            init_st = "Idle";
        }
        out << "  init(state) := " << sanitize_smv_ident(init_st) << ";\n";

        // Initial variables
        for (const auto& var : model.variables) {
            std::string val = var.initial_value.empty() ? "0" : var.initial_value;
            out << "  init(" << sanitize_smv_ident(var.name) << ") := " << val << ";\n";
        }

        // Initial timer counters
        for (const auto& [st_name, max_dur] : state_max_timeouts) {
            out << "  init(timer_" << sanitize_smv_ident(st_name) << ") := 0;\n";
            out << "  next(timer_" << sanitize_smv_ident(st_name) << ") := case\n"
                << "    state = " << sanitize_smv_ident(st_name) << " & timer_" << sanitize_smv_ident(st_name) << " < "
                << max_dur << " : timer_" << sanitize_smv_ident(st_name) << " + 1;\n"
                << "    TRUE : 0;\n"
                << "  esac;\n";
        }
        out << "\n";

        // State transitions: next(state) := case ... esac;
        out << "  next(state) := case\n";
        auto sorted_transitions = model.transitions;
        std::stable_sort(sorted_transitions.begin(), sorted_transitions.end(),
                         [](const auto& a, const auto& b) { return a.priority > b.priority; });

        for (const auto& t : sorted_transitions) {
            std::string src = t.source.empty() ? t.source_id : t.source;
            std::string dst = t.target.empty() ? t.target_id : t.target;
            std::string evt = t.event.empty() ? t.get_trigger_name() : t.event;

            if (src.empty() || dst.empty())
                continue;

            out << "    state = " << sanitize_smv_ident(src);
            if (std::holds_alternative<TimeTrigger>(t.trigger)) {
                const auto& tt = std::get<TimeTrigger>(t.trigger);
                std::uint64_t dur = tt.duration_in_ms();
                if (dur == 0)
                    dur = 1;
                out << " & timer_" << sanitize_smv_ident(src) << " >= " << dur;
            } else if (!evt.empty() && evt != "Anonymous" && evt != "none") {
                out << " & event = " << sanitize_smv_ident(evt);
            }
            if (t.guard.has_value() && !t.guard->empty()) {
                std::string guard_str = to_smv_predicate(*t.guard);
                out << " & (" << guard_str << ")";
            }
            out << " : " << sanitize_smv_ident(dst) << ";";
            if (t.action.has_value() && !t.action->empty()) {
                out << " -- action: " << *t.action;
            }
            out << "\n";
        }
        out << "    TRUE : state;\n";
        out << "  esac;\n\n";

        // 3. Formal Properties (LTLSPEC & INVARSPEC)
        if (!model.properties.empty()) {
            out << "-- ============================================================================\n";
            out << "-- Formal Specifications & Verification Goals\n";
            out << "-- ============================================================================\n\n";

            for (const auto& prop : model.properties) {
                std::string prop_name = sanitize_smv_ident(prop.name);
                std::string smv_formula = to_smv_formula(prop);

                if (prop.kind == PropertyKind::Invariant) {
                    out << "INVARSPEC -- " << prop_name << "\n";
                    out << "  " << smv_formula << ";\n\n";
                } else {
                    out << "LTLSPEC -- " << prop_name << "\n";
                    out << "  " << smv_formula << ";\n\n";
                }
            }
        }

        return out.str();
    }

  private:
    static std::string sanitize_smv_ident(std::string_view name) {
        std::string res;
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
                res += c;
            } else if (c == '.' || c == ':') {
                res += '_';
            }
        }
        return res.empty() ? "ident" : res;
    }

    static std::string to_smv_predicate(std::string_view expr) {
        std::string s(expr);
        // Replace && with &
        size_t pos = 0;
        while ((pos = s.find("&&", pos)) != std::string::npos) {
            s.replace(pos, 2, "&");
            pos += 1;
        }
        // Replace || with |
        pos = 0;
        while ((pos = s.find("||", pos)) != std::string::npos) {
            s.replace(pos, 2, "|");
            pos += 1;
        }
        return s;
    }

    static std::string to_smv_formula(const FormalProperty& prop) {
        if (prop.ast.has_value()) {
            return format_ast_for_smv(*prop.ast);
        }
        return to_smv_predicate(prop.raw_formula);
    }

    static std::string format_ast_for_smv(const PropertyAstNode& node) {
        if (node.op == TemporalOp::Atom) {
            if (node.atom.find('=') != std::string::npos || node.atom.find('<') != std::string::npos ||
                node.atom.find('>') != std::string::npos) {
                return to_smv_predicate(node.atom);
            }
            return "state = " + sanitize_smv_ident(node.atom);
        }
        if (node.op == TemporalOp::Not) {
            if (!node.children.empty()) {
                return "!(" + format_ast_for_smv(node.children[0]) + ")";
            }
            return "!(state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Globally) {
            if (!node.children.empty()) {
                return "G (" + format_ast_for_smv(node.children[0]) + ")";
            }
            return "G (state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Finally) {
            if (!node.children.empty()) {
                return "F (" + format_ast_for_smv(node.children[0]) + ")";
            }
            return "F (state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Next) {
            if (!node.children.empty()) {
                return "X (" + format_ast_for_smv(node.children[0]) + ")";
            }
            return "X (state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Until || node.op == TemporalOp::Implies || node.op == TemporalOp::Equivalent ||
            node.op == TemporalOp::And || node.op == TemporalOp::Or) {
            std::string op_str;
            switch (node.op) {
                case TemporalOp::Until:
                    op_str = " U ";
                    break;
                case TemporalOp::Implies:
                    op_str = " -> ";
                    break;
                case TemporalOp::Equivalent:
                    op_str = " <-> ";
                    break;
                case TemporalOp::And:
                    op_str = " & ";
                    break;
                case TemporalOp::Or:
                    op_str = " | ";
                    break;
                default:
                    break;
            }
            std::string result = "(";
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (i > 0)
                    result += op_str;
                result += format_ast_for_smv(node.children[i]);
            }
            result += ")";
            return result;
        }
        return "state = " + sanitize_smv_ident(node.atom);
    }
};

}  // namespace fsm::codegen

namespace fsm {
using SmvSerializer = ::fsm::codegen::SmvSerializer;
}  // namespace fsm
