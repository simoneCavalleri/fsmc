#include "fsm/backend/diagram/companion_manifest_emitter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace fsm::backend::diagram {

ir::CompanionManifest CompanionManifestEmitter::build_manifest(const ir::FsmIr& ir) {
    ir::CompanionManifest manifest;
    manifest.package_name = ir.package;
    manifest.fsm_name = ir.name;
    manifest.initial_state = ir.initial_state;

    // Custom Types (Enums, Structs, External Types)
    manifest.types.reserve(ir.custom_types.size());
    for (const auto& td : ir.custom_types) {
        ir::CompanionCustomType ct;
        ct.name = td.name;
        ct.kind = td.is_enum() ? "enum" : "struct";
        if (td.is_external()) {
            ct.external_module = td.external_ref->module;
            ct.external_symbol = td.external_ref->symbol;
        }
        for (const auto& lit : td.literals) {
            ct.enum_literals.push_back(lit.name);
        }
        for (const auto& f : td.fields) {
            ir::CompanionTypeField cf;
            cf.name = f.name;
            cf.type = f.type.to_canonical_string();
            ct.fields.push_back(std::move(cf));
        }
        manifest.types.push_back(std::move(ct));
    }

    // Services
    manifest.services.reserve(ir.services.size());
    for (const auto& svc : ir.services) {
        ir::CompanionService cs;
        cs.name = svc.name;
        cs.return_type = svc.return_type.to_canonical_string();
        cs.is_pure = svc.is_pure;
        cs.is_noexcept = svc.is_noexcept;
        cs.desc = svc.description;
        for (const auto& p : svc.parameters) {
            ir::CompanionServiceParam cp;
            cp.name = p.name;
            cp.type = p.type.to_canonical_string();
            cs.params.push_back(std::move(cp));
        }
        manifest.services.push_back(std::move(cs));
    }

    // Ports
    manifest.ports.reserve(ir.ports.size());
    for (const auto& p : ir.ports) {
        ir::CompanionPort cp;
        cp.name = p.name;
        cp.type = p.type.to_canonical_string();
        cp.direction = std::string(ir::port_direction_to_string(p.direction));
        cp.min_value = p.min_value;
        cp.max_value = p.max_value;
        cp.constraint = p.constraint;
        manifest.ports.push_back(std::move(cp));
    }

    // Variables
    manifest.variables.reserve(ir.variables.size());
    for (const auto& v : ir.variables) {
        ir::CompanionVariable cv;
        cv.name = v.name;
        cv.type = v.type.to_canonical_string();
        cv.initial_value = v.initial_value;
        if (v.physical_unit.has_value()) {
            cv.unit = *v.physical_unit;
        }
        if (v.min_value.has_value()) {
            cv.min_value = static_cast<double>(*v.min_value);
        }
        if (v.max_value.has_value()) {
            cv.max_value = static_cast<double>(*v.max_value);
        }
        manifest.variables.push_back(std::move(cv));
    }

    // Signals
    manifest.signals.reserve(ir.signals.size());
    for (const auto& s : ir.signals) {
        ir::CompanionSignal cs;
        cs.name = s.name;
        cs.attributes.reserve(s.attributes.size());
        for (const auto& a : s.attributes) {
            ir::CompanionSignalAttr ca;
            ca.name = a.name;
            ca.type = a.type.to_canonical_string();
            ca.default_value = a.default_value;
            cs.attributes.push_back(std::move(ca));
        }
        manifest.signals.push_back(std::move(cs));
    }

    // Invariants
    for (const auto& s : ir.states) {
        if (s.time_invariant.has_value() && !s.time_invariant->empty()) {
            manifest.invariants[s.name] = s.time_invariant->to_string();
        }
        for (const auto& inv : s.invariants) {
            std::string inv_str = inv.to_string();
            if (!inv_str.empty()) {
                auto it = manifest.invariants.find(s.name);
                if (it != manifest.invariants.end()) {
                    it->second += " && " + inv_str;
                } else {
                    manifest.invariants[s.name] = inv_str;
                }
            }
        }
    }

    // Properties
    manifest.properties.reserve(ir.properties.size());
    for (const auto& p : ir.properties) {
        ir::CompanionProperty cp;
        cp.name = p.name;
        cp.kind = ir::property_kind_to_string(p.kind);
        cp.formula = !p.raw_formula.empty() ? p.raw_formula : (p.ast.has_value() ? p.ast->to_string() : "");
        cp.req = p.traceability_req;
        cp.desc = p.description;
        manifest.properties.push_back(std::move(cp));
    }

    // Actions
    std::unordered_set<std::string> seen_actions;
    auto record_action = [&](const std::string& name, const std::string& inv) {
        if (name.empty())
            return;
        if (seen_actions.insert(name).second) {
            ir::CompanionAction ca;
            ca.name = name;
            ca.inv = inv;
            manifest.actions.push_back(std::move(ca));
        }
    };
    for (const auto& a : ir.actions) {
        record_action(a.name, "");
    }
    for (const auto& s : ir.states) {
        for (const auto& a : s.entry_actions)
            record_action(a.name, a.invocation);
        for (const auto& a : s.exit_actions)
            record_action(a.name, a.invocation);
    }
    for (const auto& t : ir.transitions) {
        if (t.condition_action.has_value()) {
            record_action(t.condition_action->name, t.condition_action->invocation);
        }
        if (t.transition_action.has_value()) {
            record_action(t.transition_action->name, t.transition_action->invocation);
        }
    }

    // Requirements
    manifest.requirements = ir.satisfies_reqs;
    auto record_req = [&](const std::string& req) {
        if (!req.empty() &&
            std::find(manifest.requirements.begin(), manifest.requirements.end(), req) == manifest.requirements.end()) {
            manifest.requirements.push_back(req);
        }
    };
    for (const auto& s : ir.states) {
        for (const auto& r : s.traceability_reqs)
            record_req(r);
    }
    for (const auto& t : ir.transitions) {
        for (const auto& r : t.traceability_reqs)
            record_req(r);
    }
    for (const auto& p : ir.properties) {
        record_req(p.traceability_req);
    }

    return manifest;
}

