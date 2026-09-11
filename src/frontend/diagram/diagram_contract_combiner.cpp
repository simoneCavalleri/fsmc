#include "fsm/frontend/diagram/diagram_contract_combiner.hpp"

#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::diagram {

bool DiagramContractCombiner::combine(ir::FsmIr& model, const CompanionManifest& manifest, std::string& error_msg) {
    (void)error_msg;

    // 1. Metadata override
    if (!manifest.package_name.empty()) {
        model.package = manifest.package_name;
    }
    if (!manifest.fsm_name.empty()) {
        model.name = manifest.fsm_name;
    }
    if (!manifest.initial_state.empty()) {
        model.initial_state = manifest.initial_state;
    }

    // 2. I/O Ports
    for (const auto& p : manifest.ports) {
        ir::PortDirection dir = ir::string_to_port_direction(p.direction);
        ir::PortDefinition port_def(p.name, p.type, dir, p.min_value, p.max_value, p.constraint);
        model.add_port(std::move(port_def));
    }

    // 3. Extended State Variables
    for (const auto& v : manifest.variables) {
        ir::VariableDefinition var;
        var.name = v.name;
        var.type = ir::DataType::from_string(v.type);
        var.initial_value = v.initial_value;
        if (!v.unit.empty()) {
            var.physical_unit = v.unit;
        }
        var.min_value = v.min_value;
        var.max_value = v.max_value;
        model.add_variable(std::move(var));
    }

    // 4. Signals
    for (const auto& s : manifest.signals) {
        ir::SignalDefinition sig;
        sig.name = s.name;
        for (const auto& a : s.attributes) {
            sig.attributes.emplace_back(a.name, ir::DataType::from_string(a.type), a.default_value);
        }
        model.add_signal(std::move(sig));
    }

    // 5. State Invariants
    for (const auto& [state_name, inv_str] : manifest.invariants) {
        if (auto* st = model.find_state_mut(state_name)) {
            st->time_invariant = inv_str;
        }
    }

    // 6. Verification Properties (LTL/CTL)
    for (const auto& prop : manifest.properties) {
        ir::FormalProperty fp;
        fp.name = prop.name;
        fp.raw_formula = prop.formula;
        fp.ast = directive::LtlPropertyParser::parse(prop.formula);
        fp.id = ir::compute_deterministic_id(prop.name + ":" + prop.formula);
        model.add_property(std::move(fp));
    }

    // 7. Actions
    for (const auto& act : manifest.actions) {
        model.add_action(act.name);
    }

    // 8. Traceability Requirements
    for (const auto& req : manifest.requirements) {
        model.satisfies_reqs.push_back(req);
    }

    return true;
}

}  // namespace fsm::frontend::diagram
