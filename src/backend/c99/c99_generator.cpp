#include "fsm/backend/c99/c99_generator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fsm/ir/fsm_ir_serializer.hpp"

namespace fsm::backend::c99 {

using namespace fsm::ir;

namespace {

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

struct C99Field {
    std::string name;
    std::string c_type;
    bool is_struct = false;
};

struct C99LocalStruct {
    std::string ir_name;
    std::string c_type_name;
    std::vector<C99Field> fields;
};

struct C99Context {
    std::string prefix;
    std::string upper_prefix;
    std::unordered_map<std::string, std::string> custom_c_types;  ///< IR type name -> C99 typedef name
    std::unordered_set<std::string> struct_ir_names;
    std::unordered_set<std::string> in_ports;
    std::unordered_set<std::string> out_ports;
    std::unordered_set<std::string> registers;
    std::unordered_map<std::string, const ServiceMethodSignature*> services;
    std::unordered_map<std::string, std::string> state_enums;
};

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
                            const std::string& indent, bool is_periodic_timer = false, uint64_t timer_dur_ms = 0U) {
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

}  // namespace

GeneratedC99Bundle C99Generator::generate(const FsmIr& model, const C99GeneratorOptions& options) {
    FsmIr processed = model;
    processed.canonicalize();

    if (processed.states.empty()) {
        throw std::invalid_argument("C99Generator: FsmIr model must contain at least one state.");
    }

    C99Context ctx;
    const std::string base_name = sanitize_c_ident(processed.name);
    ctx.prefix = to_snake_case(base_name);
    ctx.upper_prefix = to_upper_ascii(ctx.prefix);

    const std::string header_inc = options.header_include.empty() ? (ctx.prefix + ".h") : options.header_include;
    const std::string ir_sha256 = FsmIrSerializer::compute_sha256(processed);

    // Register states -> C99 enum constants
    for (const auto& st : processed.states) {
        ctx.state_enums[st.name] = ctx.upper_prefix + "_STATE_" + to_upper_ascii(to_snake_case(st.name));
    }

    // Register services
    for (const auto& svc : processed.services) {
        ctx.services[svc.name] = &svc;
    }

    // Register external types & local enums first
    std::vector<const TypeDefinition*> external_types;
    std::vector<const TypeDefinition*> local_enums;
    for (const auto& td : processed.custom_types) {
        if (td.is_external()) {
            const std::string c_alias = ctx.prefix + "_" + to_snake_case(td.name) + "_t";
            ctx.custom_c_types[td.name] = c_alias;
            if (td.kind == TypeKind::Struct) {
                ctx.struct_ir_names.insert(td.name);
            }
            external_types.push_back(&td);
        } else if (td.kind == TypeKind::Enum) {
            const std::string c_alias = ctx.prefix + "_" + to_snake_case(td.name) + "_t";
            ctx.custom_c_types[td.name] = c_alias;
            local_enums.push_back(&td);
        }
    }

    // Register local POD structs in topological dependency order
    std::vector<C99LocalStruct> local_structs;
    {
        bool progress = true;
        std::unordered_set<std::string> added;
        while (progress) {
            progress = false;
            for (const auto& td : processed.custom_types) {
                if (td.kind != TypeKind::Struct || td.is_external() || added.count(td.name) != 0U) {
                    continue;
                }
                C99LocalStruct ls;
                ls.ir_name = td.name;
                ls.c_type_name = ctx.prefix + "_" + to_snake_case(td.name) + "_t";
                bool all_ok = true;
                for (const auto& f : td.fields) {
                    C99Field cf;
                    cf.name = sanitize_c_ident(f.name);
                    if (!map_type_to_c99(f.type, ctx, cf.c_type, cf.is_struct)) {
                        all_ok = false;
                        break;
                    }
                    ls.fields.push_back(std::move(cf));
                }
                if (all_ok) {
                    added.insert(td.name);
                    ctx.custom_c_types[td.name] = ls.c_type_name;
                    ctx.struct_ir_names.insert(td.name);
                    local_structs.push_back(std::move(ls));
                    progress = true;
                }
            }
        }
    }

    // Collect InPorts, OutPorts, and Registers
    std::vector<C99Field> in_fields;
    std::vector<C99Field> out_fields;
    for (const auto& port : processed.ports) {
        C99Field cf;
        cf.name = sanitize_c_ident(port.name);
        if (map_type_to_c99(port.type, ctx, cf.c_type, cf.is_struct)) {
            if (port.is_in()) {
                in_fields.push_back(cf);
                ctx.in_ports.insert(port.name);
            }
            if (port.is_out()) {
                out_fields.push_back(cf);
                ctx.out_ports.insert(port.name);
            }
        }
    }

    struct C99RegField {
        std::string name;
        std::string c_type;
        std::string init_expr;
        bool is_struct = false;
    };
    std::vector<C99RegField> reg_fields;
    for (const auto& var : processed.variables) {
        C99RegField rf;
        rf.name = sanitize_c_ident(var.name);
        if (map_type_to_c99(var.type, ctx, rf.c_type, rf.is_struct)) {
            rf.init_expr = var.initial_value;
            reg_fields.push_back(std::move(rf));
            ctx.registers.insert(var.name);
        }
    }

    // Collect public signals/events
    std::vector<SignalDefinition> events;
    for (const auto& sig : processed.signals) {
        if (sig.name == "anonymous_event" || sig.name == "completion_event") {
            continue;
        }
        events.push_back(sig);
    }

    const bool has_typed_services = !processed.services.empty();

    // ========================================================================
    // 1. Pure C99 Header (*.h)
    // ========================================================================
    std::ostringstream hdr;
    hdr << "/**\n";
    hdr << " * @file " << header_inc << "\n";
    hdr << " * @brief Native Pure-C99 bare-metal state machine for '" << processed.name << "'\n";
    hdr << " * @note Generated by fsmc (--target c99). Zero C++ runtime, zero heap allocation.\n";
    hdr << " * @note IR-SHA256: " << ir_sha256 << "\n";
    hdr << " */\n\n";
    hdr << "#ifndef " << ctx.upper_prefix << "_H\n";
    hdr << "#define " << ctx.upper_prefix << "_H\n\n";
    hdr << "#define " << ctx.upper_prefix << "_IR_SHA256 \"" << ir_sha256 << "\"\n\n";
    hdr << "#include <stdbool.h>\n";
    hdr << "#include <stddef.h>\n";
    hdr << "#include <stdint.h>\n";

    // External type includes
    std::unordered_set<std::string> emitted_includes;
    for (const auto* td : external_types) {
        const std::string c_hdr = lower_external_module_to_c_header(td->external_ref->module);
        if (emitted_includes.insert(c_hdr).second) {
            hdr << "#include \"" << c_hdr << "\"\n";
        }
    }
    hdr << "\n";

    hdr << "#ifdef __cplusplus\n";
    hdr << "extern \"C\" {\n";
    hdr << "#endif\n\n";

    // External type aliases
    for (const auto* td : external_types) {
        const std::string c_sym = lower_external_symbol_to_c_type(td->external_ref->symbol);
        const std::string c_alias = ctx.custom_c_types.at(td->name);
        hdr << "typedef " << c_sym << " " << c_alias << ";\n";
    }
    if (!external_types.empty()) {
        hdr << "\n";
    }

    // Local enums
    for (const auto* en : local_enums) {
        const std::string en_upper = ctx.upper_prefix + "_" + to_upper_ascii(to_snake_case(en->name));
        hdr << "typedef enum {\n";
        for (std::size_t i = 0; i < en->literals.size(); ++i) {
            const auto& lit = en->literals[i];
            hdr << "    " << en_upper << "_" << to_upper_ascii(to_snake_case(lit.name));
            if (lit.value.has_value()) {
                hdr << " = " << *lit.value;
            }
            if (i + 1 < en->literals.size()) {
                hdr << ",";
            }
            hdr << "\n";
        }
        hdr << "} " << ctx.custom_c_types.at(en->name) << ";\n\n";
    }

    // Local POD structs
    for (const auto& ls : local_structs) {
        hdr << "typedef struct {\n";
        if (ls.fields.empty()) {
            hdr << "    uint8_t _reserved;\n";
        } else {
            for (const auto& f : ls.fields) {
                hdr << "    " << f.c_type << " " << f.name << ";\n";
            }
        }
        hdr << "} " << ls.c_type_name << ";\n\n";
    }

    // State enum
    hdr << "typedef enum {\n";
    for (std::size_t i = 0; i < processed.states.size(); ++i) {
        hdr << "    " << ctx.state_enums.at(processed.states[i].name) << " = " << i << ",\n";
    }
    hdr << "    " << ctx.upper_prefix << "_STATE_COUNT = " << processed.states.size() << "\n";
    hdr << "} " << ctx.prefix << "_state_t;\n\n";

    // Status enum
    hdr << "typedef enum {\n";
    hdr << "    " << ctx.upper_prefix << "_STATUS_OK = 0,\n";
    hdr << "    " << ctx.upper_prefix << "_STATUS_GUARD_REJECTED = 1,\n";
    hdr << "    " << ctx.upper_prefix << "_STATUS_IGNORED = 2,\n";
    hdr << "    " << ctx.upper_prefix << "_STATUS_INVALID_ARG = 3\n";
    hdr << "} " << ctx.prefix << "_status_t;\n\n";

    // InPorts POD
    hdr << "typedef struct {\n";
    if (in_fields.empty()) {
        hdr << "    uint8_t _reserved;\n";
    } else {
        for (const auto& f : in_fields) {
            hdr << "    " << f.c_type << " " << f.name << ";\n";
        }
    }
    hdr << "} " << ctx.prefix << "_in_ports_t;\n\n";

    // OutPorts POD
    hdr << "typedef struct {\n";
    if (out_fields.empty()) {
        hdr << "    uint8_t _reserved;\n";
    } else {
        for (const auto& f : out_fields) {
            hdr << "    " << f.c_type << " " << f.name << ";\n";
        }
    }
    hdr << "} " << ctx.prefix << "_out_ports_t;\n\n";

    // Registers POD
    hdr << "typedef struct {\n";
    if (reg_fields.empty()) {
        hdr << "    uint8_t _reserved;\n";
    } else {
        for (const auto& f : reg_fields) {
            hdr << "    " << f.c_type << " " << f.name << ";\n";
        }
    }
    hdr << "} " << ctx.prefix << "_registers_t;\n\n";

    // Services callback table
    if (has_typed_services) {
        hdr << "typedef struct {\n";
        hdr << "    void* user_data;\n";
        for (const auto& svc : processed.services) {
            std::string ret_c = "void";
            bool is_st = false;
            if (!svc.is_void_return()) {
                if (!map_type_to_c99(svc.return_type, ctx, ret_c, is_st)) {
                    ret_c = "int32_t";
                }
            }
            hdr << "    " << ret_c << " (*" << sanitize_c_ident(svc.name) << ")(void* user_data";
            for (const auto& param : svc.parameters) {
                std::string p_c = "int32_t";
                bool p_st = false;
                map_type_to_c99(param.type, ctx, p_c, p_st);
                hdr << ", " << p_c << " " << sanitize_c_ident(param.name);
            }
            hdr << ");\n";
        }
        hdr << "} " << ctx.prefix << "_services_t;\n\n";
    }

    // Transparent zero-heap FSM instance struct
    hdr << "typedef struct {\n";
    hdr << "    " << ctx.prefix << "_state_t state;\n";
    hdr << "    " << ctx.prefix << "_registers_t regs;\n";
    hdr << "    uint64_t state_elapsed_ms;\n";
    if (has_typed_services) {
        hdr << "    " << ctx.prefix << "_services_t services;\n";
    }
    hdr << "} " << ctx.prefix << "_fsm_t;\n\n";

    // Public C99 API
    hdr << "void " << ctx.prefix << "_init(" << ctx.prefix << "_fsm_t* fsm);\n";
    if (has_typed_services) {
        hdr << "void " << ctx.prefix << "_init_with_services(" << ctx.prefix << "_fsm_t* fsm, const " << ctx.prefix
            << "_services_t* services);\n";
    }
    hdr << "void " << ctx.prefix << "_destroy(" << ctx.prefix << "_fsm_t* fsm);\n";
    hdr << ctx.prefix << "_state_t " << ctx.prefix << "_current_state(const " << ctx.prefix << "_fsm_t* fsm);\n";
    hdr << "const char* " << ctx.prefix << "_state_name(" << ctx.prefix << "_state_t state);\n";
    hdr << "const char* " << ctx.prefix << "_ir_sha256(void);\n";
    hdr << ctx.prefix << "_status_t " << ctx.prefix << "_step(" << ctx.prefix << "_fsm_t* fsm, const " << ctx.prefix
        << "_in_ports_t* in_ports, " << ctx.prefix << "_out_ports_t* out_ports);\n";
    hdr << ctx.prefix << "_status_t " << ctx.prefix << "_tick(" << ctx.prefix
        << "_fsm_t* fsm, uint64_t delta_ms, const " << ctx.prefix << "_in_ports_t* in_ports, " << ctx.prefix
        << "_out_ports_t* out_ports);\n";
    hdr << "size_t " << ctx.prefix << "_snapshot_size(const " << ctx.prefix << "_fsm_t* fsm);\n";
    hdr << "bool " << ctx.prefix << "_save_snapshot(const " << ctx.prefix
        << "_fsm_t* fsm, uint8_t* out_buf, size_t capacity, size_t* bytes_written);\n";
    hdr << "bool " << ctx.prefix << "_restore_snapshot(" << ctx.prefix
        << "_fsm_t* fsm, const uint8_t* buf, size_t size, size_t* bytes_read);\n\n";

    // Event dispatchers
    for (const auto& sig : events) {
        const std::string ev_snake = to_snake_case(sig.name);
        hdr << ctx.prefix << "_status_t " << ctx.prefix << "_dispatch_" << ev_snake << "(" << ctx.prefix
            << "_fsm_t* fsm";
        for (const auto& attr : sig.attributes) {
            std::string c_type;
            bool is_st = false;
            if (map_type_to_c99(attr.type, ctx, c_type, is_st)) {
                if (is_st) {
                    hdr << ", const " << c_type << "* " << sanitize_c_ident(attr.name);
                } else {
                    hdr << ", " << c_type << " " << sanitize_c_ident(attr.name);
                }
            }
        }
        hdr << ", const " << ctx.prefix << "_in_ports_t* in_ports, " << ctx.prefix << "_out_ports_t* out_ports);\n";
    }

    hdr << "\n#ifdef __cplusplus\n";
    hdr << "} /* extern \"C\" */\n";
    hdr << "#endif\n\n";
    hdr << "#endif /* " << ctx.upper_prefix << "_H */\n";

    // ========================================================================
    // 2. Pure C99 Implementation (*.c)
    // ========================================================================
    std::ostringstream src;
    src << "/**\n";
    src << " * @file " << ctx.prefix << ".c\n";
    src << " * @brief Native Pure-C99 implementation of '" << processed.name << "'\n";
    src << " * @note Generated by fsmc (--target c99). Zero dynamic allocations, strict C99.\n";
    src << " */\n\n";
    src << "#include \"" << header_inc << "\"\n\n";
    src << "#include <string.h>\n\n";

    // Snapshot FNV-1a checksum helper
    src << "static uint32_t " << ctx.prefix << "_fnv1a_32(const uint8_t* data, size_t len) {\n";
    src << "    uint32_t hash = 2166136261U;\n";
    src << "    size_t i;\n";
    src << "    for (i = 0U; i < len; ++i) {\n";
    src << "        hash ^= (uint32_t)data[i];\n";
    src << "        hash *= 16777619U;\n";
    src << "    }\n";
    src << "    return hash;\n";
    src << "}\n\n";

    // init_with_services / init / destroy
    const std::string init_state_enum = ctx.state_enums.count(processed.initial_state) != 0U
                                            ? ctx.state_enums.at(processed.initial_state)
                                            : ctx.state_enums.at(processed.states.front().name);

    auto emit_init_body = [&](bool with_services) {
        src << "    " << ctx.prefix << "_in_ports_t in_val;\n";
        src << "    " << ctx.prefix << "_out_ports_t out_val;\n";
        src << "    if (fsm == NULL) {\n";
        src << "        return;\n";
        src << "    }\n";
        src << "    memset(fsm, 0, sizeof(*fsm));\n";
        src << "    memset(&in_val, 0, sizeof(in_val));\n";
        src << "    memset(&out_val, 0, sizeof(out_val));\n";
        src << "    (void)in_val;\n";
        src << "    (void)out_val;\n";
        if (with_services) {
            src << "    if (services != NULL) {\n";
            src << "        fsm->services = *services;\n";
            src << "    }\n";
        }
        src << "    fsm->state = " << init_state_enum << ";\n";
        src << "    fsm->state_elapsed_ms = 0ULL;\n";
        for (const auto& rf : reg_fields) {
            if (!rf.is_struct && !rf.init_expr.empty()) {
                src << "    fsm->regs." << rf.name << " = " << rf.init_expr << ";\n";
            }
        }
        if (const auto* init_st = model.find_state(processed.initial_state)) {
            const std::unordered_set<std::string> no_attrs;
            for (const auto& entry_act : init_st->entry_actions) {
                emit_action_c(src, entry_act, ctx, no_attrs, "    ");
            }
        }
    };

    if (has_typed_services) {
        src << "void " << ctx.prefix << "_init_with_services(" << ctx.prefix << "_fsm_t* fsm, const " << ctx.prefix
            << "_services_t* services) {\n";
        emit_init_body(true);
        src << "}\n\n";
        src << "void " << ctx.prefix << "_init(" << ctx.prefix << "_fsm_t* fsm) {\n";
        src << "    " << ctx.prefix << "_init_with_services(fsm, NULL);\n";
        src << "}\n\n";
    } else {
        src << "void " << ctx.prefix << "_init(" << ctx.prefix << "_fsm_t* fsm) {\n";
        emit_init_body(false);
        src << "}\n\n";
    }

    src << "void " << ctx.prefix << "_destroy(" << ctx.prefix << "_fsm_t* fsm) {\n";
    src << "    (void)fsm;\n";
    src << "}\n\n";

    src << ctx.prefix << "_state_t " << ctx.prefix << "_current_state(const " << ctx.prefix << "_fsm_t* fsm) {\n";
    src << "    if (fsm == NULL) {\n";
    src << "        return " << ctx.upper_prefix << "_STATE_COUNT;\n";
    src << "    }\n";
    src << "    return fsm->state;\n";
    src << "}\n\n";

    src << "const char* " << ctx.prefix << "_state_name(" << ctx.prefix << "_state_t state) {\n";
    src << "    switch (state) {\n";
    for (const auto& st : processed.states) {
        src << "        case " << ctx.state_enums.at(st.name) << ":\n";
        src << "            return \"" << st.name << "\";\n";
    }
    src << "        default:\n";
    src << "            return \"unknown\";\n";
    src << "    }\n";
    src << "}\n\n";

    src << "const char* " << ctx.prefix << "_ir_sha256(void) {\n";
    src << "    return " << ctx.upper_prefix << "_IR_SHA256;\n";
    src << "}\n\n";

    // Snapshot size / save / restore
    // Layout: [4 magic] [4 version=1] [4 state] [8 elapsed_ms] [sizeof(regs)] [4 fnv1a_32]
    src << "size_t " << ctx.prefix << "_snapshot_size(const " << ctx.prefix << "_fsm_t* fsm) {\n";
    src << "    if (fsm == NULL) {\n";
    src << "        return 0U;\n";
    src << "    }\n";
    src << "    return 24U + sizeof(fsm->regs);\n";
    src << "}\n\n";

    src << "bool " << ctx.prefix << "_save_snapshot(const " << ctx.prefix
        << "_fsm_t* fsm, uint8_t* out_buf, size_t capacity, size_t* bytes_written) {\n";
    src << "    const size_t needed = " << ctx.prefix << "_snapshot_size(fsm);\n";
    src << "    const uint32_t magic = 0x434D5346U; /* 'FSMC' */\n";
    src << "    const uint32_t version = 1U;\n";
    src << "    uint32_t state_u32;\n";
    src << "    uint32_t crc;\n";
    src << "    size_t offset = 0U;\n";
    src << "    if (fsm == NULL || out_buf == NULL || capacity < needed) {\n";
    src << "        if (bytes_written != NULL) {\n";
    src << "            *bytes_written = 0U;\n";
    src << "        }\n";
    src << "        return false;\n";
    src << "    }\n";
    src << "    state_u32 = (uint32_t)fsm->state;\n";
    src << "    memcpy(out_buf + offset, &magic, 4U); offset += 4U;\n";
    src << "    memcpy(out_buf + offset, &version, 4U); offset += 4U;\n";
    src << "    memcpy(out_buf + offset, &state_u32, 4U); offset += 4U;\n";
    src << "    memcpy(out_buf + offset, &fsm->state_elapsed_ms, 8U); offset += 8U;\n";
    src << "    memcpy(out_buf + offset, &fsm->regs, sizeof(fsm->regs)); offset += sizeof(fsm->regs);\n";
    src << "    crc = " << ctx.prefix << "_fnv1a_32(out_buf, offset);\n";
    src << "    memcpy(out_buf + offset, &crc, 4U); offset += 4U;\n";
    src << "    if (bytes_written != NULL) {\n";
    src << "        *bytes_written = offset;\n";
    src << "    }\n";
    src << "    return true;\n";
    src << "}\n\n";

    src << "bool " << ctx.prefix << "_restore_snapshot(" << ctx.prefix
        << "_fsm_t* fsm, const uint8_t* buf, size_t size, size_t* bytes_read) {\n";
    src << "    const size_t needed = " << ctx.prefix << "_snapshot_size(fsm);\n";
    src << "    uint32_t magic = 0U;\n";
    src << "    uint32_t version = 0U;\n";
    src << "    uint32_t state_u32 = 0U;\n";
    src << "    uint64_t elapsed_ms = 0ULL;\n";
    src << "    " << ctx.prefix << "_registers_t tmp_regs;\n";
    src << "    uint32_t stored_crc = 0U;\n";
    src << "    uint32_t computed_crc = 0U;\n";
    src << "    size_t offset = 0U;\n";
    src << "    if (fsm == NULL || buf == NULL || size < needed) {\n";
    src << "        if (bytes_read != NULL) {\n";
    src << "            *bytes_read = 0U;\n";
    src << "        }\n";
    src << "        return false;\n";
    src << "    }\n";
    src << "    memcpy(&magic, buf + offset, 4U); offset += 4U;\n";
    src << "    memcpy(&version, buf + offset, 4U); offset += 4U;\n";
    src << "    memcpy(&state_u32, buf + offset, 4U); offset += 4U;\n";
    src << "    memcpy(&elapsed_ms, buf + offset, 8U); offset += 8U;\n";
    src << "    memcpy(&tmp_regs, buf + offset, sizeof(tmp_regs)); offset += sizeof(tmp_regs);\n";
    src << "    computed_crc = " << ctx.prefix << "_fnv1a_32(buf, offset);\n";
    src << "    memcpy(&stored_crc, buf + offset, 4U); offset += 4U;\n";
    src << "    if (magic != 0x434D5346U || version != 1U || state_u32 >= (uint32_t)" << ctx.upper_prefix
        << "_STATE_COUNT || stored_crc != computed_crc) {\n";
    src << "        if (bytes_read != NULL) {\n";
    src << "            *bytes_read = 0U;\n";
    src << "        }\n";
    src << "        return false;\n";
    src << "    }\n";
    src << "    fsm->state = (" << ctx.prefix << "_state_t)state_u32;\n";
    src << "    fsm->state_elapsed_ms = elapsed_ms;\n";
    src << "    fsm->regs = tmp_regs;\n";
    src << "    if (bytes_read != NULL) {\n";
    src << "        *bytes_read = offset;\n";
    src << "    }\n";
    src << "    return true;\n";
    src << "}\n\n";

    // Sampled step (anonymous non-timed transitions)
    {
        std::unordered_map<std::string, std::vector<const TransitionEdge*>> anon_by_state;
        for (const auto& tr : processed.transitions) {
            if (!std::holds_alternative<TimeTrigger>(tr.trigger) &&
                (tr.event.empty() || tr.event == "anonymous_event" || tr.event == "completion_event")) {
                anon_by_state[tr.source].push_back(&tr);
            }
        }
        src << ctx.prefix << "_status_t " << ctx.prefix << "_step(" << ctx.prefix << "_fsm_t* fsm, const " << ctx.prefix
            << "_in_ports_t* in_ports, " << ctx.prefix << "_out_ports_t* out_ports) {\n";
        src << "    " << ctx.prefix << "_in_ports_t in_val;\n";
        src << "    " << ctx.prefix << "_out_ports_t out_val;\n";
        src << "    " << ctx.prefix << "_status_t status = " << ctx.upper_prefix << "_STATUS_IGNORED;\n";
        src << "    if (fsm == NULL) {\n";
        src << "        return " << ctx.upper_prefix << "_STATUS_INVALID_ARG;\n";
        src << "    }\n";
        src << "    memset(&in_val, 0, sizeof(in_val));\n";
        src << "    memset(&out_val, 0, sizeof(out_val));\n";
        src << "    if (in_ports != NULL) {\n";
        src << "        in_val = *in_ports;\n";
        src << "    }\n";
        src << "    (void)in_val;\n";
        if (!anon_by_state.empty()) {
            const std::unordered_set<std::string> no_attrs;
            src << "    switch (fsm->state) {\n";
            for (const auto& st : processed.states) {
                auto it = anon_by_state.find(st.name);
                if (it == anon_by_state.end()) {
                    continue;
                }
                src << "        case " << ctx.state_enums.at(st.name) << ":\n";
                for (const auto* tr : it->second) {
                    const std::string g_expr = resolve_guard_expr_c(*tr, processed, ctx, no_attrs);
                    if (!g_expr.empty()) {
                        src << "            if (" << g_expr << ") {\n";
                        emit_transition_body_c(src, *tr, processed, ctx, no_attrs, "                ");
                        src << "                status = " << ctx.upper_prefix << "_STATUS_OK;\n";
                        src << "                break;\n";
                        src << "            }\n";
                    } else {
                        emit_transition_body_c(src, *tr, processed, ctx, no_attrs, "            ");
                        src << "            status = " << ctx.upper_prefix << "_STATUS_OK;\n";
                        src << "            break;\n";
                    }
                }
                src << "            break;\n";
            }
            src << "        default:\n";
            src << "            break;\n";
            src << "    }\n";
        }
        src << "    if (out_ports != NULL) {\n";
        src << "        *out_ports = out_val;\n";
        src << "    }\n";
        src << "    return status;\n";
        src << "}\n\n";
    }

    // Timed tick (advances state_elapsed_ms, evaluates TimeTrigger transitions, then sampled step)
    {
        std::unordered_map<std::string, std::vector<const TransitionEdge*>> timed_by_state;
        for (const auto& tr : processed.transitions) {
            if (std::holds_alternative<TimeTrigger>(tr.trigger)) {
                timed_by_state[tr.source].push_back(&tr);
            }
        }
        src << ctx.prefix << "_status_t " << ctx.prefix << "_tick(" << ctx.prefix
            << "_fsm_t* fsm, uint64_t delta_ms, const " << ctx.prefix << "_in_ports_t* in_ports, " << ctx.prefix
            << "_out_ports_t* out_ports) {\n";
        src << "    " << ctx.prefix << "_in_ports_t in_val;\n";
        src << "    " << ctx.prefix << "_out_ports_t out_val;\n";
        src << "    " << ctx.prefix << "_status_t status = " << ctx.upper_prefix << "_STATUS_IGNORED;\n";
        src << "    if (fsm == NULL) {\n";
        src << "        return " << ctx.upper_prefix << "_STATUS_INVALID_ARG;\n";
        src << "    }\n";
        src << "    memset(&in_val, 0, sizeof(in_val));\n";
        src << "    memset(&out_val, 0, sizeof(out_val));\n";
        src << "    if (in_ports != NULL) {\n";
        src << "        in_val = *in_ports;\n";
        src << "    }\n";
        src << "    (void)in_val;\n";
        src << "    fsm->state_elapsed_ms += delta_ms;\n";
        if (!timed_by_state.empty()) {
            const std::unordered_set<std::string> no_attrs;
            src << "    switch (fsm->state) {\n";
            for (const auto& st : processed.states) {
                auto it = timed_by_state.find(st.name);
                if (it == timed_by_state.end()) {
                    continue;
                }
                src << "        case " << ctx.state_enums.at(st.name) << ":\n";
                for (const auto* tr : it->second) {
                    const auto& tt = std::get<TimeTrigger>(tr->trigger);
                    const uint64_t dur = tt.duration_in_ms();
                    const bool is_periodic = (tt.periodic || tt.kind == TimeTriggerKind::Every);
                    const std::string g_expr = resolve_guard_expr_c(*tr, processed, ctx, no_attrs);
                    src << "            if (fsm->state_elapsed_ms >= " << dur << "ULL";
                    if (!g_expr.empty()) {
                        src << " && (" << g_expr << ")";
                    }
                    src << ") {\n";
                    emit_transition_body_c(src, *tr, processed, ctx, no_attrs, "                ", is_periodic, dur);
                    src << "                status = " << ctx.upper_prefix << "_STATUS_OK;\n";
                    src << "                break;\n";
                    src << "            }\n";
                }
                src << "            break;\n";
            }
            src << "        default:\n";
            src << "            break;\n";
            src << "    }\n";
        }
        src << "    if (status == " << ctx.upper_prefix << "_STATUS_OK) {\n";
        src << "        if (out_ports != NULL) {\n";
        src << "            *out_ports = out_val;\n";
        src << "        }\n";
        src << "        return status;\n";
        src << "    }\n";
        src << "    return " << ctx.prefix << "_step(fsm, in_ports, out_ports);\n";
        src << "}\n\n";
    }

    // Event dispatchers
    for (const auto& sig : events) {
        const std::string ev_snake = to_snake_case(sig.name);
        src << ctx.prefix << "_status_t " << ctx.prefix << "_dispatch_" << ev_snake << "(" << ctx.prefix
            << "_fsm_t* fsm";

        std::unordered_set<std::string> ev_attrs;
        std::vector<C99Field> sig_params;
        for (const auto& attr : sig.attributes) {
            C99Field cf;
            cf.name = sanitize_c_ident(attr.name);
            if (map_type_to_c99(attr.type, ctx, cf.c_type, cf.is_struct)) {
                if (cf.is_struct) {
                    src << ", const " << cf.c_type << "* " << cf.name;
                } else {
                    src << ", " << cf.c_type << " " << cf.name;
                }
                ev_attrs.insert(attr.name);
                sig_params.push_back(std::move(cf));
            }
        }
        src << ", const " << ctx.prefix << "_in_ports_t* in_ports, " << ctx.prefix << "_out_ports_t* out_ports) {\n";
        src << "    " << ctx.prefix << "_in_ports_t in_val;\n";
        src << "    " << ctx.prefix << "_out_ports_t out_val;\n";
        src << "    " << ctx.prefix << "_status_t status = " << ctx.upper_prefix << "_STATUS_IGNORED;\n";
        for (const auto& sp : sig_params) {
            src << "    " << sp.c_type << " ev_" << sp.name << ";\n";
        }
        src << "    if (fsm == NULL) {\n";
        src << "        return " << ctx.upper_prefix << "_STATUS_INVALID_ARG;\n";
        src << "    }\n";
        src << "    memset(&in_val, 0, sizeof(in_val));\n";
        src << "    memset(&out_val, 0, sizeof(out_val));\n";
        src << "    if (in_ports != NULL) {\n";
        src << "        in_val = *in_ports;\n";
        src << "    }\n";
        src << "    (void)in_val;\n";
        for (const auto& sp : sig_params) {
            if (sp.is_struct) {
                src << "    memset(&ev_" << sp.name << ", 0, sizeof(ev_" << sp.name << "));\n";
                src << "    if (" << sp.name << " != NULL) {\n";
                src << "        ev_" << sp.name << " = *" << sp.name << ";\n";
                src << "    }\n";
            } else {
                src << "    ev_" << sp.name << " = " << sp.name << ";\n";
            }
            src << "    (void)ev_" << sp.name << ";\n";
        }

        // Group transitions matching this signal by source state
        std::unordered_map<std::string, std::vector<const TransitionEdge*>> by_state;
        for (const auto& tr : processed.transitions) {
            if (!std::holds_alternative<TimeTrigger>(tr.trigger) && tr.event == sig.name) {
                by_state[tr.source].push_back(&tr);
            }
        }

        if (!by_state.empty()) {
            src << "    switch (fsm->state) {\n";
            for (const auto& st : processed.states) {
                auto it = by_state.find(st.name);
                if (it == by_state.end()) {
                    continue;
                }
                src << "        case " << ctx.state_enums.at(st.name) << ":\n";
                for (const auto* tr : it->second) {
                    const std::string g_expr = resolve_guard_expr_c(*tr, processed, ctx, ev_attrs);
                    if (!g_expr.empty()) {
                        src << "            if (" << g_expr << ") {\n";
                        emit_transition_body_c(src, *tr, processed, ctx, ev_attrs, "                ");
                        src << "                status = " << ctx.upper_prefix << "_STATUS_OK;\n";
                        src << "                break;\n";
                        src << "            } else {\n";
                        src << "                status = " << ctx.upper_prefix << "_STATUS_GUARD_REJECTED;\n";
                        src << "            }\n";
                    } else {
                        emit_transition_body_c(src, *tr, processed, ctx, ev_attrs, "            ");
                        src << "            status = " << ctx.upper_prefix << "_STATUS_OK;\n";
                        src << "            break;\n";
                    }
                }
                src << "            break;\n";
            }
            src << "        default:\n";
            src << "            break;\n";
            src << "    }\n";
        }

        src << "    if (out_ports != NULL) {\n";
        src << "        *out_ports = out_val;\n";
        src << "    }\n";
        src << "    return status;\n";
        src << "}\n\n";
    }

    return GeneratedC99Bundle{hdr.str(), src.str()};
}

}  // namespace fsm::backend::c99