std::string CompanionManifestEmitter::emit_yaml(const ir::CompanionManifest& manifest) {
    std::ostringstream ss;
    ss << "# Generated by fsmc CompanionManifestEmitter (target-agnostic formal contract)\n";
    ss << "fsm:\n";
    if (!manifest.package_name.empty()) {
        ss << "  package: \"" << manifest.package_name << "\"\n";
    }
    ss << "  name: \"" << manifest.fsm_name << "\"\n";
    if (!manifest.initial_state.empty()) {
        ss << "  initial: \"" << manifest.initial_state << "\"\n";
    }

    if (!manifest.types.empty()) {
        ss << "\ntypes:\n";
        for (const auto& t : manifest.types) {
            ss << "  - name: \"" << t.name << "\"\n";
            ss << "    kind: \"" << t.kind << "\"\n";
            if (!t.external_module.empty()) {
                ss << "    external_module: \"" << t.external_module << "\"\n";
            }
            if (!t.external_symbol.empty()) {
                ss << "    external_symbol: \"" << t.external_symbol << "\"\n";
            }
            if (!t.enum_literals.empty()) {
                ss << "    literals: [";
                for (std::size_t i = 0; i < t.enum_literals.size(); ++i) {
                    if (i > 0) {
                        ss << ", ";
                    }
                    ss << "\"" << t.enum_literals[i] << "\"";
                }
                ss << "]\n";
            }
            if (!t.fields.empty()) {
                ss << "    fields:\n";
                for (const auto& f : t.fields) {
                    ss << "      - name: \"" << f.name << "\"\n";
                    ss << "        type: \"" << f.type << "\"\n";
                }
            }
        }
    }

    if (!manifest.services.empty()) {
        ss << "\nservices:\n";
        for (const auto& svc : manifest.services) {
            ss << "  - name: \"" << svc.name << "\"\n";
            ss << "    return_type: \"" << svc.return_type << "\"\n";
            if (svc.is_pure) {
                ss << "    pure: true\n";
            }
            if (!svc.is_noexcept) {
                ss << "    noexcept: false\n";
            }
            if (!svc.desc.empty()) {
                ss << "    desc: \"" << svc.desc << "\"\n";
            }
            if (!svc.params.empty()) {
                ss << "    params:\n";
                for (const auto& p : svc.params) {
                    ss << "      - name: \"" << p.name << "\"\n";
                    ss << "        type: \"" << p.type << "\"\n";
                }
            }
        }
    }

    if (!manifest.ports.empty()) {
        ss << "\nports:\n";
        for (const auto& p : manifest.ports) {
            ss << "  - name: \"" << p.name << "\"\n";
            ss << "    type: \"" << p.type << "\"\n";
            ss << "    direction: \"" << p.direction << "\"\n";
            if (p.min_value.has_value()) {
                ss << "    min: " << *p.min_value << "\n";
            }
            if (p.max_value.has_value()) {
                ss << "    max: " << *p.max_value << "\n";
            }
            if (!p.constraint.empty()) {
                ss << "    constraint: \"" << p.constraint << "\"\n";
            }
        }
    }

    if (!manifest.variables.empty()) {
        ss << "\nvariables:\n";
        for (const auto& v : manifest.variables) {
            ss << "  - name: \"" << v.name << "\"\n";
            ss << "    type: \"" << v.type << "\"\n";
            if (!v.initial_value.empty()) {
                ss << "    initial: \"" << v.initial_value << "\"\n";
            }
            if (!v.unit.empty()) {
                ss << "    unit: \"" << v.unit << "\"\n";
            }
            if (v.min_value.has_value()) {
                ss << "    min: " << *v.min_value << "\n";
            }
            if (v.max_value.has_value()) {
                ss << "    max: " << *v.max_value << "\n";
            }
        }
    }

    if (!manifest.signals.empty()) {
        ss << "\nsignals:\n";
        for (const auto& s : manifest.signals) {
            ss << "  - name: \"" << s.name << "\"\n";
            if (!s.attributes.empty()) {
                ss << "    attributes:\n";
                for (const auto& a : s.attributes) {
                    ss << "      - name: \"" << a.name << "\"\n";
                    ss << "        type: \"" << a.type << "\"\n";
                    if (!a.default_value.empty()) {
                        ss << "        default: \"" << a.default_value << "\"\n";
                    }
                }
            }
        }
    }

    if (!manifest.invariants.empty()) {
        ss << "\ninvariants:\n";
        for (const auto& [state, inv] : manifest.invariants) {
            ss << "  " << state << ": \"" << inv << "\"\n";
        }
    }

    if (!manifest.properties.empty()) {
        ss << "\nproperties:\n";
        for (const auto& prop : manifest.properties) {
            ss << "  - name: \"" << prop.name << "\"\n";
            if (!prop.kind.empty()) {
                ss << "    kind: \"" << prop.kind << "\"\n";
            }
            ss << "    formula: \"" << prop.formula << "\"\n";
            if (!prop.req.empty()) {
                ss << "    req: \"" << prop.req << "\"\n";
            }
            if (!prop.desc.empty()) {
                ss << "    desc: \"" << prop.desc << "\"\n";
            }
        }
    }

    if (!manifest.actions.empty()) {
        ss << "\nactions:\n";
        for (const auto& act : manifest.actions) {
            ss << "  - name: \"" << act.name << "\"\n";
            if (!act.signature.empty()) {
                ss << "    signature: \"" << act.signature << "\"\n";
            }
            if (!act.inv.empty()) {
                ss << "    inv: \"" << act.inv << "\"\n";
            }
        }
    }

    if (!manifest.requirements.empty()) {
        ss << "\nrequirements:\n";
        for (const auto& req : manifest.requirements) {
            ss << "  - \"" << req << "\"\n";
        }
    }

    return ss.str();
}

