#include "fsm/frontend/formal/smv_parser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "fsm/frontend/directive/directive_parser.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using directive::DirectiveParser;

bool SmvParser::parse(std::string_view content, FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) {
    std::string err;
    if (!parse_impl(content, out_ir, err)) {
        report_parse_error(diag, "E_PARSE_SMV", err);
        return false;
    }
    return true;
}

bool SmvParser::parse_impl(std::string_view content, FsmIr& model, std::string& error_message) {
    std::string content_str(content);
    std::istringstream stream(content_str);
    std::string line;

    enum class Section : std::uint8_t { None, Var, AssignNextState, AssignInit };
    Section current_section = Section::None;
    std::string case_accumulator;

    while (std::getline(stream, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        // Extract model name from comment or MODULE declaration
        if (trimmed.rfind("-- nuXmv / SMV Formal Verification Model:", 0) == 0) {
            std::string name = trim(trimmed.substr(41));
            if (!name.empty()) {
                model.name = sanitize_identifier(name);
            }
            continue;
        }
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
            if (directive::DirectiveParser::is_directive(trimmed.substr(2))) {
                std::string body = directive::DirectiveParser::extract_directive_body(trimmed.substr(2));
                if (body.rfind("var", 0) == 0) {
                    if (auto var = directive::DirectiveParser::parse_variable_directive(body)) {
                        model.add_variable(std::move(*var));
                    }
                } else if (body.rfind("port", 0) == 0) {
                    if (auto port = directive::DirectiveParser::parse_port_directive(body)) {
                        model.add_port(std::move(*port));
                    }
                } else if (body.rfind("enum", 0) == 0) {
                    if (auto en = directive::DirectiveParser::parse_enum_directive(body)) {
                        model.add_enum(std::move(*en));
                    }
                } else if (body.rfind("struct", 0) == 0) {
                    if (auto st = directive::DirectiveParser::parse_struct_directive(body)) {
                        model.add_struct(std::move(*st));
                    }
                } else if (body.rfind("signal", 0) == 0) {
                    if (auto sig = directive::DirectiveParser::parse_signal_directive(body)) {
                        model.add_signal(std::move(*sig));
                    }
                } else if (body.rfind("property", 0) == 0) {
                    if (auto prop = directive::DirectiveParser::parse_property_directive(body)) {
                        model.add_property(std::move(*prop));
                    }
                } else if (body.rfind("choice", 0) == 0) {
                    auto n_pos = body.find("name=");
                    std::string cname =
                        (n_pos != std::string::npos) ? extract_word_or_quoted(body, n_pos + 5) : trim(body.substr(6));
                    cname = sanitize_identifier(cname);
                    if (!cname.empty()) {
                        model.add_choice_node(cname);
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
        if (trimmed.rfind("LTLSPEC", 0) == 0 || trimmed.rfind("CTLSPEC", 0) == 0 ||
            trimmed.rfind("INVARSPEC", 0) == 0) {
            PropertyKind kind = PropertyKind::Liveness;
            std::string default_prefix = "ltl_spec_";
            std::size_t kw_len = 7;
            if (trimmed.rfind("CTLSPEC", 0) == 0) {
                kind = PropertyKind::Safety;
                default_prefix = "ctl_spec_";
                kw_len = 7;
            } else if (trimmed.rfind("INVARSPEC", 0) == 0) {
                kind = PropertyKind::Invariant;
                default_prefix = "invar_spec_";
                kw_len = 9;
            }
            std::string rest = trim(trimmed.substr(kw_len));
            std::string comment_name;
            auto c_pos = rest.find("--");
            if (c_pos != std::string::npos) {
                comment_name = trim(rest.substr(c_pos + 2));
                rest = trim(rest.substr(0, c_pos));
            }
            while (rest.empty() && std::getline(stream, line)) {
                std::string next_trim = trim(line);
                if (next_trim.empty() || next_trim.rfind("--", 0) == 0) {
                    continue;
                }
                auto nc_pos = next_trim.find("--");
                if (nc_pos != std::string::npos) {
                    next_trim = trim(next_trim.substr(0, nc_pos));
                }
                rest = next_trim;
            }
            if (!rest.empty() && rest.back() == ';') {
                rest.pop_back();
            }
            FormalProperty prop;
            prop.name = !comment_name.empty() ? sanitize_identifier(comment_name)
                                              : (default_prefix + std::to_string(model.properties.size() + 1));
            prop.kind = kind;
            prop.raw_formula = trim(rest);
            if (!prop.raw_formula.empty()) {
                model.add_property(std::move(prop));
            }
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
                    std::string norm_init = trim(init_val);
                    if (norm_init == "TRUE") {
                        norm_init = "true";
                    } else if (norm_init == "FALSE") {
                        norm_init = "false";
                    }
                    for (auto& v : model.variables) {
                        if (v.name == var_name) {
                            v.initial_value = norm_init;
                            if (norm_init.size() >= 2 && norm_init.front() == '"' && norm_init.back() == '"') {
                                v.type = DataType::string();
                            }
                            break;
                        }
                    }
                }
            }
            continue;
        }

        // 3. ASSIGN next(state) := case transitions
        if (current_section == Section::AssignNextState) {
            std::string line_code = trimmed;
            std::string line_comment;
            if (auto cpos = trimmed.find("--"); cpos != std::string::npos) {
                line_code = trim(trimmed.substr(0, cpos));
                line_comment = trimmed.substr(cpos);
            }
            if (!line_code.empty()) {
                if (!case_accumulator.empty()) {
                    case_accumulator += " ";
                }
                case_accumulator += line_code;
            }
            size_t semi_pos = case_accumulator.find(';');
            while (semi_pos != std::string::npos) {
                std::string stmt = trim(case_accumulator.substr(0, semi_pos + 1));
                case_accumulator = trim(case_accumulator.substr(semi_pos + 1));
                if (case_accumulator.empty() && !line_comment.empty()) {
                    stmt += " " + line_comment;
                    line_comment.clear();
                }
                if (stmt.find("TRUE :") == std::string::npos && stmt.find("TRUE:") == std::string::npos) {
                    parse_transition_case(stmt, model);
                }
                semi_pos = case_accumulator.find(';');
            }
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

std::string SmvParser::trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(first, last - first + 1));
}

void SmvParser::parse_enum_states(const std::string& line, FsmIr& model) {
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
        if (!st_name.empty() && !model.is_choice_node(st_name)) {
            model.add_or_get_state(st_name, "");
        }
    }
}

void SmvParser::parse_enum_events(const std::string& line, FsmIr& model) {
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
        }
    }
}

