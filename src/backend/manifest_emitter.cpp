#include "fsm/backend/manifest_emitter.hpp"

#include <cstdio>
#include <set>
#include <sstream>

#include "fsm/ir/fsm_ir_serializer.hpp"

namespace fsm::backend {

namespace {

std::string escape_json(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

}  // namespace

std::string ManifestEmitter::emit_json(const ir::FsmIr& model, const ManifestArtifacts& artifacts,
                                       const ManifestVerificationSummary& verification) {
    ir::FsmIr canonical = model;
    canonical.canonicalize();

    const std::string ir_hash = ir::FsmIrSerializer::compute_sha256(canonical);

    std::set<std::string> reqs(canonical.satisfies_reqs.begin(), canonical.satisfies_reqs.end());
    for (const auto& st : canonical.states) {
        reqs.insert(st.traceability_reqs.begin(), st.traceability_reqs.end());
    }
    for (const auto& tr : canonical.transitions) {
        reqs.insert(tr.traceability_reqs.begin(), tr.traceability_reqs.end());
    }
    for (const auto& prop : canonical.properties) {
        if (!prop.traceability_req.empty()) {
            reqs.insert(prop.traceability_req);
        }
    }

    std::ostringstream out;
    out << "{\n";
    out << "  \"manifest_schema_version\": \"" << kManifestSchemaVersion << "\",\n";
    out << "  \"compiler_version\": \"0.13.0\",\n";
    out << "  \"fsm_name\": \"" << escape_json(canonical.name) << "\",\n";
    out << "  \"package\": \"" << escape_json(canonical.package) << "\",\n";
    out << "  \"ir_sha256\": \"" << ir_hash << "\",\n";
    out << "  \"initial_state\": \"" << escape_json(canonical.initial_state) << "\",\n";

    // Artifacts
    out << "  \"artifacts\": {\n";
    out << "    \"standalone\": " << (artifacts.standalone ? "true" : "false") << ",\n";
    out << "    \"ports_header\": \"" << escape_json(artifacts.ports_header) << "\",\n";
    out << "    \"fsm_header\": \"" << escape_json(artifacts.fsm_header) << "\",\n";
    out << "    \"c_header\": \"" << escape_json(artifacts.c_header) << "\",\n";
    out << "    \"c_source\": \"" << escape_json(artifacts.c_source) << "\"\n";
    out << "  },\n";

    // States
    out << "  \"states\": [";
    for (std::size_t i = 0; i < canonical.states.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "\"" << escape_json(canonical.states[i].name) << "\"";
    }
    out << "],\n";

    // Ports
    out << "  \"ports\": [\n";
    for (std::size_t i = 0; i < canonical.ports.size(); ++i) {
        const auto& p = canonical.ports[i];
        out << "    { \"name\": \"" << escape_json(p.name) << "\", \"direction\": \""
            << ir::port_direction_to_string(p.direction) << "\", \"type\": \""
            << escape_json(p.type.to_canonical_string()) << "\"";
        if (p.min_value.has_value()) {
            out << ", \"min\": " << *p.min_value;
        }
        if (p.max_value.has_value()) {
            out << ", \"max\": " << *p.max_value;
        }
        if (p.physical_unit.has_value() && !p.physical_unit->empty()) {
            out << ", \"unit\": \"" << escape_json(*p.physical_unit) << "\"";
        }
        out << " }";
        if (i + 1 < canonical.ports.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    // Registers
    out << "  \"registers\": [\n";
    for (std::size_t i = 0; i < canonical.variables.size(); ++i) {
        const auto& v = canonical.variables[i];
        out << "    { \"name\": \"" << escape_json(v.name) << "\", \"type\": \""
            << escape_json(v.type.to_canonical_string()) << "\", \"initial_value\": \"" << escape_json(v.initial_value)
            << "\"";
        if (v.min_value.has_value()) {
            out << ", \"min\": " << *v.min_value;
        }
        if (v.max_value.has_value()) {
            out << ", \"max\": " << *v.max_value;
        }
        if (v.physical_unit.has_value() && !v.physical_unit->empty()) {
            out << ", \"unit\": \"" << escape_json(*v.physical_unit) << "\"";
        }
        out << " }";
        if (i + 1 < canonical.variables.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    // Events
    out << "  \"events\": [\n";
    std::vector<const ir::SignalDefinition*> public_events;
    for (const auto& sig : canonical.signals) {
        if (sig.name != "anonymous_event" && sig.name != "completion_event") {
            public_events.push_back(&sig);
        }
    }
    for (std::size_t i = 0; i < public_events.size(); ++i) {
        const auto& sig = *public_events[i];
        out << "    { \"name\": \"" << escape_json(sig.name) << "\", \"attributes\": [";
        for (std::size_t a = 0; a < sig.attributes.size(); ++a) {
            if (a > 0) {
                out << ", ";
            }
            out << "{ \"name\": \"" << escape_json(sig.attributes[a].name) << "\", \"type\": \""
                << escape_json(sig.attributes[a].type.to_canonical_string()) << "\" }";
        }
        out << "] }";
        if (i + 1 < public_events.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    // Services
    out << "  \"services\": [\n";
    for (std::size_t i = 0; i < canonical.services.size(); ++i) {
        const auto& svc = canonical.services[i];
        out << "    { \"name\": \"" << escape_json(svc.name) << "\", \"return_type\": \""
            << (svc.is_void_return() ? "void" : escape_json(svc.return_type.to_canonical_string()))
            << "\", \"pure\": " << (svc.is_pure ? "true" : "false")
            << ", \"noexcept\": " << (svc.is_noexcept ? "true" : "false") << ", \"params\": [";
        for (std::size_t pi = 0; pi < svc.parameters.size(); ++pi) {
            if (pi > 0) {
                out << ", ";
            }
            out << "{ \"name\": \"" << escape_json(svc.parameters[pi].name) << "\", \"type\": \""
                << escape_json(svc.parameters[pi].type.to_canonical_string()) << "\" }";
        }
        out << "] }";
        if (i + 1 < canonical.services.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    // External Types
    out << "  \"external_types\": [\n";
    std::vector<const ir::TypeDefinition*> ext_types;
    for (const auto& td : canonical.custom_types) {
        if (td.is_external()) {
            ext_types.push_back(&td);
        }
    }
    for (std::size_t i = 0; i < ext_types.size(); ++i) {
        const auto& td = *ext_types[i];
        out << "    { \"name\": \"" << escape_json(td.name) << "\", \"kind\": \"" << ir::type_kind_to_string(td.kind)
            << "\", \"module\": \"" << escape_json(td.external_ref->module) << "\", \"symbol\": \""
            << escape_json(td.external_ref->symbol) << "\" }";
        if (i + 1 < ext_types.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    // Requirements
    out << "  \"requirements\": [";
    std::size_t r_idx = 0;
    for (const auto& req : reqs) {
        if (r_idx++ > 0) {
            out << ", ";
        }
        out << "\"" << escape_json(req) << "\"";
    }
    out << "],\n";

    // Compute real-time scheduling bounds (prefer caller-supplied pass results, fallback to IR attributes / static
    // bound)
    std::size_t effective_queue_bound = verification.max_event_queue_bound;
    if (effective_queue_bound == 0U) {
        if (auto it = canonical.attributes.find("static_event_queue_capacity"); it != canonical.attributes.end()) {
            try {
                effective_queue_bound = static_cast<std::size_t>(std::stoull(it->second));
            } catch (...) {
                effective_queue_bound = 0U;
            }
        }
    }
    if (effective_queue_bound == 0U) {
        std::size_t max_deferred = 0U;
        std::size_t max_emits = 0U;
        auto count_emits = [](const ir::ActionSignature& act) {
            std::size_t c = 0U;
            for (const auto& inst : act.instructions) {
                if (std::holds_alternative<ir::SignalEmitOp>(inst.op)) {
                    ++c;
                }
            }
            return c;
        };
        for (const auto& st : canonical.states) {
            if (st.deferred_events.size() > max_deferred) {
                max_deferred = st.deferred_events.size();
            }
            for (const auto& a : st.entry_actions) {
                max_emits = std::max(max_emits, count_emits(a));
            }
            for (const auto& a : st.exit_actions) {
                max_emits = std::max(max_emits, count_emits(a));
            }
        }
        for (const auto& tr : canonical.transitions) {
            if (tr.transition_action) {
                max_emits = std::max(max_emits, count_emits(*tr.transition_action));
            }
            if (tr.condition_action) {
                max_emits = std::max(max_emits, count_emits(*tr.condition_action));
            }
        }
        const std::size_t raw_bound = max_deferred + max_emits + 2U;
        effective_queue_bound = 2U;
        while (effective_queue_bound < raw_bound) {
            effective_queue_bound <<= 1U;
        }
    }

    std::size_t effective_wcet_microsteps = verification.wcet_max_microsteps;
    if (effective_wcet_microsteps == 0U && !canonical.transitions.empty()) {
        effective_wcet_microsteps = 1U;
    }

    // Scheduling metadata
    out << "  \"scheduling\": {\n";
    out << "    \"wcet_max_microsteps\": " << effective_wcet_microsteps << ",\n";
    out << "    \"max_event_queue_bound\": " << effective_queue_bound << ",\n";
    out << "    \"zeno_free\": " << (verification.zeno_free ? "true" : "false") << "\n";
    out << "  },\n";

    // Verification summary
    out << "  \"verification\": {\n";
    out << "    \"verified\": " << (verification.verified ? "true" : "false") << ",\n";
    out << "    \"errors\": " << verification.errors << ",\n";
    out << "    \"warnings\": " << verification.warnings << ",\n";
    out << "    \"properties_checked\": " << verification.properties_checked << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace fsm::backend