std::string CompanionManifestEmitter::emit_json(const ir::CompanionManifest& manifest) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"fsm\": {\n";
    ss << "    \"package\": \"" << manifest.package_name << "\",\n";
    ss << "    \"name\": \"" << manifest.fsm_name << "\",\n";
    ss << "    \"initial\": \"" << manifest.initial_state << "\"\n";
    ss << "  },\n";

    // types
    if (!manifest.types.empty()) {
        ss << "  \"types\": [\n";
        for (size_t i = 0; i < manifest.types.size(); ++i) {
            const auto& t = manifest.types[i];
            ss << "    {\n";
            ss << "      \"name\": \"" << t.name << "\",\n";
            ss << "      \"kind\": \"" << t.kind << "\"";
            if (!t.external_module.empty()) {
                ss << ",\n      \"external_module\": \"" << t.external_module << "\"";
            }
            if (!t.external_symbol.empty()) {
                ss << ",\n      \"external_symbol\": \"" << t.external_symbol << "\"";
            }
            if (!t.enum_literals.empty()) {
                ss << ",\n      \"literals\": [";
                for (size_t li = 0; li < t.enum_literals.size(); ++li) {
                    if (li > 0) {
                        ss << ", ";
                    }
                    ss << "\"" << t.enum_literals[li] << "\"";
                }
                ss << "]";
            }
            if (!t.fields.empty()) {
                ss << ",\n      \"fields\": [\n";
                for (size_t fi = 0; fi < t.fields.size(); ++fi) {
                    ss << "        {\"name\": \"" << t.fields[fi].name << "\", \"type\": \"" << t.fields[fi].type
                       << "\"}" << (fi + 1 < t.fields.size() ? "," : "") << "\n";
                }
                ss << "      ]";
            }
            ss << "\n    }" << (i + 1 < manifest.types.size() ? "," : "") << "\n";
        }
        ss << "  ],\n";
    }

    // services
    if (!manifest.services.empty()) {
        ss << "  \"services\": [\n";
        for (size_t i = 0; i < manifest.services.size(); ++i) {
            const auto& svc = manifest.services[i];
            ss << "    {\n";
            ss << "      \"name\": \"" << svc.name << "\",\n";
            ss << "      \"return_type\": \"" << svc.return_type << "\",\n";
            ss << "      \"pure\": " << (svc.is_pure ? "true" : "false") << ",\n";
            ss << "      \"noexcept\": " << (svc.is_noexcept ? "true" : "false");
            if (!svc.params.empty()) {
                ss << ",\n      \"params\": [\n";
                for (size_t pi = 0; pi < svc.params.size(); ++pi) {
                    ss << "        {\"name\": \"" << svc.params[pi].name << "\", \"type\": \"" << svc.params[pi].type
                       << "\"}" << (pi + 1 < svc.params.size() ? "," : "") << "\n";
                }
                ss << "      ]";
            }
            ss << "\n    }" << (i + 1 < manifest.services.size() ? "," : "") << "\n";
        }
        ss << "  ],\n";
    }

    // ports
    ss << "  \"ports\": [\n";
    for (size_t i = 0; i < manifest.ports.size(); ++i) {
        const auto& p = manifest.ports[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << p.name << "\",\n";
        ss << "      \"type\": \"" << p.type << "\",\n";
        ss << "      \"direction\": \"" << p.direction << "\"";
        if (p.min_value.has_value()) {
            ss << ",\n      \"min\": " << *p.min_value;
        }
        if (p.max_value.has_value()) {
            ss << ",\n      \"max\": " << *p.max_value;
        }
        if (!p.constraint.empty()) {
            ss << ",\n      \"constraint\": \"" << p.constraint << "\"";
        }
        ss << "\n    }" << (i + 1 < manifest.ports.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // variables
    ss << "  \"variables\": [\n";
    for (size_t i = 0; i < manifest.variables.size(); ++i) {
        const auto& v = manifest.variables[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << v.name << "\",\n";
        ss << "      \"type\": \"" << v.type << "\"";
        if (!v.initial_value.empty()) {
            ss << ",\n      \"initial\": \"" << v.initial_value << "\"";
        }
        if (!v.unit.empty()) {
            ss << ",\n      \"unit\": \"" << v.unit << "\"";
        }
        if (v.min_value.has_value()) {
            ss << ",\n      \"min\": " << *v.min_value;
        }
        if (v.max_value.has_value()) {
            ss << ",\n      \"max\": " << *v.max_value;
        }
        ss << "\n    }" << (i + 1 < manifest.variables.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // signals
    ss << "  \"signals\": [\n";
    for (size_t i = 0; i < manifest.signals.size(); ++i) {
        const auto& s = manifest.signals[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << s.name << "\",\n";
        ss << "      \"attributes\": [\n";
        for (size_t j = 0; j < s.attributes.size(); ++j) {
            const auto& a = s.attributes[j];
            ss << "        {\n";
            ss << "          \"name\": \"" << a.name << "\",\n";
            ss << "          \"type\": \"" << a.type << "\"";
            if (!a.default_value.empty()) {
                ss << ",\n          \"default\": \"" << a.default_value << "\"";
            }
            ss << "\n        }" << (j + 1 < s.attributes.size() ? "," : "") << "\n";
        }
        ss << "      ]\n";
        ss << "    }" << (i + 1 < manifest.signals.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // invariants
    ss << "  \"invariants\": {\n";
    size_t inv_idx = 0;
    for (const auto& [state, inv] : manifest.invariants) {
        ss << "    \"" << state << "\": \"" << inv << "\"" << (++inv_idx < manifest.invariants.size() ? "," : "")
           << "\n";
    }
    ss << "  },\n";

    // properties
    ss << "  \"properties\": [\n";
    for (size_t i = 0; i < manifest.properties.size(); ++i) {
        const auto& prop = manifest.properties[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << prop.name << "\",\n";
        if (!prop.kind.empty()) {
            ss << "      \"kind\": \"" << prop.kind << "\",\n";
        }
        ss << "      \"formula\": \"" << prop.formula << "\"";
        if (!prop.req.empty()) {
            ss << ",\n      \"req\": \"" << prop.req << "\"";
        }
        if (!prop.desc.empty()) {
            ss << ",\n      \"desc\": \"" << prop.desc << "\"";
        }
        ss << "\n    }" << (i + 1 < manifest.properties.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // actions
    ss << "  \"actions\": [\n";
    for (size_t i = 0; i < manifest.actions.size(); ++i) {
        const auto& act = manifest.actions[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << act.name << "\"";
        if (!act.signature.empty()) {
            ss << ",\n      \"signature\": \"" << act.signature << "\"";
        }
        if (!act.inv.empty()) {
            ss << ",\n      \"inv\": \"" << act.inv << "\"";
        }
        ss << "\n    }" << (i + 1 < manifest.actions.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // requirements
    ss << "  \"requirements\": [\n";
    for (size_t i = 0; i < manifest.requirements.size(); ++i) {
        ss << "    \"" << manifest.requirements[i] << "\"" << (i + 1 < manifest.requirements.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

}  // namespace fsm::backend::diagram