void SmvParser::parse_variable_decl(const std::string& line, FsmIr& model) {
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
    if (model.find_port(name) != nullptr) {
        return;  // Port already declared via @fsm:port directive
    }

    std::string type_part = trim(line.substr(colon + 1));
    if (!type_part.empty() && type_part.back() == ';') {
        type_part.pop_back();
    }

    for (auto& existing : model.variables) {
        if (existing.name == name) {
            if (type_part.find("..") != std::string::npos && !existing.min_value.has_value() &&
                !existing.max_value.has_value()) {
                size_t dotdot = type_part.find("..");
                try {
                    existing.min_value = std::stoll(trim(type_part.substr(0, dotdot)));
                    existing.max_value = std::stoll(trim(type_part.substr(dotdot + 2)));
                } catch (const std::exception&) {
                }
            }
            return;
        }
    }

    VariableDefinition var;
    var.name = name;
    if (type_part.find("boolean") != std::string::npos) {
        var.type = DataType::boolean();
    } else if (type_part.find("..") != std::string::npos) {
        var.type = DataType::uint32();
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
        var.type = DataType::uint32();
    }
    model.add_variable(std::move(var));
}

static std::string normalize_smv_guard_expr(std::string_view expr) {
    std::string out;
    out.reserve(expr.size() + 8);
    for (size_t i = 0; i < expr.size(); ++i) {
        char ch = expr[i];
        if (ch == '=') {
            char prev = (i > 0) ? expr[i - 1] : '\0';
            char next = (i + 1 < expr.size()) ? expr[i + 1] : '\0';
            if (prev != '!' && prev != '<' && prev != '>' && prev != '=' && prev != ':' && next != '=') {
                out += "==";
            } else {
                out.push_back(ch);
            }
        } else if (ch == '&') {
            char prev = (i > 0) ? expr[i - 1] : '\0';
            char next = (i + 1 < expr.size()) ? expr[i + 1] : '\0';
            if (prev != '&' && next != '&') {
                out += "&&";
            } else {
                out.push_back(ch);
            }
        } else if (ch == '|') {
            char prev = (i > 0) ? expr[i - 1] : '\0';
            char next = (i + 1 < expr.size()) ? expr[i + 1] : '\0';
            if (prev != '|' && next != '|') {
                out += "||";
            } else {
                out.push_back(ch);
            }
        } else if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            size_t start = i;
            while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) != 0 || expr[i] == '_')) {
                ++i;
            }
            std::string_view tok = expr.substr(start, i - start);
            if (tok == "TRUE") {
                out += "true";
            } else if (tok == "FALSE") {
                out += "false";
            } else {
                out.append(tok);
            }
            --i;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

