#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

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
        ss << indent << "\"ns\": \"" << escape_json(ir.ns) << "\",\n";
        ss << indent << "\"context_type\": \"" << escape_json(ir.context_type) << "\",\n";
        ss << indent << "\"initial_state_id\": \"" << escape_json(ir.initial_state_id) << "\",\n";
        ss << indent << "\"thread_safe\": " << (ir.thread_safe ? "true" : "false") << ",\n";

        // Requirements
        ss << indent << "\"satisfies_reqs\": [";
        for (std::size_t i = 0; i < ir.satisfies_reqs.size(); ++i) {
            if (i > 0)
                ss << ", ";
            ss << "\"" << escape_json(ir.satisfies_reqs[i]) << "\"";
        }
        ss << "],\n";

        // State Variables (EFSM)
        ss << indent << "\"variables\": [\n";
        for (std::size_t i = 0; i < ir.variables.size(); ++i) {
            const auto& var = ir.variables[i];
            ss << indent << indent << "{\n";
            ss << indent << indent << indent << "\"name\": \"" << escape_json(var.name) << "\",\n";
            ss << indent << indent << indent << "\"type\": \"" << escape_json(var.type) << "\",\n";
            ss << indent << indent << indent << "\"type_kind\": \""
               << variable_type_kind_to_string(var.type_kind) << "\",\n";
            if (var.physical_unit.has_value()) {
                ss << indent << indent << indent << "\"physical_unit\": \"" << escape_json(*var.physical_unit) << "\",\n";
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
            ss << indent << indent << indent << "\"source_id\": \"" << escape_json(tr.source_id) << "\",\n";
            ss << indent << indent << indent << "\"target_id\": \"" << escape_json(tr.target_id) << "\",\n";
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
                    ss << "{\"variable\": \"" << escape_json(tr.action_sig->assignments[a].target_variable)
                       << "\", \"expression\": \"" << escape_json(tr.action_sig->assignments[a].expression) << "\"}";
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

}  // namespace fsm::codegen

namespace fsm {
using FsmIrSerializer = fsm::codegen::FsmIrSerializer;
}  // namespace fsm
