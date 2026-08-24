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
                ss << indent << indent << indent << "\"do_activity\": \"" << escape_json(*st.do_activity) << "\"\n";
            } else {
                ss << indent << indent << indent << "\"do_activity\": null\n";
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
            ss << indent << indent << indent << "\"kind\": \"" << transition_edge_kind_to_string(tr.kind) << "\",\n";
            ss << indent << indent << indent << "\"trigger\": \"" << escape_json(tr.get_trigger_name()) << "\",\n";
            if (tr.guard_ast.has_value()) {
                ss << indent << indent << indent << "\"guard_ast\": \"" << escape_json(tr.guard_ast->to_string())
                   << "\",\n";
            } else {
                ss << indent << indent << indent << "\"guard_ast\": null,\n";
            }
            if (tr.action_sig.has_value()) {
                ss << indent << indent << indent << "\"action_sig\": \"" << escape_json(tr.action_sig->name) << "\"\n";
            } else {
                ss << indent << indent << indent << "\"action_sig\": null\n";
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
