#include "fsm/backend/c99/c99_generator.hpp"

#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "c99_emitter_internal.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

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

    const bool has_typed_services = !processed.services.empty();

    const C99EmitPlan plan{processed,     ctx,       header_inc, ir_sha256,  external_types, local_enums,
                           local_structs, in_fields, out_fields, reg_fields, events,         has_typed_services};
    return GeneratedC99Bundle{emit_c99_header(plan), emit_c99_source(plan)};
}

}  // namespace fsm::backend::c99
