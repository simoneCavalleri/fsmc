#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/sha256.hpp"
#include "ir_serializer_internal.hpp"

namespace fsm::ir {

using namespace json_detail;

// ============================================================================
// FsmIrSerializer Public API Implementation
// ============================================================================

std::string FsmIrSerializer::serialize_json(const FsmIr& ir, int indent_spaces) {
    (void)indent_spaces;
    std::ostringstream out;
    std::string sm_name = ir.name.empty() ? "StateMachine" : ir.name;
    out << "{\n";
    out << "  \"ir_schema_version\": \"" << kIrSchemaVersion << "\",\n";
    out << "  \"id\": \"" << escape_json(sm_name) << "\",\n";
    if (!ir.id.empty()) {
        out << "  \"fsm_id\": \"" << escape_json(ir.id) << "\",\n";
    }
    out << "  \"name\": \"" << escape_json(sm_name) << "\",\n";

    if (!ir.initial_state.empty()) {
        out << "  \"initial\": \"" << escape_json(ir.initial_state) << "\",\n";
    }
    if (!ir.initial_state_id.empty()) {
        out << "  \"initial_state_id\": \"" << escape_json(ir.initial_state_id) << "\",\n";
    }

    if (!ir.package.empty()) {
        out << "  \"package\": \"" << escape_json(ir.package) << "\",\n";
    }

    if (!ir.satisfies_reqs.empty()) {
        out << "  \"satisfies_reqs\": [";
        for (size_t r = 0; r < ir.satisfies_reqs.size(); ++r) {
            if (r > 0)
                out << ", ";
            out << "\"" << escape_json(ir.satisfies_reqs[r]) << "\"";
        }
        out << "],\n";
    }

    // Attributes (sorted for byte-identical determinism)
    if (!ir.attributes.empty()) {
        std::map<std::string, std::string> sorted_attrs(ir.attributes.begin(), ir.attributes.end());
        out << "  \"attributes\": {";
        bool first_attr = true;
        for (const auto& [k, v] : sorted_attrs) {
            if (!first_attr)
                out << ", ";
            first_attr = false;
            out << "\"" << escape_json(k) << "\": \"" << escape_json(v) << "\"";
        }
        out << "},\n";
    }

    // Concurrency
    out << "  \"concurrency\": {\n";
    out << "    \"dispatch\": \"" << event_dispatch_semantics_to_string(ir.concurrency.dispatch) << "\",\n";
    out << "    \"orthogonal_conflict\": \""
        << orthogonal_conflict_resolution_to_string(ir.concurrency.orthogonal_conflict) << "\",\n";
    out << "    \"datapath_isolation\": \"" << datapath_isolation_to_string(ir.concurrency.datapath_isolation)
        << "\"\n";
    out << "  },\n";

    // Ports
    if (!ir.ports.empty()) {
        out << "  \"ports\": [\n";
        for (size_t p = 0; p < ir.ports.size(); ++p) {
            const auto& port = ir.ports[p];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(port.name) << "\",\n";
            out << "      \"type\": \"" << escape_json(port.type.to_canonical_string()) << "\",\n";
            out << "      \"direction\": \""
                << (port.direction == PortDirection::InOut ? "inout"
                                                           : (port.direction == PortDirection::Out ? "out" : "in"))
                << "\"";
            if (port.min_value.has_value()) {
                out << ",\n      \"min\": " << *port.min_value;
                out << ",\n      \"min_value\": " << *port.min_value;
            }
            if (port.max_value.has_value()) {
                out << ",\n      \"max\": " << *port.max_value;
                out << ",\n      \"max_value\": " << *port.max_value;
            }
            if (!port.constraint.empty()) {
                out << ",\n      \"constraint\": \"" << escape_json(port.constraint) << "\"";
            }
            if (!port.default_value.empty()) {
                out << ",\n      \"default\": \"" << escape_json(port.default_value) << "\"";
                out << ",\n      \"default_value\": \"" << escape_json(port.default_value) << "\"";
            }
            if (port.physical_unit.has_value()) {
                out << ",\n      \"unit\": \"" << escape_json(*port.physical_unit) << "\"";
                out << ",\n      \"physical_unit\": \"" << escape_json(*port.physical_unit) << "\"";
            }
            if (!port.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(port.description) << "\"";
            }
            if (!port.bound_to.empty()) {
                out << ",\n      \"bound_to\": \"" << escape_json(port.bound_to) << "\"";
            }
            out << "\n    }";
            if (p + 1 < ir.ports.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Variables
    if (!ir.variables.empty()) {
        out << "  \"variables\": [\n";
        for (size_t v = 0; v < ir.variables.size(); ++v) {
            const auto& var = ir.variables[v];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(var.name) << "\",\n";
            out << "      \"type\": \"" << escape_json(var.type.to_canonical_string()) << "\",\n";
            out << "      \"init\": \"" << escape_json(var.initial_value) << "\",\n";
            out << "      \"initial_value\": \"" << escape_json(var.initial_value) << "\"";
            if (var.min_value.has_value()) {
                out << ",\n      \"min\": " << *var.min_value;
                out << ",\n      \"min_value\": " << *var.min_value;
            }
            if (var.max_value.has_value()) {
                out << ",\n      \"max\": " << *var.max_value;
                out << ",\n      \"max_value\": " << *var.max_value;
            }
            if (var.physical_unit.has_value()) {
                out << ",\n      \"unit\": \"" << escape_json(*var.physical_unit) << "\"";
                out << ",\n      \"physical_unit\": \"" << escape_json(*var.physical_unit) << "\"";
            }
            if (var.register_index.has_value()) {
                out << ",\n      \"register_index\": " << *var.register_index;
            }
            if (!var.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(var.description) << "\"";
            }
            out << "\n    }";
            if (v + 1 < ir.variables.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Enums
    const auto enums = ir.get_enums();
    if (!enums.empty()) {
        out << "  \"enums\": [\n";
        for (size_t e = 0; e < enums.size(); ++e) {
            const auto& en = enums[e];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(en.name) << "\",\n";
            out << "      \"type\": \"" << escape_json(en.underlying_type) << "\",\n";
            out << "      \"underlying_type\": \"" << escape_json(en.underlying_type) << "\",\n";
            out << "      \"description\": \"" << escape_json(en.description) << "\",\n";
            out << "      \"literals\": [\n";
            for (size_t l = 0; l < en.literals.size(); ++l) {
                const auto& lit = en.literals[l];
                out << "        { \"name\": \"" << escape_json(lit.name) << "\"";
                if (lit.value.has_value()) {
                    out << ", \"value\": " << *lit.value;
                }
                if (!lit.description.empty()) {
                    out << ", \"description\": \"" << escape_json(lit.description) << "\"";
                }
                out << " }";
                if (l + 1 < en.literals.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "      ]\n";
            out << "    }";
            if (e + 1 < enums.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Structs
    const auto structs = ir.get_structs();
    if (!structs.empty()) {
        out << "  \"structs\": [\n";
        for (size_t st_i = 0; st_i < structs.size(); ++st_i) {
            const auto& st = structs[st_i];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(st.name) << "\",\n";
            out << "      \"is_datatype\": " << (st.is_datatype ? "true" : "false") << ",\n";
            out << "      \"description\": \"" << escape_json(st.description) << "\",\n";
            out << "      \"fields\": [\n";
            for (size_t f = 0; f < st.fields.size(); ++f) {
                const auto& field = st.fields[f];
                out << "        {\n";
                out << "          \"name\": \"" << escape_json(field.name) << "\",\n";
                out << "          \"type\": \"" << escape_json(field.type.to_canonical_string()) << "\",\n";
                out << "          \"default\": \"" << escape_json(field.default_value) << "\",\n";
                out << "          \"default_value\": \"" << escape_json(field.default_value) << "\"";
                if (field.physical_unit.has_value()) {
                    out << ",\n          \"physical_unit\": \"" << escape_json(*field.physical_unit) << "\"";
                }
                if (field.min_value.has_value()) {
                    out << ",\n          \"min_value\": " << *field.min_value;
                }
                if (field.max_value.has_value()) {
                    out << ",\n          \"max_value\": " << *field.max_value;
                }
                if (!field.description.empty()) {
                    out << ",\n          \"description\": \"" << escape_json(field.description) << "\"";
                }
                out << "\n        }";
                if (f + 1 < st.fields.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "      ]\n";
            out << "    }";
            if (st_i + 1 < structs.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Compound Types
    if (!ir.custom_types.empty()) {
        out << "  \"types\": [\n";
        for (size_t ti = 0; ti < ir.custom_types.size(); ++ti) {
            const auto& t = ir.custom_types[ti];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(t.name) << "\",\n";
            out << "      \"kind\": \"" << type_kind_to_string(t.kind) << "\",\n";
            out << "      \"underlying_type\": \"" << escape_json(t.underlying_type) << "\",\n";
            out << "      \"description\": \"" << escape_json(t.description) << "\",\n";
            out << "      \"is_datatype\": " << (t.is_datatype ? "true" : "false");
            if (t.external_ref.has_value()) {
                out << ",\n      \"external_ref\": { \"module\": \"" << escape_json(t.external_ref->module)
                    << "\", \"symbol\": \"" << escape_json(t.external_ref->symbol) << "\" }";
            }
            if (!t.literals.empty()) {
                out << ",\n      \"literals\": [\n";
                for (size_t li = 0; li < t.literals.size(); ++li) {
                    const auto& lit = t.literals[li];
                    out << "        { \"name\": \"" << escape_json(lit.name) << "\"";
                    if (lit.value.has_value()) {
                        out << ", \"value\": " << *lit.value;
                    }
                    if (!lit.description.empty()) {
                        out << ", \"description\": \"" << escape_json(lit.description) << "\"";
                    }
                    out << " }";
                    if (li + 1 < t.literals.size())
                        out << ",";
                    out << "\n";
                }
                out << "      ]";
            }
            if (!t.fields.empty()) {
                out << ",\n      \"fields\": [\n";
                for (size_t fi = 0; fi < t.fields.size(); ++fi) {
                    const auto& f = t.fields[fi];
                    out << "        { \"name\": \"" << escape_json(f.name) << "\", \"type\": \""
                        << escape_json(f.type.to_canonical_string()) << "\"";
                    if (!f.default_value.empty()) {
                        out << ", \"default\": \"" << escape_json(f.default_value) << "\", \"default_value\": \""
                            << escape_json(f.default_value) << "\"";
                    }
                    if (f.physical_unit.has_value()) {
                        out << ", \"unit\": \"" << escape_json(*f.physical_unit) << "\", \"physical_unit\": \""
                            << escape_json(*f.physical_unit) << "\"";
                    }
                    if (f.min_value.has_value()) {
                        out << ", \"min\": " << *f.min_value << ", \"min_value\": " << *f.min_value;
                    }
                    if (f.max_value.has_value()) {
                        out << ", \"max\": " << *f.max_value << ", \"max_value\": " << *f.max_value;
                    }
                    if (!f.description.empty()) {
                        out << ", \"description\": \"" << escape_json(f.description) << "\"";
                    }
                    out << " }";
                    if (fi + 1 < t.fields.size())
                        out << ",";
                    out << "\n";
                }
                out << "      ]";
            }
            out << "\n    }";
            if (ti + 1 < ir.custom_types.size())
                out << ",";
            out << "\n";
        }
        out << "  ],\n";
    }

    // Signals
    if (!ir.signals.empty()) {
        out << "  \"signals\": [\n";
        for (size_t s = 0; s < ir.signals.size(); ++s) {
            const auto& sig = ir.signals[s];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(sig.name) << "\"";
            if (!sig.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(sig.description) << "\"";
            }
            if (!sig.attributes.empty()) {
                out << ",\n      \"attributes\": [\n";
                for (size_t a = 0; a < sig.attributes.size(); ++a) {
                    const auto& attr = sig.attributes[a];
                    out << "        { \"name\": \"" << escape_json(attr.name) << "\", \"type\": \""
                        << escape_json(attr.type.to_canonical_string()) << "\"";
                    if (!attr.default_value.empty()) {
                        out << ", \"default\": \"" << escape_json(attr.default_value) << "\", \"default_value\": \""
                            << escape_json(attr.default_value) << "\"";
                    }
                    out << " }";
                    if (a + 1 < sig.attributes.size()) {
                        out << ",";
                    }
                    out << "\n";
                }
                out << "      ]";
            }
            if (!sig.validators.empty()) {
                out << ",\n      \"validators\": [";
                for (size_t v = 0; v < sig.validators.size(); ++v) {
                    if (v > 0)
                        out << ", ";
                    out << "\"" << escape_json(sig.validators[v]) << "\"";
                }
                out << "]";
            }
            out << "\n    }";
            if (s + 1 < ir.signals.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Guards
    if (!ir.guards.empty()) {
        out << "  \"guards\": [\n";
        for (size_t g = 0; g < ir.guards.size(); ++g) {
            const auto& grd = ir.guards[g];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(grd.name) << "\"";
            if (!grd.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(grd.description) << "\"";
            }
            if (grd.raw_expression.has_value()) {
                out << ",\n      \"raw_expression\": \"" << escape_json(*grd.raw_expression) << "\"";
            }
            if (grd.normalized_expression.has_value()) {
                out << ",\n      \"normalized_expression\": \"" << escape_json(*grd.normalized_expression) << "\"";
            }
            out << "\n    }";
            if (g + 1 < ir.guards.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // Typed Services (sys-math / HAL)
    if (!ir.services.empty()) {
        out << "  \"services\": [\n";
        for (size_t s = 0; s < ir.services.size(); ++s) {
            const auto& svc = ir.services[s];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(svc.name) << "\",\n";
            out << "      \"return_type\": \""
                << escape_json(svc.is_void_return() ? "void" : svc.return_type.to_canonical_string()) << "\",\n";
            out << "      \"pure\": " << (svc.is_pure ? "true" : "false") << ",\n";
            out << "      \"noexcept\": " << (svc.is_noexcept ? "true" : "false") << ",\n";
            out << "      \"params\": [";
            for (size_t pi = 0; pi < svc.parameters.size(); ++pi) {
                if (pi > 0)
                    out << ", ";
                out << "{\"name\": \"" << escape_json(svc.parameters[pi].name) << "\", \"type\": \""
                    << escape_json(svc.parameters[pi].type.to_canonical_string()) << "\"}";
            }
            out << "]";
            if (!svc.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(svc.description) << "\"";
            }
            out << "\n    }" << (s + 1 < ir.services.size() ? "," : "") << "\n";
        }
        out << "  ],\n";
    }

    // Clocks
    if (!ir.clocks.empty()) {
        out << "  \"clocks\": [\n";
        for (size_t c = 0; c < ir.clocks.size(); ++c) {
            const auto& clk = ir.clocks[c];
            out << "    {\n";
            out << "      \"name\": \"" << escape_json(clk.name) << "\",\n";
            out << "      \"id\": \"" << escape_json(clk.id) << "\",\n";
            out << "      \"resolution\": \"" << time_unit_to_string(clk.resolution) << "\",\n";
            out << "      \"initial_value\": 0";
            if (!clk.description.empty()) {
                out << ",\n      \"description\": \"" << escape_json(clk.description) << "\"";
            }
            out << "\n    }" << (c + 1 < ir.clocks.size() ? "," : "") << "\n";
        }
        out << "  ],\n";
    }

    // Choice Nodes
    if (!ir.choice_nodes.empty()) {
        out << "  \"choice_nodes\": [\n";
        for (size_t c = 0; c < ir.choice_nodes.size(); ++c) {
            const auto& cn = ir.choice_nodes[c];
            out << "    {\"name\": \"" << escape_json(cn.name) << "\"}" << (c + 1 < ir.choice_nodes.size() ? "," : "")
                << "\n";
        }
        out << "  ],\n";
    }

    // Properties
    if (!ir.properties.empty()) {
        out << "  \"properties\": [\n";
        for (size_t p = 0; p < ir.properties.size(); ++p) {
            const auto& prop = ir.properties[p];
            out << "    {\n";
            out << "      \"id\": \"" << escape_json(prop.id) << "\",\n";
            out << "      \"name\": \"" << escape_json(prop.name) << "\",\n";
            out << "      \"kind\": \"" << property_kind_to_string(prop.kind) << "\",\n";
            out << "      \"raw_formula\": \"" << escape_json(prop.raw_formula) << "\",\n";
            out << "      \"ltl\": \"" << escape_json(prop.raw_formula) << "\",\n";
            if (prop.ast.has_value()) {
                out << "      \"ast\": \"" << escape_json(prop.ast->to_string()) << "\",\n";
            }
            if (!prop.traceability_req.empty()) {
                out << "      \"traceability_req\": \"" << escape_json(prop.traceability_req) << "\",\n";
                out << "      \"req\": \"" << escape_json(prop.traceability_req) << "\",\n";
            }
            out << "      \"description\": \"" << escape_json(prop.description) << "\"\n";
            out << "    }";
            if (p + 1 < ir.properties.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
    }

    // States
    out << "  \"states\": {\n";
    std::vector<const StateNode*> top_states;
    for (const auto& s : ir.states) {
        if (s.parent_state.empty() && (!s.parent_id.has_value() || s.parent_id->empty())) {
            top_states.push_back(&s);
        }
    }
    for (size_t i = 0; i < top_states.size(); ++i) {
        emit_state(out, *top_states[i], ir, 4);
        if (i + 1 < top_states.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }";
    if (!ir.transitions.empty()) {
        out << ",\n  \"transitions\": [\n";
        for (size_t i = 0; i < ir.transitions.size(); ++i) {
            const auto& tr = ir.transitions[i];
            out << "    {\n";
            out << "      \"id\": \"" << escape_json(tr.id) << "\",\n";
            out << "      \"source\": \"" << escape_json(tr.source) << "\",\n";
            out << "      \"target\": \"" << escape_json(tr.target) << "\",\n";
            out << "      \"source_ids\": [";
            for (size_t s = 0; s < tr.source_ids.size(); ++s) {
                if (s > 0)
                    out << ", ";
                out << "\"" << escape_json(tr.source_ids[s]) << "\"";
            }
            out << "],\n";
            out << "      \"target_ids\": [";
            for (size_t t = 0; t < tr.target_ids.size(); ++t) {
                if (t > 0)
                    out << ", ";
                out << "\"" << escape_json(tr.target_ids[t]) << "\"";
            }
            out << "],\n";
            out << "      \"kind\": \"" << transition_edge_kind_to_string(tr.kind) << "\",\n";
            out << "      \"priority\": " << tr.priority << ",\n";
            out << "      \"event\": \"" << escape_json(tr.event) << "\",\n";
            out << "      \"trigger\": \"" << escape_json(tr.get_trigger_name()) << "\"";
            emit_transition_metadata(out, tr, "      ", true);
            if (!tr.transition_action.has_value() || tr.transition_action->assignments.empty()) {
                out << ",\n      \"assignments\": []\n";
            } else {
                out << "\n";
            }
            out << "    }" << (i + 1 < ir.transitions.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
    } else {
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

std::string FsmIrSerializer::compute_sha256(const FsmIr& ir) {
    FsmIr canonical = ir;
    canonical.canonicalize();
    return sha256_hex(serialize_json(canonical));
}

}  // namespace fsm::ir