void SmvParser::parse_transition_case(const std::string& line, FsmIr& model) {
    std::string clean_line = line;
    std::string raw_action_from_comment;
    std::string change_expr_from_comment;
    TransitionEdgeKind parsed_kind = TransitionEdgeKind::External;
    auto comment_pos = line.find("--");
    if (comment_pos != std::string::npos) {
        std::string comment = line.substr(comment_pos + 2);
        if (comment.find("kind=internal") != std::string::npos) {
            parsed_kind = TransitionEdgeKind::Internal;
        } else if (comment.find("kind=local") != std::string::npos) {
            parsed_kind = TransitionEdgeKind::Local;
        }
        auto act_pos = comment.find("action:");
        if (act_pos == std::string::npos) {
            act_pos = comment.find("action=");
        }
        if (act_pos != std::string::npos) {
            raw_action_from_comment = trim(comment.substr(act_pos + 7));
        }
        auto chg_pos = comment.find("change=");
        if (chg_pos != std::string::npos) {
            size_t chg_end = comment.find('|', chg_pos + 7);
            if (act_pos != std::string::npos && act_pos > chg_pos &&
                (chg_end == std::string::npos || act_pos < chg_end)) {
                chg_end = act_pos;
            }
            change_expr_from_comment = trim(comment.substr(
                chg_pos + 7, chg_end == std::string::npos ? std::string::npos : chg_end - (chg_pos + 7)));
        }
        clean_line = line.substr(0, comment_pos);
    }

    // Find the case target colon ':' that is not part of a '::' namespace qualifier
    size_t colon = std::string::npos;
    for (size_t i = clean_line.size(); i > 0; --i) {
        size_t idx = i - 1;
        if (clean_line[idx] == ':') {
            bool is_double_colon =
                (idx > 0 && clean_line[idx - 1] == ':') || (idx + 1 < clean_line.size() && clean_line[idx + 1] == ':');
            if (!is_double_colon) {
                colon = idx;
                break;
            }
        }
    }
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
    std::vector<std::string> source_states;
    std::vector<std::string> event_names;
    std::string guard_expr;

    std::vector<std::string> clauses;
    int p_depth = 0;
    size_t last_split = 0;
    for (size_t i = 0; i < cond_part.size(); ++i) {
        if (cond_part[i] == '(') {
            p_depth++;
        } else if (cond_part[i] == ')') {
            if (p_depth > 0)
                p_depth--;
        } else if (cond_part[i] == '&' && p_depth == 0) {
            clauses.push_back(cond_part.substr(last_split, i - last_split));
            last_split = i + 1;
        }
    }
    clauses.push_back(cond_part.substr(last_split));

    std::vector<std::string> other_clauses;
    std::optional<std::uint64_t> timer_duration_ms;
    std::string matched_timer_var;
    static const std::regex timer_clause_re(R"(^(timer_[A-Za-z0-9_]+)\s*>=\s*([0-9]+)$)");
    for (const auto& raw_clause : clauses) {
        std::string c = trim(raw_clause);
        while (c.size() >= 2 && c.front() == '(' && c.back() == ')') {
            c = trim(c.substr(1, c.size() - 2));
        }
        if (c.empty())
            continue;

        if (c.rfind("state in", 0) == 0) {
            size_t open_b = c.find('{');
            size_t close_b = c.find('}');
            if (open_b != std::string::npos && close_b != std::string::npos && close_b > open_b) {
                std::string list_str = c.substr(open_b + 1, close_b - open_b - 1);
                std::stringstream list_ss(list_str);
                std::string item;
                while (std::getline(list_ss, item, ',')) {
                    std::string s = sanitize_identifier(trim(item));
                    if (!s.empty()) {
                        source_states.push_back(s);
                    }
                }
            }
        } else if (c.rfind("state =", 0) == 0 || c.rfind("state=", 0) == 0) {
            size_t eq = c.find('=');
            std::string s = sanitize_identifier(trim(c.substr(eq + 1)));
            if (!s.empty()) {
                source_states.push_back(s);
            }
        } else if (c.rfind("event in", 0) == 0) {
            size_t open_b = c.find('{');
            size_t close_b = c.find('}');
            if (open_b != std::string::npos && close_b != std::string::npos && close_b > open_b) {
                std::string list_str = c.substr(open_b + 1, close_b - open_b - 1);
                std::stringstream list_ss(list_str);
                std::string item;
                while (std::getline(list_ss, item, ',')) {
                    std::string ev = sanitize_identifier(trim(item));
                    if (ev != "none" && !ev.empty()) {
                        event_names.push_back(ev);
                    }
                }
            }
        } else if (c.find('|') != std::string::npos && (c.rfind("event =", 0) == 0 || c.rfind("event=", 0) == 0)) {
            std::stringstream or_ss(c);
            std::string branch;
            std::vector<std::string> parsed_evs;
            bool all_events = true;
            while (std::getline(or_ss, branch, '|')) {
                std::string b = trim(branch);
                while (b.size() >= 2 && b.front() == '(' && b.back() == ')') {
                    b = trim(b.substr(1, b.size() - 2));
                }
                if (b.rfind("event =", 0) == 0 || b.rfind("event=", 0) == 0) {
                    size_t eq = b.find('=');
                    std::string ev = sanitize_identifier(trim(b.substr(eq + 1)));
                    if (ev != "none" && !ev.empty()) {
                        parsed_evs.push_back(ev);
                    }
                } else {
                    all_events = false;
                    break;
                }
            }
            if (all_events && !parsed_evs.empty()) {
                for (auto& ev : parsed_evs) {
                    event_names.push_back(std::move(ev));
                }
            } else {
                other_clauses.push_back(c);
            }
        } else if (c.rfind("event =", 0) == 0 || c.rfind("event=", 0) == 0) {
            size_t eq = c.find('=');
            std::string ev = sanitize_identifier(trim(c.substr(eq + 1)));
            if (ev != "none" && !ev.empty()) {
                event_names.push_back(ev);
            }
        } else {
            std::smatch tm;
            if (std::regex_match(c, tm, timer_clause_re)) {
                matched_timer_var = tm[1].str();
                timer_duration_ms = static_cast<std::uint64_t>(std::stoull(tm[2].str()));
            } else {
                other_clauses.push_back(c);
            }
        }
    }

    // If no explicit 'event = ...', check if any clause is a boolean trigger (e.g. cmd = TRUE or sig_start)
    for (const auto& c : other_clauses) {
        if (event_names.empty() && c.find('&') == std::string::npos && c.find('|') == std::string::npos &&
            (c.find("= TRUE") != std::string::npos || c.find("= true") != std::string::npos ||
             (c.find('=') == std::string::npos && c.find('<') == std::string::npos &&
              c.find('>') == std::string::npos))) {
            size_t eq = c.find('=');
            std::string cand = trim(eq != std::string::npos ? c.substr(0, eq) : c);
            std::string cand_id = sanitize_identifier(cand);
            if (!cand_id.empty() && cand_id != "TRUE" && cand_id != "true") {
                event_names.push_back(cand_id);
                continue;
            }
        }
        if (!guard_expr.empty())
            guard_expr += " && ";
        guard_expr += c;
    }

    if (source_states.empty())
        return;

    if (event_names.empty()) {
        event_names.emplace_back("");
    }

    if (!matched_timer_var.empty()) {
        model.variables.erase(std::remove_if(model.variables.begin(), model.variables.end(),
                                             [&](const VariableDefinition& v) { return v.name == matched_timer_var; }),
                              model.variables.end());
    }

    for (const auto& src : source_states) {
        for (const auto& event_name : event_names) {
            TransitionEdge trans;
            trans.source = src;
            trans.target = target_state;
            trans.event = event_name;
            if (!change_expr_from_comment.empty()) {
                trans.trigger = ChangeTrigger(change_expr_from_comment, true);
            } else if (timer_duration_ms.has_value()) {
                trans.trigger = TimeTrigger(TimeTriggerKind::After, *timer_duration_ms, TimeUnit::Milliseconds);
                if (trans.event.empty()) {
                    trans.event = trans.get_trigger_name();
                }
            }
            if (!guard_expr.empty() && (change_expr_from_comment.empty() ||
                                        normalize_smv_guard_expr(guard_expr) != change_expr_from_comment)) {
                // Generate a stable guard name from the transition endpoints and index,
                // instead of sanitizing the raw boolean expression which would mangle
                // operator characters (e.g. "x > 0 && !y" → "x__0____y").
                std::string guard_name = "guard_" + sanitize_identifier(src) + "_to_" +
                                         sanitize_identifier(target_state) + "_" +
                                         std::to_string(model.transitions.size() + 1);
                const std::string norm_guard = normalize_smv_guard_expr(guard_expr);
                model.add_guard(guard_name, "", std::optional<std::string>{norm_guard},
                                std::optional<std::string>{norm_guard});
                trans.guard = guard_name;
            }
            if (!raw_action_from_comment.empty()) {
                const std::string prefix =
                    "action_" + sanitize_identifier(src) + "_" + sanitize_identifier(target_state) +
                    (event_name.empty() ? "" : "_" + event_name) + "_" + std::to_string(model.transitions.size() + 1);
                if (auto assign_sig = try_parse_assignment_action(raw_action_from_comment, prefix)) {
                    model.add_action(assign_sig->name);
                    trans.transition_action = std::move(*assign_sig);
                } else {
                    std::string act_id = sanitize_identifier(raw_action_from_comment);
                    if (!act_id.empty()) {
                        trans.transition_action = ActionSignature(act_id);
                        model.add_action(act_id);
                    }
                }
            }
            trans.kind = parsed_kind;
            model.add_transition(trans);
        }
    }
}

