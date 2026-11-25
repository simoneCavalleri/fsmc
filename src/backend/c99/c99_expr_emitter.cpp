#include <algorithm>
#include <cctype>
#include <sstream>

#include "c99_emitter_internal.hpp"
#include "fsm/ir/guard.hpp"

namespace fsm::backend::c99::c99_detail {

using namespace fsm::ir;

std::string to_snake_case(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (std::isalnum(ch) != 0) {
            if (std::isupper(ch) != 0) {
                if (i > 0 && !out.empty() && out.back() != '_') {
                    const unsigned char prev = static_cast<unsigned char>(raw[i - 1]);
                    const bool prev_lower_or_digit = (std::islower(prev) != 0 || std::isdigit(prev) != 0);
                    const bool next_lower =
                        (i + 1 < raw.size() && std::islower(static_cast<unsigned char>(raw[i + 1])) != 0);
                    if (prev_lower_or_digit || next_lower) {
                        out.push_back('_');
                    }
                }
                out.push_back(static_cast<char>(std::tolower(ch)));
            } else {
                out.push_back(static_cast<char>(ch));
            }
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "fsm";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string to_upper_ascii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string sanitize_c_ident(std::string_view raw) {
    std::string res;
    res.reserve(raw.size() + 1);
    for (char c : raw) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0 || c == '_') {
            res.push_back(c);
        } else {
            res.push_back('_');
        }
    }
    if (res.empty()) {
        res = "fsm";
    }
    if (std::isdigit(static_cast<unsigned char>(res.front())) != 0) {
        res.insert(res.begin(), '_');
    }
    return res;
}

std::string lower_external_module_to_c_header(std::string_view module) {
    std::string m(module);
    if (m.size() >= 4 && m.substr(m.size() - 4) == ".hpp") {
        m.replace(m.size() - 4, 4, ".h");
    } else if (m.size() >= 2 && m.substr(m.size() - 2) == ".h") {
        // already a C header
    } else {
        m += ".h";
    }
    return m;
}

std::string lower_external_symbol_to_c_type(std::string_view symbol) {
    const std::size_t dot = symbol.rfind('.');
    if (dot != std::string_view::npos && dot + 1 < symbol.size()) {
        return sanitize_c_ident(symbol.substr(dot + 1));
    }
    const std::size_t colons = symbol.rfind("::");
    if (colons != std::string_view::npos && colons + 2 < symbol.size()) {
        return sanitize_c_ident(symbol.substr(colons + 2));
    }
    return sanitize_c_ident(symbol);
}

bool map_type_to_c99(const DataType& dt, const C99Context& ctx, std::string& out_c_type, bool& out_is_struct) {
    out_is_struct = false;
    if (dt.is_boolean()) {
        out_c_type = "bool";
        return true;
    }
    if (dt.is_integer()) {
        out_c_type = dt.to_cpp_type();
        return true;
    }
    if (dt.is_floating_point()) {
        out_c_type = dt.to_cpp_type();
        return true;
    }
    if (!dt.is_primitive()) {
        const std::string cname = dt.custom_name();
        if (auto it = ctx.custom_c_types.find(cname); it != ctx.custom_c_types.end()) {
            out_c_type = it->second;
            out_is_struct = (ctx.struct_ir_names.count(cname) != 0U);
            return true;
        }
    }
    return false;
}

bool is_ident_start(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) != 0 || c == '_';
}

