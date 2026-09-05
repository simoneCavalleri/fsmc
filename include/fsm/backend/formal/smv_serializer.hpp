#pragma once

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {

/**
 * @brief Serializer for nuXmv / NuSMV / SMV Formal Verification Language.
 *
 * Emits a complete, verifiable SMV module with:
 * - State enumerations (`VAR state : { ... };`)
 * - Event input variables (`VAR event : { ... };`)
 * - Extended state variables and input boolean guards
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

        // State enumeration: collect all states, choice nodes, and transition endpoints
        std::vector<std::string> state_names;
        auto add_state = [&](std::string_view st) {
            if (st.empty() || st == "[*]" || st == "None" || st == "none") {
                return;
            }
            std::string sanitized = sanitize_smv_ident(st);
            if (std::find(state_names.begin(), state_names.end(), sanitized) == state_names.end()) {
                state_names.push_back(sanitized);
            }
        };

        for (const auto& s : model.states) {
            add_state(s.name);
        }
        for (const auto& c : model.choice_nodes) {
            add_state(c.name);
        }
        for (const auto& t : model.transitions) {
            add_state(t.source);
            add_state(t.target);
        }
        if (state_names.empty()) {
            state_names.push_back("Idle");
        }

        out << "  state : {";
        for (std::size_t i = 0; i < state_names.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << sanitize_smv_ident(state_names[i]);
        }
        out << "};\n";

        // Global Event enumeration
        out << "  event : {none";
        std::vector<std::string> event_names;
        for (const auto& t : model.transitions) {
            std::string evt = t.event.empty() ? t.get_trigger_name() : t.event;
            if (!evt.empty() && evt != "none" && evt != "Anonymous" &&
                std::find(event_names.begin(), event_names.end(), evt) == event_names.end()) {
                event_names.push_back(evt);
            }
        }
        for (const auto& sig : model.signals) {
            if (!sig.name.empty() && sig.name != "none" &&
                std::find(event_names.begin(), event_names.end(), sig.name) == event_names.end()) {
                event_names.push_back(sig.name);
            }
        }
        for (const auto& e : event_names) {
            out << ", " << sanitize_smv_ident(e);
        }
        out << "};\n";

        // Track all known identifier names (states, events, vars, ports)
        std::set<std::string> known_idents;
        for (const auto& s : state_names)
            known_idents.insert(s);
        for (const auto& e : event_names)
            known_idents.insert(sanitize_smv_ident(e));
        known_idents.insert("state");
        known_idents.insert("event");
        known_idents.insert("none");
        known_idents.insert("TRUE");
        known_idents.insert("FALSE");
        known_idents.insert("true");
        known_idents.insert("false");

        // State variables & physical quantities
        for (const auto& var : model.variables) {
            std::string vname = sanitize_smv_ident(var.name);
            known_idents.insert(vname);
            out << "  " << vname << " : ";
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
            std::string pname = sanitize_smv_ident(port.name);
            known_idents.insert(pname);
            out << "  " << pname << " : ";
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

        // Collect and declare free boolean guard variables
        std::set<std::string> free_guard_vars;
        for (const auto& t : model.transitions) {
            if (t.guard.has_value() && !t.guard->empty()) {
                std::string smv_guard = to_smv_predicate(*t.guard);
                extract_smv_identifiers(smv_guard, known_idents, free_guard_vars);
            }
        }
        for (const auto& gvar : free_guard_vars) {
            known_idents.insert(gvar);
            out << "  " << gvar << " : boolean; -- input guard condition\n";
        }

        // Discrete tick counters for TimeTriggers
        std::map<std::string, std::uint64_t> state_max_timeouts;
        for (const auto& t : model.transitions) {
            if (std::holds_alternative<TimeTrigger>(t.trigger)) {
                const auto& tt = std::get<TimeTrigger>(t.trigger);
                const std::string& src = t.source;
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
        std::stable_sort(sorted_transitions.begin(), sorted_transitions.end(), [](const auto& a, const auto& b) {
            auto pa = (a.priority == 0) ? std::numeric_limits<std::uint32_t>::max() : a.priority;
            auto pb = (b.priority == 0) ? std::numeric_limits<std::uint32_t>::max() : b.priority;
            return pa < pb;
        });


        for (const auto& t : sorted_transitions) {
            const std::string& src = t.source;
            const std::string& dst = t.target;
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
                std::string smv_formula = to_smv_formula(prop, event_names);

                if (prop.kind == PropertyKind::Invariant) {
                    // INVARSPEC does not take the leading 'G' temporal operator
                    std::string invar_formula = smv_formula;
                    if (invar_formula.rfind("G (", 0) == 0 && invar_formula.back() == ')') {
                        invar_formula = invar_formula.substr(3, invar_formula.length() - 4);
                    } else if (invar_formula.rfind("G(", 0) == 0 && invar_formula.back() == ')') {
                        invar_formula = invar_formula.substr(2, invar_formula.length() - 3);
                    }
                    out << "INVARSPEC -- " << prop_name << "\n";
                    out << "  " << invar_formula << ";\n\n";
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

    static void extract_smv_identifiers(const std::string& expr, const std::set<std::string>& known,
                                        std::set<std::string>& out_free) {
        std::string cur;
        for (std::size_t i = 0; i <= expr.length(); ++i) {
            char c = (i < expr.length()) ? expr[i] : '\0';
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
                cur += c;
            } else {
                if (!cur.empty()) {
                    if (std::isalpha(static_cast<unsigned char>(cur[0])) != 0 || cur[0] == '_') {
                        if (known.find(cur) == known.end()) {
                            out_free.insert(cur);
                        }
                    }
                    cur.clear();
                }
            }
        }
    }

    static std::string to_smv_predicate(std::string_view expr) {
        std::string s(expr);

        // Strip domain prefixes (in., out., reg.)
        for (const char* prefix : {"in.", "out.", "reg."}) {
            size_t pos = 0;
            while ((pos = s.find(prefix, pos)) != std::string::npos) {
                s.erase(pos, std::string_view(prefix).length());
            }
        }

        // Recursively transform C++ guard template helpers: fsm::not_<X>, fsm::and_<X, Y>, fsm::or_<X, Y>
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& [prefix, op] :
                 {std::pair{"fsm::not_<", "!"}, std::pair{"fsm::and_<", " & "}, std::pair{"fsm::or_<", " | "}}) {
                size_t p = s.find(prefix);
                if (p != std::string::npos) {
                    size_t start = p + std::string(prefix).length();
                    int depth = 1;
                    size_t end = start;
                    while (end < s.length() && depth > 0) {
                        if (s[end] == '<')
                            depth++;
                        else if (s[end] == '>')
                            depth--;
                        end++;
                    }
                    if (depth == 0) {
                        std::string inner = s.substr(start, end - start - 1);
                        std::string repl;
                        if (std::string(prefix) == "fsm::not_<") {
                            repl = "!(" + inner + ")";
                        } else {
                            std::vector<std::string> args;
                            size_t arg_start = 0;
                            int inner_depth = 0;
                            for (size_t i = 0; i < inner.length(); ++i) {
                                if (inner[i] == '<' || inner[i] == '(')
                                    inner_depth++;
                                else if (inner[i] == '>' || inner[i] == ')')
                                    inner_depth--;
                                else if (inner[i] == ',' && inner_depth == 0) {
                                    args.push_back(inner.substr(arg_start, i - arg_start));
                                    arg_start = i + 1;
                                }
                            }
                            args.push_back(inner.substr(arg_start));
                            repl = "(";
                            for (size_t i = 0; i < args.size(); ++i) {
                                if (i > 0)
                                    repl += op;
                                std::string a = args[i];
                                a.erase(0, a.find_first_not_of(" \t"));
                                if (a.find_last_not_of(" \t") != std::string::npos) {
                                    a.erase(a.find_last_not_of(" \t") + 1);
                                }
                                repl += a;
                            }
                            repl += ")";
                        }
                        s.replace(p, end - p, repl);
                        changed = true;
                        break;
                    }
                }
            }
        }

        // Replace boolean and relational operators
        size_t pos = 0;
        while ((pos = s.find("&&", pos)) != std::string::npos) {
            s.replace(pos, 2, "&");
            pos += 1;
        }
        pos = 0;
        while ((pos = s.find("||", pos)) != std::string::npos) {
            s.replace(pos, 2, "|");
            pos += 1;
        }
        pos = 0;
        while ((pos = s.find("==", pos)) != std::string::npos) {
            s.replace(pos, 2, "=");
            pos += 1;
        }

        return s;
    }

    static std::string to_smv_formula(const FormalProperty& prop, const std::vector<std::string>& event_names) {
        if (prop.ast.has_value()) {
            return format_ast_for_smv(*prop.ast, event_names);
        }
        return to_smv_predicate(prop.raw_formula);
    }

    static std::string format_ast_for_smv(const PropertyAstNode& node, const std::vector<std::string>& event_names) {
        if (node.op == TemporalOp::Atom) {
            if (node.atom.find('=') != std::string::npos || node.atom.find('<') != std::string::npos ||
                node.atom.find('>') != std::string::npos) {
                return to_smv_predicate(node.atom);
            }
            std::string sanitized = sanitize_smv_ident(node.atom);
            if (std::find(event_names.begin(), event_names.end(), node.atom) != event_names.end() ||
                std::find(event_names.begin(), event_names.end(), sanitized) != event_names.end()) {
                return "event = " + sanitized;
            }
            return "state = " + sanitized;
        }
        if (node.op == TemporalOp::Not) {
            if (!node.children.empty()) {
                return "!(" + format_ast_for_smv(node.children[0], event_names) + ")";
            }
            return "!(state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Globally) {
            if (!node.children.empty()) {
                return "G (" + format_ast_for_smv(node.children[0], event_names) + ")";
            }
            return "G (state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Finally) {
            if (!node.children.empty()) {
                return "F (" + format_ast_for_smv(node.children[0], event_names) + ")";
            }
            return "F (state = " + sanitize_smv_ident(node.atom) + ")";
        }
        if (node.op == TemporalOp::Next) {
            if (!node.children.empty()) {
                return "X (" + format_ast_for_smv(node.children[0], event_names) + ")";
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
                result += format_ast_for_smv(node.children[i], event_names);
            }
            result += ")";
            return result;
        }
        return "state = " + sanitize_smv_ident(node.atom);
    }
};

}  // namespace fsm::backend::formal

namespace fsm::backend {
using formal::SmvSerializer;
}  // namespace fsm::backend
