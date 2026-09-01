#pragma once

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Formal parser for nuXmv / NuSMV / SMV Formal Verification Language.
 *
 * Ingests SMV modules, reconstructing states, events, transition cases,
 * extended state variables, and LTL/CTL temporal verification specifications.
 */
class SmvParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "smv"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::string content_str(content);
        std::istringstream stream(content_str);
        std::string line;

        enum class Section : std::uint8_t { None, Var, AssignNextState, AssignInit };
        Section current_section = Section::None;

        while (std::getline(stream, line)) {
            std::string trimmed = trim(line);
            if (trimmed.empty()) {
                continue;
            }

            // Extract model name from comment or MODULE declaration
            if (trimmed.rfind("-- nuXmv / SMV Formal Model:", 0) == 0) {
                std::string name = trim(trimmed.substr(28));
                if (!name.empty()) {
                    model.name = sanitize_identifier(name);
                }
                continue;
            }
            if (trimmed.rfind("MODULE", 0) == 0) {
                std::string mod_name = trim(trimmed.substr(6));
                if (!mod_name.empty() && mod_name != "main" && (model.name.empty() || model.name == "MyStateMachine")) {
                    model.name = sanitize_identifier(mod_name);
                }
                continue;
            }

            if (trimmed.rfind("--", 0) == 0) {
                // Check for lossless @fsm directives in comments
                if (DirectiveParser::is_directive(trimmed.substr(2))) {
                    std::string body = DirectiveParser::extract_directive_body(trimmed.substr(2));
                    if (body.rfind("var", 0) == 0) {
                        if (auto var = DirectiveParser::parse_variable_directive(body)) {
                            model.add_variable(std::move(*var));
                        }
                    } else if (body.rfind("signal", 0) == 0) {
                        if (auto sig = DirectiveParser::parse_signal_directive(body)) {
                            model.add_signal(std::move(*sig));
                        }
                    } else if (body.rfind("property", 0) == 0) {
                        if (auto prop = DirectiveParser::parse_property_directive(body)) {
                            model.add_property(std::move(*prop));
                        }
                    } else if (body.rfind("state", 0) == 0) {
                        parse_state_metadata_directive(body, model);
                    } else if (body.rfind("entry", 0) == 0) {
                        parse_action_directive(body, model, "entry");
                    } else if (body.rfind("exit", 0) == 0) {
                        parse_action_directive(body, model, "exit");
                    } else if (body.rfind("defer", 0) == 0) {
                        parse_defer_directive(body, model);
                    } else if (body.rfind("req", 0) == 0) {
                        parse_req_directive(body, model);
                    } else if (body.rfind("trans_action", 0) == 0) {
                        parse_trans_action_directive(body, model);
                    }
                }
                continue;
            }

            if (trimmed == "VAR") {
                current_section = Section::Var;
                continue;
            }
            if (trimmed == "ASSIGN") {
                current_section = Section::AssignInit;
                continue;
            }
            if (trimmed.find("next(state)") != std::string::npos && trimmed.find("case") != std::string::npos) {
                current_section = Section::AssignNextState;
                continue;
            }
            if (trimmed == "esac;" || trimmed == "esac") {
                current_section = Section::None;
                continue;
            }

            // Temporal logic specifications
            if (trimmed.rfind("LTLSPEC", 0) == 0) {
                std::string formula = trim(trimmed.substr(7));
                if (!formula.empty() && formula.back() == ';') {
                    formula.pop_back();
                }
                FormalProperty prop;
                prop.name = "ltl_spec_" + std::to_string(model.properties.size() + 1);
                prop.kind = PropertyKind::Liveness;
                prop.raw_formula = trim(formula);
                model.add_property(std::move(prop));
                continue;
            }
            if (trimmed.rfind("INVARSPEC", 0) == 0) {
                std::string formula = trim(trimmed.substr(9));
                if (!formula.empty() && formula.back() == ';') {
                    formula.pop_back();
                }
                FormalProperty prop;
                prop.name = "invar_spec_" + std::to_string(model.properties.size() + 1);
                prop.kind = PropertyKind::Invariant;
                prop.raw_formula = trim(formula);
                model.add_property(std::move(prop));
                continue;
            }

            // 1. VAR section: state and event enumerations & variables
            if (current_section == Section::Var) {
                if (trimmed.rfind("state :", 0) == 0 || trimmed.rfind("state:", 0) == 0) {
                    parse_enum_states(trimmed, model);
                } else if (trimmed.rfind("event :", 0) == 0 || trimmed.rfind("event:", 0) == 0) {
                    parse_enum_events(trimmed, model);
                } else {
                    parse_variable_decl(trimmed, model);
                }
                continue;
            }

            // 2. ASSIGN section: init(state) and init(var)
            if (current_section == Section::AssignInit) {
                if (trimmed.rfind("init(state)", 0) == 0) {
                    size_t eq = trimmed.find(":=");
                    if (eq != std::string::npos) {
                        std::string init_s = trim(trimmed.substr(eq + 2));
                        if (!init_s.empty() && init_s.back() == ';') {
                            init_s.pop_back();
                        }
                        model.initial_state = sanitize_identifier(trim(init_s));
                    }
                } else if (trimmed.rfind("init(", 0) == 0) {
                    size_t p_close = trimmed.find(')');
                    size_t eq = trimmed.find(":=");
                    if (p_close != std::string::npos && eq != std::string::npos && eq > p_close) {
                        std::string var_name = trim(trimmed.substr(5, p_close - 5));
                        std::string init_val = trim(trimmed.substr(eq + 2));
                        if (!init_val.empty() && init_val.back() == ';') {
                            init_val.pop_back();
                        }
                        for (auto& v : model.variables) {
                            if (v.name == var_name) {
                                v.initial_value = trim(init_val);
                                break;
                            }
                        }
                    }
                }
                continue;
            }

            // 3. ASSIGN next(state) := case transitions
            if (current_section == Section::AssignNextState) {
                if (trimmed.find("TRUE :") != std::string::npos || trimmed.find("TRUE:") != std::string::npos) {
                    continue;  // Skip default frame condition
                }
                parse_transition_case(trimmed, model);
                continue;
            }
        }

        if (model.states.empty()) {
            error_message = "SMV Parser: No states found in MODULE.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
    static std::string trim(std::string_view str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return std::string(str.substr(first, last - first + 1));
    }

    static void parse_enum_states(const std::string& line, FsmIr& model) {
        size_t open_b = line.find('{');
        size_t close_b = line.find('}');
        if (open_b == std::string::npos || close_b == std::string::npos || close_b <= open_b) {
            return;
        }
        std::string enum_content = line.substr(open_b + 1, close_b - open_b - 1);
        std::stringstream ss(enum_content);
        std::string item;
        while (std::getline(ss, item, ',')) {
            std::string st_name = sanitize_identifier(trim(item));
            if (!st_name.empty()) {
                model.add_or_get_state(st_name, "");
            }
        }
    }

    static void parse_enum_events(const std::string& line, FsmIr& model) {
        size_t open_b = line.find('{');
        size_t close_b = line.find('}');
        if (open_b == std::string::npos || close_b == std::string::npos || close_b <= open_b) {
            return;
        }
        std::string enum_content = line.substr(open_b + 1, close_b - open_b - 1);
        std::stringstream ss(enum_content);
        std::string item;
        while (std::getline(ss, item, ',')) {
            std::string ev_name = sanitize_identifier(trim(item));
            if (!ev_name.empty() && ev_name != "none") {
                model.add_event(ev_name);
                SignalDefinition sig;
                sig.name = ev_name;
                model.add_signal(std::move(sig));
            }
        }
    }

    static void parse_variable_decl(const std::string& line, FsmIr& model) {
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return;
        std::string name = sanitize_identifier(trim(line.substr(0, colon)));
        if (name.empty())
            return;
        if (name.rfind("timer_", 0) == 0) {
            std::string possible_st = name.substr(6);
            if (model.find_state(possible_st) != nullptr) {
                return;  // Skip synthetic tick timers
            }
        }

        std::string type_part = trim(line.substr(colon + 1));
        if (!type_part.empty() && type_part.back() == ';') {
            type_part.pop_back();
        }

        VariableDefinition var;
        var.name = name;
        if (type_part.find("boolean") != std::string::npos) {
            var.type = "bool";
            var.type_kind = VariableTypeKind::Boolean;
        } else if (type_part.find("..") != std::string::npos) {
            var.type = "uint32_t";
            var.type_kind = VariableTypeKind::Integer;
            size_t dotdot = type_part.find("..");
            const std::string min_s = trim(type_part.substr(0, dotdot));
            const std::string max_s = trim(type_part.substr(dotdot + 2));
            try {
                var.min_value = std::stoll(min_s);
                var.max_value = std::stoll(max_s);
            } catch (const std::exception&) {
                var.min_value = 0;
                var.max_value = 100;
            }
        } else {
            var.type = "uint32_t";
            var.type_kind = VariableTypeKind::Integer;
        }
        model.add_variable(std::move(var));
    }

    static void parse_transition_case(const std::string& line, FsmIr& model) {
        std::string clean_line = line;
        std::string action_from_comment;
        auto comment_pos = line.find("--");
        if (comment_pos != std::string::npos) {
            std::string comment = line.substr(comment_pos + 2);
            auto act_pos = comment.find("action:");
            if (act_pos == std::string::npos) {
                act_pos = comment.find("action=");
            }
            if (act_pos != std::string::npos) {
                action_from_comment = sanitize_identifier(trim(comment.substr(act_pos + 7)));
            }
            clean_line = line.substr(0, comment_pos);
        }

        size_t colon = clean_line.find(':');
        if (colon == std::string::npos)
            return;

        std::string cond_part = trim(clean_line.substr(0, colon));
        std::string target_part = trim(clean_line.substr(colon + 1));
        if (!target_part.empty() && target_part.back() == ';') {
            target_part.pop_back();
        }
        std::string target_state = sanitize_identifier(trim(target_part));
        if (target_state.empty())
            return;

        // Parse: state = S1 & event = E1 & [guard]
        std::string source_state;
        std::string event_name;
        std::string guard_expr;

        std::stringstream ss(cond_part);
        std::string clause;
        while (std::getline(ss, clause, '&')) {
            std::string c = trim(clause);
            if (c.rfind("state =", 0) == 0 || c.rfind("state=", 0) == 0) {
                size_t eq = c.find('=');
                source_state = sanitize_identifier(trim(c.substr(eq + 1)));
            } else if (c.rfind("event =", 0) == 0 || c.rfind("event=", 0) == 0) {
                size_t eq = c.find('=');
                std::string ev = sanitize_identifier(trim(c.substr(eq + 1)));
                if (ev != "none") {
                    event_name = ev;
                }
            } else if (!c.empty()) {
                while (c.size() >= 2 && c.front() == '(' && c.back() == ')') {
                    c = trim(c.substr(1, c.size() - 2));
                }
                if (!c.empty()) {
                    if (!guard_expr.empty())
                        guard_expr += " && ";
                    guard_expr += c;
                }
            }
        }

        if (source_state.empty())
            return;

        TransitionEdge trans;
        trans.source = source_state;
        trans.target = target_state;
        trans.event = event_name;
        if (!guard_expr.empty()) {
            trans.guard = guard_expr;
            model.add_guard(sanitize_identifier(guard_expr));
        }
        if (!action_from_comment.empty()) {
            trans.action = action_from_comment;
            model.add_action(action_from_comment);
        }
        model.add_transition(trans);
    }

    static void parse_state_metadata_directive(const std::string& body, FsmIr& model) {
        // e.g. state name=Preflight parent=... composite=true initial=SensorCalib kind=parallel history=shallow
        // do_activity="..."
        auto n_pos = body.find("name=");
        if (n_pos == std::string::npos)
            return;
        std::string name = extract_word_or_quoted(body, n_pos + 5);
        if (name.empty())
            return;

        auto* st = model.find_state_mut(name);
        if (st == nullptr) {
            auto& new_st = model.add_or_get_state(name, "");
            st = &new_st;
        }

        auto p_pos = body.find("parent=");
        if (p_pos != std::string::npos) {
            st->parent_state = extract_word_or_quoted(body, p_pos + 7);
        }

        if (body.find("composite=true") != std::string::npos) {
            st->is_composite = true;
        }

        auto init_pos = body.find("initial=");
        if (init_pos != std::string::npos) {
            st->initial_sub_state = extract_word_or_quoted(body, init_pos + 8);
        }

        if (body.find("kind=parallel") != std::string::npos) {
            st->kind = StateKind::Parallel;
        }

        if (body.find("history=deep") != std::string::npos) {
            st->has_history = true;
            st->has_deep_history = true;
        } else if (body.find("history=shallow") != std::string::npos) {
            st->has_history = true;
        }

        auto do_pos = body.find("do_activity=");
        if (do_pos != std::string::npos) {
            st->do_activity = extract_word_or_quoted(body, do_pos + 12);
        }
    }

    static void parse_action_directive(const std::string& body, FsmIr& model, const std::string& type) {
        // e.g. entry state=Preflight action="ArmMotors"
        auto s_pos = body.find("state=");
        auto a_pos = body.find("action=");
        if (s_pos == std::string::npos || a_pos == std::string::npos)
            return;

        std::string st_name = extract_word_or_quoted(body, s_pos + 6);
        std::string act_name = extract_word_or_quoted(body, a_pos + 7);
        if (st_name.empty() || act_name.empty())
            return;

        auto* st = model.find_state_mut(st_name);
        if (st == nullptr) {
            auto& new_st = model.add_or_get_state(st_name, "");
            st = &new_st;
        }

        if (type == "entry") {
            st->entry_actions.emplace_back(act_name);
        } else if (type == "exit") {
            st->exit_actions.emplace_back(act_name);
        }
        model.add_action(act_name);
    }

    static void parse_defer_directive(const std::string& body, FsmIr& model) {
        // e.g. defer state=Navigating event="EvTelemetryPing"
        auto s_pos = body.find("state=");
        auto e_pos = body.find("event=");
        if (s_pos == std::string::npos || e_pos == std::string::npos)
            return;

        std::string st_name = extract_word_or_quoted(body, s_pos + 6);
        std::string ev_name = extract_word_or_quoted(body, e_pos + 6);
        if (st_name.empty() || ev_name.empty())
            return;

        auto* st = model.find_state_mut(st_name);
        if (st == nullptr) {
            auto& new_st = model.add_or_get_state(st_name, "");
            st = &new_st;
        }

        if (std::find(st->deferred_events.begin(), st->deferred_events.end(), ev_name) == st->deferred_events.end()) {
            st->deferred_events.push_back(ev_name);
        }
        model.add_event(ev_name);
    }

    static void parse_req_directive(const std::string& body, FsmIr& model) {
        // e.g. req state=Preflight req="REQ_UAV_PRE_01"
        auto s_pos = body.find("state=");
        auto r_pos = body.find("req=");
        if (s_pos == std::string::npos || r_pos == std::string::npos)
            return;

        std::string st_name = extract_word_or_quoted(body, s_pos + 6);
        std::string req_name = extract_word_or_quoted(body, r_pos + 4);
        if (st_name.empty() || req_name.empty())
            return;

        auto* st = model.find_state_mut(st_name);
        if (st == nullptr) {
            auto& new_st = model.add_or_get_state(st_name, "");
            st = &new_st;
        }

        if (std::find(st->traceability_reqs.begin(), st->traceability_reqs.end(), req_name) ==
            st->traceability_reqs.end()) {
            st->traceability_reqs.push_back(req_name);
        }
    }

    static void parse_trans_action_directive(const std::string& body, FsmIr& model) {
        // e.g. trans_action src=Preflight tgt=InFlight evt=TakeoffCmd action="LaunchUav"
        auto s_pos = body.find("src=");
        auto t_pos = body.find("tgt=");
        auto a_pos = body.find("action=");
        if (s_pos == std::string::npos || t_pos == std::string::npos || a_pos == std::string::npos)
            return;

        std::string src = extract_word_or_quoted(body, s_pos + 4);
        std::string tgt = extract_word_or_quoted(body, t_pos + 4);
        std::string act = extract_word_or_quoted(body, a_pos + 7);
        if (src.empty() || tgt.empty() || act.empty())
            return;

        for (auto& trans : model.transitions) {
            if (trans.source == src && trans.target == tgt) {
                trans.action = act;
                break;
            }
        }
        model.add_action(act);
    }

    static std::string extract_word_or_quoted(const std::string& str, size_t pos) {
        if (pos >= str.size())
            return "";
        while (pos < str.size() && (str[pos] == ' ' || str[pos] == '\t'))
            pos++;
        if (pos >= str.size())
            return "";
        if (str[pos] == '"') {
            size_t close_q = str.find('"', pos + 1);
            if (close_q == std::string::npos)
                return str.substr(pos + 1);
            return str.substr(pos + 1, close_q - pos - 1);
        }
        size_t end = str.find_first_of(" \t\r\n;", pos);
        if (end == std::string::npos)
            return str.substr(pos);
        return str.substr(pos, end - pos);
    }
};

}  // namespace fsm::codegen