bool is_ident_char(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

std::string translate_expr_to_c(std::string_view expr, const C99Context& ctx,
                                const std::unordered_set<std::string>& event_attrs) {
    std::string out;
    out.reserve(expr.size() + 32);
    std::size_t i = 0;
    const std::size_t n = expr.size();

    while (i < n) {
        // Preserve string/char literals untouched
        if (expr[i] == '"' || expr[i] == '\'') {
            const char quote = expr[i];
            out.push_back(expr[i++]);
            while (i < n) {
                if (expr[i] == '\\' && i + 1 < n) {
                    out.push_back(expr[i++]);
                    out.push_back(expr[i++]);
                    continue;
                }
                const char ch = expr[i++];
                out.push_back(ch);
                if (ch == quote) {
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
            const std::string tok(expr.substr(start, i - start));

            // Check if followed by '.' (scoped access: in.x, out.y, reg.z, ev.a, or struct.field)
            if (i < n && expr[i] == '.') {
                const std::size_t dot_pos = i;
                std::size_t j = dot_pos + 1;
                while (j < n && is_ident_char(expr[j])) {
                    ++j;
                }
                const std::string member(expr.substr(dot_pos + 1, j - (dot_pos + 1)));

                if (tok == "in") {
                    out += "in_val." + sanitize_c_ident(member);
                    i = j;
                    continue;
                }
                if (tok == "out") {
                    out += "out_val." + sanitize_c_ident(member);
                    i = j;
                    continue;
                }
                if (tok == "reg" || tok == "regs") {
                    out += "fsm->regs." + sanitize_c_ident(member);
                    i = j;
                    continue;
                }
                if (tok == "ev" || tok == "event") {
                    out += "ev_" + sanitize_c_ident(member);
                    i = j;
                    continue;
                }
                // Struct port or register member access (e.g., sensor.temp_c)
                if (ctx.in_ports.count(tok) != 0U) {
                    out += "in_val." + sanitize_c_ident(tok);
                    continue;
                }
                if (ctx.out_ports.count(tok) != 0U) {
                    out += "out_val." + sanitize_c_ident(tok);
                    continue;
                }
                if (ctx.registers.count(tok) != 0U) {
                    out += "fsm->regs." + sanitize_c_ident(tok);
                    continue;
                }
                // SysML v2 event payload binding (e.g. `accept alert : EvOverTemp` -> `alert.peak_temp`)
                if (event_attrs.count(member) != 0U) {
                    out += "ev_" + sanitize_c_ident(member);
                    i = j;
                    continue;
                }
                out += tok;
                continue;
            }

            // Boolean keyword normalization
            if (tok == "and") {
                out += "&&";
                continue;
            }
            if (tok == "or") {
                out += "||";
                continue;
            }
            if (tok == "not") {
                out += "!";
                continue;
            }

            // Bare identifier resolution (precedence: event attr -> register -> in_port -> out_port)
            // Skip if immediately preceded by '.' (member name of a preceding struct)
            const bool preceded_by_dot = (!out.empty() && out.back() == '.');
            if (!preceded_by_dot) {
                if (event_attrs.count(tok) != 0U) {
                    out += "ev_" + sanitize_c_ident(tok);
                    continue;
                }
                if (ctx.registers.count(tok) != 0U) {
                    out += "fsm->regs." + sanitize_c_ident(tok);
                    continue;
                }
                if (ctx.in_ports.count(tok) != 0U) {
                    out += "in_val." + sanitize_c_ident(tok);
                    continue;
                }
                if (ctx.out_ports.count(tok) != 0U) {
                    out += "out_val." + sanitize_c_ident(tok);
                    continue;
                }
            }
            out += tok;
            continue;
        }

        out.push_back(expr[i++]);
    }
    return out;
}

std::string format_lvalue_c(const LValueTarget& target, const C99Context& ctx) {
    std::string lhs;
    if (target.scope == LValueScope::OutPort ||
        (ctx.out_ports.count(target.name) != 0U && ctx.registers.count(target.name) == 0U)) {
        lhs = "out_val." + sanitize_c_ident(target.name);
    } else {
        lhs = "fsm->regs." + sanitize_c_ident(target.name);
    }
    for (const auto& m : target.member_path) {
        lhs += "." + sanitize_c_ident(m);
    }
    return lhs;
}

void emit_action_c(std::ostringstream& src, const ActionSignature& act, const C99Context& ctx,
                   const std::unordered_set<std::string>& event_attrs, const std::string& indent) {
    for (const auto& assign : act.assignments) {
        const std::string lhs = format_lvalue_c(assign.target, ctx);
        const std::string rhs = translate_expr_to_c(assign.expression, ctx, event_attrs);
        src << indent << lhs << " " << assignment_op_to_string(assign.op) << " " << rhs << ";\n";
    }

    for (const auto& inst : act.instructions) {
        if (std::holds_alternative<StoreOp>(inst.op)) {
            const auto& st = std::get<StoreOp>(inst.op);
            const std::string lhs = format_lvalue_c(st.target, ctx);
            const std::string rhs = translate_expr_to_c(st.expression, ctx, event_attrs);
            src << indent << lhs << " " << assignment_op_to_string(st.op) << " " << rhs << ";\n";
        } else if (std::holds_alternative<PortWriteOp>(inst.op)) {
            const auto& pw = std::get<PortWriteOp>(inst.op);
            const std::string rhs = translate_expr_to_c(pw.expression, ctx, event_attrs);
            src << indent << "out_val." << sanitize_c_ident(pw.port_name) << " = " << rhs << ";\n";
        } else if (std::holds_alternative<PortReadOp>(inst.op)) {
            const auto& pr = std::get<PortReadOp>(inst.op);
            const std::string lhs = format_lvalue_c(pr.destination, ctx);
            src << indent << lhs << " = in_val." << sanitize_c_ident(pr.port_name) << ";\n";
        } else if (std::holds_alternative<ActionCallOp>(inst.op)) {
            const auto& call = std::get<ActionCallOp>(inst.op);
            auto svc_it = ctx.services.find(call.function_name);
            if (svc_it != ctx.services.end()) {
                const std::string fn = sanitize_c_ident(call.function_name);
                src << indent << "if (fsm->services." << fn << " != NULL) {\n";
                src << indent << "    ";
                if (call.result_target.has_value() && !svc_it->second->is_void_return()) {
                    src << format_lvalue_c(*call.result_target, ctx) << " = ";
                }
                src << "fsm->services." << fn << "(fsm->services.user_data";
                for (const auto& arg : call.arguments) {
                    src << ", " << translate_expr_to_c(arg, ctx, event_attrs);
                }
                src << ");\n";
                src << indent << "}\n";
            }
        } else if (std::holds_alternative<SignalEmitOp>(inst.op)) {
            const auto& se = std::get<SignalEmitOp>(inst.op);
            if (!se.signal_name.empty() && !ctx.emitted_signals.empty()) {
                const std::string tag_const =
                    ctx.upper_prefix + "_OUT_" + to_upper_ascii(to_snake_case(se.signal_name));
                const std::string port_lit = se.target_port.value_or("");
                src << indent << "if (out_val.outbox_count < " << ctx.upper_prefix << "_MAX_OUTBOX) {\n";
                src << indent << "    " << ctx.prefix << "_emitted_event_t _em;\n";
                src << indent << "    memset(&_em, 0, sizeof(_em));\n";
                src << indent << "    _em.tag = " << tag_const << ";\n";
                src << indent << "    _em.port = \"" << port_lit << "\";\n";
                auto sig_it = ctx.emitted_signals_map.find(se.signal_name);
                const SignalDefinition* sig_def = (sig_it != ctx.emitted_signals_map.end()) ? sig_it->second : nullptr;
                if (sig_def != nullptr && !sig_def->attributes.empty()) {
                    const std::string field_pfx = "_em." + to_snake_case(se.signal_name) + ".";
                    for (std::size_t ai = 0; ai < se.arguments.size() && ai < sig_def->attributes.size(); ++ai) {
                        const auto& attr = sig_def->attributes[ai];
                        std::string ct;
                        bool is_st = false;
                        if (map_type_to_c99(attr.type, ctx, ct, is_st)) {
                            src << indent << "    " << field_pfx << sanitize_c_ident(attr.name) << " = (" << ct << ")("
                                << translate_expr_to_c(se.arguments[ai], ctx, event_attrs) << ");\n";
                        }
                    }
                }
                src << indent << "    out_val.outbox[out_val.outbox_count++] = _em;\n";
                src << indent << "}\n";
            }
        }
    }
}

std::string resolve_guard_expr_c(const TransitionEdge& tr, const FsmIr& model, const C99Context& ctx,
                                 const std::unordered_set<std::string>& event_attrs) {
    if (!tr.guard.has_value() || tr.guard->empty() || *tr.guard == "else") {
        return "";
    }
    std::string raw_expr = *tr.guard;
    if (const auto* g = model.find_guard(*tr.guard)) {
        if (g->raw_expression.has_value() && !g->raw_expression->empty()) {
            raw_expr = *g->raw_expression;
        }
    }
    return translate_expr_to_c(raw_expr, ctx, event_attrs);
}

void emit_transition_body_c(std::ostringstream& src, const TransitionEdge& tr, const FsmIr& model,
                            const C99Context& ctx, const std::unordered_set<std::string>& event_attrs,
                            const std::string& indent, bool is_periodic_timer, uint64_t timer_dur_ms) {
    if (tr.condition_action.has_value()) {
        emit_action_c(src, *tr.condition_action, ctx, event_attrs, indent);
    }

    if (tr.kind == TransitionEdgeKind::Internal) {
        if (tr.transition_action.has_value()) {
            emit_action_c(src, *tr.transition_action, ctx, event_attrs, indent);
        }
        if (is_periodic_timer && timer_dur_ms > 0U) {
            src << indent << "fsm->state_elapsed_ms -= " << timer_dur_ms << "ULL;\n";
        }
        return;
    }

    // External transition: exit source -> transition action -> update state -> reset timer -> enter target
    if (const auto* src_st = model.find_state(tr.source)) {
        for (const auto& exit_act : src_st->exit_actions) {
            emit_action_c(src, exit_act, ctx, event_attrs, indent);
        }
    }
    if (tr.transition_action.has_value()) {
        emit_action_c(src, *tr.transition_action, ctx, event_attrs, indent);
    }
    const auto tgt_it = ctx.state_enums.find(tr.target);
    if (tgt_it != ctx.state_enums.end()) {
        src << indent << "fsm->state = " << tgt_it->second << ";\n";
    }
    if (is_periodic_timer && tr.source == tr.target && timer_dur_ms > 0U) {
        src << indent << "fsm->state_elapsed_ms -= " << timer_dur_ms << "ULL;\n";
    } else {
        src << indent << "fsm->state_elapsed_ms = 0ULL;\n";
    }
    if (const auto* dst_st = model.find_state(tr.target)) {
        for (const auto& entry_act : dst_st->entry_actions) {
            emit_action_c(src, entry_act, ctx, event_attrs, indent);
        }
    }
}

}  // namespace fsm::backend::c99::c99_detail
