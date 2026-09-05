#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::ir {

class FsmIrSerializer {
  public:
    // ========================================================================
    // JSON Serialization
    // ========================================================================
    static std::string serialize_json(const FsmIr& ir, int indent_spaces = 2) {
        std::ostringstream ss;
        std::string indent(indent_spaces, ' ');

        ss << "{\n";
        ss << indent << "\"id\": \"" << escape_json(ir.id) << "\",\n";
        ss << indent << "\"name\": \"" << escape_json(ir.name) << "\",\n";
        ss << indent << "\"package\": \"" << escape_json(ir.package) << "\",\n";
        ss << indent << "\"initial_state_id\": \"" << escape_json(ir.initial_state_id) << "\",\n";

        // Attributes
        ss << indent << "\"attributes\": {";
        bool first_attr = true;
        for (const auto& [k, v] : ir.attributes) {
            if (!first_attr)
                ss << ", ";
            first_attr = false;
            ss << "\"" << escape_json(k) << "\": \"" << escape_json(v) << "\"";
        }
        ss << "},\n";

        // Requirements
        ss << indent << "\"satisfies_reqs\": [";
        for (std::size_t i = 0; i < ir.satisfies_reqs.size(); ++i) {
            if (i > 0)
                ss << ", ";
            ss << "\"" << escape_json(ir.satisfies_reqs[i]) << "\"";
        }
        ss << "],\n";

        // Typed I/O Ports & Domain Contracts (SysML v2)
        ss << indent << "\"ports\": [\n";
        for (std::size_t i = 0; i < ir.ports.size(); ++i) {
            const auto& port = ir.ports[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(port.name) << "\",\n";
            ss << indent << indent << indent << "\"type\": \"" << escape_json(port.type) << "\",\n";
            ss << indent << indent << indent << "\"type_kind\": \"" << variable_type_kind_to_string(port.type_kind)
               << "\",\n";
            ss << indent << indent << indent << "\"direction\": \"" << port_direction_to_string(port.direction)
               << "\",\n";
            if (port.min_value.has_value()) {
                ss << indent << indent << indent << "\"min_value\": " << *port.min_value << ",\n";
            } else {
                ss << indent << indent << indent << "\"min_value\": null,\n";
            }
            if (port.max_value.has_value()) {
                ss << indent << indent << indent << "\"max_value\": " << *port.max_value << ",\n";
            } else {
                ss << indent << indent << indent << "\"max_value\": null,\n";
            }
            ss << indent << indent << indent << "\"constraint\": \"" << escape_json(port.constraint) << "\",\n";
            ss << indent << indent << indent << "\"default_value\": \"" << escape_json(port.default_value) << "\",\n";
            if (port.physical_unit.has_value()) {
                ss << indent << indent << indent << "\"physical_unit\": \"" << escape_json(*port.physical_unit)
                   << "\",\n";
            } else {
                ss << indent << indent << indent << "\"physical_unit\": null,\n";
            }
            ss << indent << indent << indent << "\"description\": \"" << escape_json(port.description) << "\"\n";
            ss << indent << indent << "}" << (i + 1 < ir.ports.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // State Variables (EFSM)
        ss << indent << "\"variables\": [\n";
        for (std::size_t i = 0; i < ir.variables.size(); ++i) {
            const auto& var = ir.variables[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(var.name) << "\",\n";
            ss << indent << indent << indent << "\"type\": \"" << escape_json(var.type) << "\",\n";
            ss << indent << indent << indent << "\"type_kind\": \"" << variable_type_kind_to_string(var.type_kind)
               << "\",\n";
            if (var.physical_unit.has_value()) {
                ss << indent << indent << indent << "\"physical_unit\": \"" << escape_json(*var.physical_unit)
                   << "\",\n";
            } else {
                ss << indent << indent << indent << "\"physical_unit\": null,\n";
            }
            ss << indent << indent << indent << "\"initial_value\": \"" << escape_json(var.initial_value) << "\",\n";
            if (var.min_value.has_value()) {
                ss << indent << indent << indent << "\"min_value\": " << *var.min_value << ",\n";
            } else {
                ss << indent << indent << indent << "\"min_value\": null,\n";
            }
            if (var.max_value.has_value()) {
                ss << indent << indent << indent << "\"max_value\": " << *var.max_value << ",\n";
            } else {
                ss << indent << indent << indent << "\"max_value\": null,\n";
            }
            ss << indent << indent << indent << "\"description\": \"" << escape_json(var.description) << "\"\n";
            ss << indent << indent << "}" << (i + 1 < ir.variables.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // User-Defined Enums (SysML v2 enum def)
        const auto enums = ir.get_enums();
        ss << indent << "\"enums\": [\n";
        for (std::size_t i = 0; i < enums.size(); ++i) {
            const auto& en = enums[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(en.name) << "\",\n";
            ss << indent << indent << indent << "\"underlying_type\": \"" << escape_json(en.underlying_type) << "\",\n";
            ss << indent << indent << indent << "\"description\": \"" << escape_json(en.description) << "\",\n";
            ss << indent << indent << indent << "\"literals\": [\n";
            for (std::size_t l = 0; l < en.literals.size(); ++l) {
                const auto& lit = en.literals[l];
                ss << indent << indent << indent << indent << "{\"name\": \"" << escape_json(lit.name) << "\"";
                if (lit.value.has_value()) {
                    ss << ", \"value\": " << *lit.value;
                } else {
                    ss << ", \"value\": null";
                }
                ss << ", \"description\": \"" << escape_json(lit.description) << "\"}";
                if (l + 1 < en.literals.size()) {
                    ss << ",";
                }
                ss << "\n";
            }
            ss << indent << indent << indent << "]\n";
            ss << indent << indent << "}" << (i + 1 < enums.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // Structured Data Definitions (SysML v2 struct def & datatype def)
        const auto structs = ir.get_structs();
        ss << indent << "\"structs\": [\n";
        for (std::size_t i = 0; i < structs.size(); ++i) {
            const auto& st = structs[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(st.name) << "\",\n";
            ss << indent << indent << indent << "\"is_datatype\": " << (st.is_datatype ? "true" : "false") << ",\n";
            ss << indent << indent << indent << "\"description\": \"" << escape_json(st.description) << "\",\n";
            ss << indent << indent << indent << "\"fields\": [\n";
            for (std::size_t f = 0; f < st.fields.size(); ++f) {
                const auto& field = st.fields[f];
                ss << indent << indent << indent << indent << "{\n";
                ss << indent << indent << indent << indent << indent << "\"name\": \"" << escape_json(field.name)
                   << "\",\n";
                ss << indent << indent << indent << indent << indent << "\"type\": \"" << escape_json(field.type)
                   << "\",\n";
                ss << indent << indent << indent << indent << indent << "\"default_value\": \""
                   << escape_json(field.default_value) << "\",\n";
                if (field.physical_unit.has_value()) {
                    ss << indent << indent << indent << indent << indent << "\"physical_unit\": \""
                       << escape_json(*field.physical_unit) << "\",\n";
                } else {
                    ss << indent << indent << indent << indent << indent << "\"physical_unit\": null,\n";
                }
                if (field.min_value.has_value()) {
                    ss << indent << indent << indent << indent << indent << "\"min_value\": " << *field.min_value
                       << ",\n";
                } else {
                    ss << indent << indent << indent << indent << indent << "\"min_value\": null,\n";
                }
                if (field.max_value.has_value()) {
                    ss << indent << indent << indent << indent << indent << "\"max_value\": " << *field.max_value
                       << ",\n";
                } else {
                    ss << indent << indent << indent << indent << indent << "\"max_value\": null,\n";
                }
                ss << indent << indent << indent << indent << indent << "\"description\": \""
                   << escape_json(field.description) << "\"\n";
                ss << indent << indent << indent << indent << "}" << (f + 1 < st.fields.size() ? "," : "") << "\n";
            }
            ss << indent << indent << indent << "]\n";
            ss << indent << indent << "}" << (i + 1 < structs.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // Compound User-Defined Types (Enum, Struct, Alias)
        ss << indent << "\"types\": [\n";
        for (std::size_t i = 0; i < ir.custom_types.size(); ++i) {
            const auto& ct = ir.custom_types[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(ct.name) << "\",\n";
            ss << indent << indent << indent << "\"kind\": \"" << type_kind_to_string(ct.kind) << "\",\n";
            ss << indent << indent << indent << "\"underlying_type\": \"" << escape_json(ct.underlying_type) << "\",\n";
            ss << indent << indent << indent << "\"is_datatype\": " << (ct.is_datatype ? "true" : "false") << ",\n";
            ss << indent << indent << indent << "\"description\": \"" << escape_json(ct.description) << "\",\n";
            ss << indent << indent << indent << "\"literals\": [\n";
            for (std::size_t l = 0; l < ct.literals.size(); ++l) {
                const auto& lit = ct.literals[l];
                ss << indent << indent << indent << indent << "{\"name\": \"" << escape_json(lit.name) << "\"";
                if (lit.value.has_value()) {
                    ss << ", \"value\": " << *lit.value;
                } else {
                    ss << ", \"value\": null";
                }
                ss << ", \"description\": \"" << escape_json(lit.description) << "\"}";
                if (l + 1 < ct.literals.size()) {
                    ss << ",";
                }
                ss << "\n";
            }
            ss << indent << indent << indent << "],\n";
            ss << indent << indent << indent << "\"fields\": [\n";
            for (std::size_t f = 0; f < ct.fields.size(); ++f) {
                const auto& field = ct.fields[f];
                ss << indent << indent << indent << indent << "{\n";
                ss << indent << indent << indent << indent << indent << "\"name\": \"" << escape_json(field.name)
                   << "\",\n";
                ss << indent << indent << indent << indent << indent << "\"type\": \"" << escape_json(field.type)
                   << "\",\n";
                ss << indent << indent << indent << indent << indent << "\"default_value\": \""
                   << escape_json(field.default_value) << "\",\n";
                if (field.physical_unit.has_value()) {
                    ss << indent << indent << indent << indent << indent << "\"physical_unit\": \""
                       << escape_json(*field.physical_unit) << "\",\n";
                } else {
                    ss << indent << indent << indent << indent << indent << "\"physical_unit\": null,\n";
                }
                if (field.min_value.has_value()) {
                    ss << indent << indent << indent << indent << indent << "\"min_value\": " << *field.min_value
                       << ",\n";
                } else {
                    ss << indent << indent << indent << indent << indent << "\"min_value\": null,\n";
                }
                if (field.max_value.has_value()) {
                    ss << indent << indent << indent << indent << indent << "\"max_value\": " << *field.max_value
                       << ",\n";
                } else {
                    ss << indent << indent << indent << indent << indent << "\"max_value\": null,\n";
                }
                ss << indent << indent << indent << indent << indent << "\"description\": \""
                   << escape_json(field.description) << "\"\n";
                ss << indent << indent << indent << indent << "}" << (f + 1 < ct.fields.size() ? "," : "") << "\n";
            }
            ss << indent << indent << indent << "]\n";
            ss << indent << indent << "}" << (i + 1 < ir.custom_types.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // Formal Properties (LTL/CTL & Safety Invariants)
        ss << indent << "\"properties\": [\n";
        for (std::size_t i = 0; i < ir.properties.size(); ++i) {
            const auto& prop = ir.properties[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"id\": \"" << escape_json(prop.id) << "\",\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(prop.name) << "\",\n";
            ss << indent << indent << indent << "\"kind\": \"" << property_kind_to_string(prop.kind) << "\",\n";
            ss << indent << indent << indent << "\"raw_formula\": \"" << escape_json(prop.raw_formula) << "\",\n";
            if (prop.ast.has_value()) {
                ss << indent << indent << indent << "\"ast\": \"" << escape_json(prop.ast->to_string()) << "\",\n";
            } else {
                ss << indent << indent << indent << "\"ast\": null,\n";
            }
            ss << indent << indent << indent << "\"traceability_req\": \"" << escape_json(prop.traceability_req)
               << "\",\n";
            ss << indent << indent << indent << "\"description\": \"" << escape_json(prop.description) << "\"\n";
            ss << indent << indent << "}" << (i + 1 < ir.properties.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // Signals
        ss << indent << "\"signals\": [\n";
        for (std::size_t i = 0; i < ir.signals.size(); ++i) {
            const auto& sig = ir.signals[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(sig.name) << "\",\n";
            ss << indent << indent << indent << "\"attributes\": [";
            for (std::size_t a = 0; a < sig.attributes.size(); ++a) {
                if (a > 0)
                    ss << ", ";
                ss << "{\"name\": \"" << escape_json(sig.attributes[a].name) << "\", \"type\": \""
                   << escape_json(sig.attributes[a].type) << "\", \"default\": \""
                   << escape_json(sig.attributes[a].default_value) << "\"}";
            }
            ss << "],\n";
            ss << indent << indent << indent << "\"validators\": [";
            for (std::size_t v = 0; v < sig.validators.size(); ++v) {
                if (v > 0)
                    ss << ", ";
                ss << "\"" << escape_json(sig.validators[v]) << "\"";
            }
            ss << "]\n";
            ss << indent << indent << "}" << (i + 1 < ir.signals.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // States
        ss << indent << "\"states\": [\n";
        for (std::size_t i = 0; i < ir.states.size(); ++i) {
            const auto& st = ir.states[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"id\": \"" << escape_json(st.id) << "\",\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(st.name) << "\",\n";
            ss << indent << indent << indent << "\"fqn\": \"" << escape_json(st.fqn) << "\",\n";
            ss << indent << indent << indent << "\"kind\": \"" << state_kind_to_string(st.kind) << "\",\n";
            if (st.parent_id.has_value()) {
                ss << indent << indent << indent << "\"parent_id\": \"" << escape_json(*st.parent_id) << "\",\n";
            } else {
                ss << indent << indent << indent << "\"parent_id\": null,\n";
            }
            ss << indent << indent << indent << "\"children_ids\": [";
            for (std::size_t c = 0; c < st.children_ids.size(); ++c) {
                if (c > 0)
                    ss << ", ";
                ss << "\"" << escape_json(st.children_ids[c]) << "\"";
            }
            ss << "],\n";

            // Orthogonal regions
            ss << indent << indent << indent << "\"orthogonal_regions\": [";
            for (std::size_t r = 0; r < st.orthogonal_regions.size(); ++r) {
                if (r > 0)
                    ss << ", ";
                const auto& reg = st.orthogonal_regions[r];
                ss << "{\"id\": \"" << escape_json(reg.id) << "\", \"name\": \"" << escape_json(reg.name)
                   << "\", \"initial_state_id\": \"" << escape_json(reg.initial_state_id) << "\", \"state_ids\": [";
                for (std::size_t s = 0; s < reg.state_ids.size(); ++s) {
                    if (s > 0)
                        ss << ", ";
                    ss << "\"" << escape_json(reg.state_ids[s]) << "\"";
                }
                ss << "]}";
            }
            ss << "],\n";

            // Submachine
            if (st.submachine.has_value()) {
                ss << indent << indent << indent << "\"submachine\": {\"fsm_name\": \""
                   << escape_json(st.submachine->fsm_name) << "\", \"source_uri\": \""
                   << escape_json(st.submachine->source_uri) << "\", \"port_mappings\": [";
                for (std::size_t pm = 0; pm < st.submachine->port_mappings.size(); ++pm) {
                    if (pm > 0)
                        ss << ", ";
                    ss << "{\"entry\": \"" << escape_json(st.submachine->port_mappings[pm].entry_point)
                       << "\", \"exit\": \"" << escape_json(st.submachine->port_mappings[pm].exit_point) << "\"}";
                }
                ss << "]},\n";
            } else {
                ss << indent << indent << indent << "\"submachine\": null,\n";
            }

            ss << indent << indent << indent << "\"deferred_events\": [";
            for (std::size_t d = 0; d < st.deferred_events.size(); ++d) {
                if (d > 0)
                    ss << ", ";
                ss << "\"" << escape_json(st.deferred_events[d]) << "\"";
            }
            ss << "],\n";
            ss << indent << indent << indent << "\"traceability_reqs\": [";
            for (std::size_t r = 0; r < st.traceability_reqs.size(); ++r) {
                if (r > 0)
                    ss << ", ";
                ss << "\"" << escape_json(st.traceability_reqs[r]) << "\"";
            }
            ss << "],\n";
            if (st.do_activity.has_value()) {
                ss << indent << indent << indent << "\"do_activity\": \"" << escape_json(*st.do_activity) << "\",\n";
            } else {
                ss << indent << indent << indent << "\"do_activity\": null,\n";
            }
            if (st.time_invariant.has_value()) {
                ss << indent << indent << indent << "\"time_invariant\": \"" << escape_json(*st.time_invariant)
                   << "\"\n";
            } else {
                ss << indent << indent << indent << "\"time_invariant\": null\n";
            }
            ss << indent << indent << "}" << (i + 1 < ir.states.size() ? "," : "") << "\n";
        }
        ss << indent << "],\n";

        // Transitions
        ss << indent << "\"transitions\": [\n";
        for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
            const auto& tr = ir.transitions[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"id\": \"" << escape_json(tr.id) << "\",\n";
            ss << indent << indent << indent << "\"source\": \"" << escape_json(tr.source) << "\",\n";
            ss << indent << indent << indent << "\"target\": \"" << escape_json(tr.target) << "\",\n";
            ss << indent << indent << indent << "\"source_ids\": [";
            for (std::size_t s = 0; s < tr.source_ids.size(); ++s) {
                if (s > 0)
                    ss << ", ";
                ss << "\"" << escape_json(tr.source_ids[s]) << "\"";
            }
            ss << "],\n";
            ss << indent << indent << indent << "\"target_ids\": [";
            for (std::size_t t = 0; t < tr.target_ids.size(); ++t) {
                if (t > 0)
                    ss << ", ";
                ss << "\"" << escape_json(tr.target_ids[t]) << "\"";
            }
            ss << "],\n";
            ss << indent << indent << indent << "\"kind\": \"" << transition_edge_kind_to_string(tr.kind) << "\",\n";
            ss << indent << indent << indent << "\"priority\": " << tr.priority << ",\n";
            ss << indent << indent << indent << "\"trigger\": \"" << escape_json(tr.get_trigger_name()) << "\",\n";
            if (std::holds_alternative<TimeTrigger>(tr.trigger)) {
                const auto& tt = std::get<TimeTrigger>(tr.trigger);
                ss << indent << indent << indent << "\"time_trigger\": {\"kind\": \""
                   << time_trigger_kind_to_string(tt.kind) << "\", \"duration_value\": " << tt.duration_value
                   << ", \"unit\": \"" << time_unit_to_string(tt.unit) << "\", \"dynamic_expression\": \""
                   << escape_json(tt.dynamic_expression) << "\", \"duration_ms\": " << tt.duration_in_ms() << "},\n";
            } else if (std::holds_alternative<SignalTrigger>(tr.trigger)) {
                const auto& st = std::get<SignalTrigger>(tr.trigger);
                ss << indent << indent << indent << "\"signal_trigger\": {\"signal_name\": \""
                   << escape_json(st.signal_name) << "\", \"payload_binding\": \"" << escape_json(st.payload_binding)
                   << "\"},\n";
            } else {
                ss << indent << indent << indent << "\"completion_trigger\": true,\n";
            }
            if (tr.guard_ast.has_value()) {
                ss << indent << indent << indent << "\"guard_ast\": \"" << escape_json(tr.guard_ast->to_string())
                   << "\",\n";
            } else {
                ss << indent << indent << indent << "\"guard_ast\": null,\n";
            }
            if (tr.action_sig.has_value()) {
                ss << indent << indent << indent << "\"action_sig\": \"" << escape_json(tr.action_sig->name) << "\",\n";
                ss << indent << indent << indent << "\"assignments\": [";
                for (std::size_t a = 0; a < tr.action_sig->assignments.size(); ++a) {
                    if (a > 0)
                        ss << ", ";
                    const auto& assign = tr.action_sig->assignments[a];
                    ss << "{\"variable\": \"" << escape_json(assign.target_variable)
                       << "\", \"op\": \"" << assignment_op_to_string(assign.op)
                       << "\", \"expression\": \"" << escape_json(assign.expression) << "\"";
                    if (assign.expr_ast.has_value()) {
                        ss << ", \"ast\": " << assign.expr_ast->to_json();
                    }
                    ss << "}";
                }
                ss << "]\n";
            } else {
                ss << indent << indent << indent << "\"action_sig\": null,\n";
                ss << indent << indent << indent << "\"assignments\": []\n";
            }
            ss << indent << indent << "}" << (i + 1 < ir.transitions.size() ? "," : "") << "\n";
        }
        ss << indent << "]\n";

        ss << "}\n";
        return ss.str();
    }

  private:
    static std::string escape_json(std::string_view s) {
        std::ostringstream ss;
        for (char c : s) {
            switch (c) {
                case '"':
                    ss << "\\\"";
                    break;
                case '\\':
                    ss << "\\\\";
                    break;
                case '\b':
                    ss << "\\b";
                    break;
                case '\f':
                    ss << "\\f";
                    break;
                case '\n':
                    ss << "\\n";
                    break;
                case '\r':
                    ss << "\\r";
                    break;
                case '\t':
                    ss << "\\t";
                    break;
                default:
                    ss << c;
                    break;
            }
        }
        return ss.str();
    }
};

}  // namespace fsm::ir

namespace fsm {
using FsmIrSerializer = ir::FsmIrSerializer;
}  // namespace fsm