void SmvParser::parse_state_metadata_directive(const std::string& body, FsmIr& model) {
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

    auto inv_pos = body.find("invariant=");
    if (inv_pos != std::string::npos) {
        st->time_invariant = extract_word_or_quoted(body, inv_pos + 10);
    }
}

void SmvParser::parse_action_directive(const std::string& body, FsmIr& model, const std::string& type) {
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

    ActionSignature act_sig(sanitize_identifier(act_name));
    if (auto assign_sig = try_parse_assignment_action(act_name, sanitize_identifier(st_name) + "_" + type)) {
        act_sig = std::move(*assign_sig);
    }
    if (type == "entry") {
        st->entry_actions.push_back(act_sig);
    } else if (type == "exit") {
        st->exit_actions.push_back(act_sig);
    }
    model.add_action(act_sig.name);
}

void SmvParser::parse_defer_directive(const std::string& body, FsmIr& model) {
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

void SmvParser::parse_req_directive(const std::string& body, FsmIr& model) {
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

void SmvParser::parse_trans_action_directive(const std::string& body, FsmIr& model) {
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
            trans.transition_action = ActionSignature(act);
            break;
        }
    }
    model.add_action(act);
}

std::string SmvParser::extract_word_or_quoted(const std::string& str, size_t pos) {
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

}  // namespace fsm::frontend::formal
