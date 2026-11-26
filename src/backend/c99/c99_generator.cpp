#include "fsm/backend/c99/c99_generator.hpp"

#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "c99_emitter_internal.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/guard.hpp"

namespace fsm::backend::c99 {

using namespace fsm::ir;
using namespace c99_detail;

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
            const std::string en_upper = ctx.upper_prefix + "_" + to_upper_ascii(to_snake_case(td.name));
            for (const auto& lit : td.literals) {
                const std::string c_lit = en_upper + "_" + to_upper_ascii(to_snake_case(lit.name));
                ctx.enum_literals[td.name + "::" + lit.name] = c_lit;
                ctx.enum_literals[td.name + "." + lit.name] = c_lit;
            }
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

    // Collect emitted signals (SignalEmitOp) for static zero-heap outbox
    {
        std::unordered_set<std::string> seen_emits;
        std::size_t max_burst = 0U;
        auto scan_emits = [&](const ActionSignature& act) {
            std::size_t burst = 0U;
            for (const auto& inst : act.instructions) {
                if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                    if (!se->signal_name.empty()) {
                        ++burst;
                        if (seen_emits.insert(se->signal_name).second) {
                            ctx.emitted_signals.push_back(se->signal_name);
                            ctx.emitted_signals_map[se->signal_name] = processed.find_signal(se->signal_name);
                        }
                    }
                }
            }
            if (burst > max_burst) {
                max_burst = burst;
            }
        };
        for (const auto& st : processed.states) {
            for (const auto& act : st.entry_actions) {
                scan_emits(act);
            }
            for (const auto& act : st.exit_actions) {
                scan_emits(act);
            }
        }
        for (const auto& tr : processed.transitions) {
            if (tr.condition_action.has_value()) {
                scan_emits(*tr.condition_action);
            }
            if (tr.transition_action.has_value()) {
                scan_emits(*tr.transition_action);
            }
        }
        if (!ctx.emitted_signals.empty()) {
            ctx.max_outbox = (max_burst * 2U > 4U) ? (max_burst * 2U) : 4U;
        }
    }

    // Collect symbolic guard predicates (guards with no inline expression) as C99 bool (*)(void*) callbacks
    {
        auto is_simple_ident = [](std::string_view s) {
            if (s.empty()) {
                return false;
            }
            const auto first = static_cast<unsigned char>(s[0]);
            if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
                return false;
            }
            for (std::size_t i = 1; i < s.size(); ++i) {
                const auto ch = static_cast<unsigned char>(s[i]);
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) {
                    return false;
                }
            }
            return true;
        };

        std::unordered_set<std::string> known_symbols{"true", "false", "else", "and", "or", "not"};
        for (const auto& p : ctx.in_ports) {
            known_symbols.insert(p);
        }
        for (const auto& p : ctx.out_ports) {
            known_symbols.insert(p);
        }
        for (const auto& r : ctx.registers) {
            known_symbols.insert(r);
        }
        for (const auto& [svc_name, _] : ctx.services) {
            known_symbols.insert(svc_name);
        }
        for (const auto* en : local_enums) {
            for (const auto& lit : en->literals) {
                known_symbols.insert(lit.name);
            }
        }
        for (const auto& sig : events) {
            for (const auto& attr : sig.attributes) {
                known_symbols.insert(attr.name);
            }
        }

        auto maybe_register_guard_cb = [&](std::string_view name) {
            if (!is_simple_ident(name)) {
                return;
            }
            const std::string key(name);
            if (known_symbols.count(key) != 0U) {
                return;
            }
            if (ctx.guard_callbacks.insert(key).second) {
                ctx.guard_callbacks_ordered.push_back(key);
            }
        };

        for (const auto& g : processed.guards) {
            if (!g.raw_expression.has_value() || g.raw_expression->empty() || *g.raw_expression == g.name) {
                maybe_register_guard_cb(g.name);
            }
        }
        for (const auto& tr : processed.transitions) {
            if (!tr.guard.has_value() || tr.guard->empty() || *tr.guard == "else") {
                continue;
            }
            std::string expanded = *tr.guard;
            if (expanded.find("fsm::") != std::string::npos) {
                expanded = GuardExpressionParser::to_diagram_string(expanded, processed.guards);
            } else if (const auto* g = processed.find_guard(expanded)) {
                if (g->raw_expression.has_value() && !g->raw_expression->empty()) {
                    expanded = *g->raw_expression;
                }
            }
            const auto parsed = GuardExpressionParser::parse(expanded);
            if (!parsed.atomic_guard_details.empty()) {
                for (const auto& leaf : parsed.atomic_guard_details) {
                    maybe_register_guard_cb(leaf.expression);
                }
            } else {
                maybe_register_guard_cb(expanded);
            }
        }
    }

    bool has_deferred = false;
    if (!events.empty()) {
        for (const auto& st : processed.states) {
            if (!st.deferred_events.empty()) {
                has_deferred = true;
                break;
            }
        }
    }

    const bool has_typed_services = !processed.services.empty() || !ctx.guard_callbacks_ordered.empty();

    const C99EmitPlan plan{
        processed, ctx,        header_inc, ir_sha256, external_types,     local_enums,  local_structs,
        in_fields, out_fields, reg_fields, events,    has_typed_services, has_deferred, options.omit_header_include};
    return GeneratedC99Bundle{emit_c99_header(plan), emit_c99_source(plan)};
}

}  // namespace fsm::backend::c99
