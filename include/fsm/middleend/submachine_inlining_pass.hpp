#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Target-Agnostic Middle-End Pass: Splicing and inlining of modular Submachine Statecharts.
 *
 * Allows resolving external submachine models (SubmachineRef) and inlining their sub-graphs
 * directly into the parent state, mapping EntryPoint and ExitPoint connection ports.
 */
class SubmachineInliningPass {
  public:
    using SubmachineResolver = std::function<const FsmIr*(const std::string& submachine_name)>;

    explicit SubmachineInliningPass(SubmachineResolver resolver = nullptr) : resolver_(std::move(resolver)) {}

    [[nodiscard]] static std::string name() { return "SubmachineInlining"; }
    [[nodiscard]] static std::string description() {
        return "Resolves and inlines modular submachine statecharts into parent composite states";
    }

    void set_resolver(SubmachineResolver resolver) { resolver_ = std::move(resolver); }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        if (!resolver_) {
            // If no resolver is supplied, pass completes with diagnostic notice if submachines exist
            for (const auto& s : ir.states) {
                if (s.submachine.has_value()) {
                    diag.report(Diagnostic::info("I_SUBMACHINE_MODULAR",
                                                 "State '" + s.name + "' references submachine '" +
                                                     s.submachine->fsm_name + "' in modular (un-inlined) mode."));
                }
            }
            return true;
        }

        std::vector<StateNode> inlined_states;
        std::vector<TransitionEdge> inlined_transitions;

        for (auto& st : ir.states) {
            if (!st.submachine.has_value()) {
                continue;
            }

            const FsmIr* sub_model = resolver_(st.submachine->fsm_name);
            if (sub_model == nullptr) {
                diag.report(Diagnostic::warning("W_SUBMACHINE_NOT_FOUND", "Could not resolve submachine model '" +
                                                                              st.submachine->fsm_name +
                                                                              "' for state '" + st.name + "'."));
                continue;
            }

            st.is_composite = true;
            st.kind = StateKind::Composite;

            // Map submachine states into parent
            std::map<std::string, std::string> name_map;
            for (const auto& sub_st : sub_model->states) {
                StateNode cloned = sub_st;
                std::string inlined_name = st.name + "_" + sub_st.name;
                name_map[sub_st.name] = inlined_name;

                cloned.name = inlined_name;
                cloned.fqn = st.fqn + "." + sub_st.name;
                cloned.id = compute_deterministic_id(cloned.fqn);
                cloned.parent_state = st.name;
                cloned.parent_id = st.id;
                inlined_states.push_back(std::move(cloned));
                st.children_ids.push_back(inlined_states.back().id);
            }

            // Map submachine transitions
            for (const auto& sub_t : sub_model->transitions) {
                TransitionEdge cloned_t = sub_t;
                cloned_t.source = name_map[sub_t.source];
                cloned_t.target = name_map[sub_t.target];
                cloned_t.source_id = cloned_t.source;
                cloned_t.target_id = cloned_t.target;
                cloned_t.id = compute_deterministic_id(cloned_t.source + "->" + cloned_t.target + ":" + cloned_t.event);
                inlined_transitions.push_back(std::move(cloned_t));
            }

            // Remap Entry and Exit Port Mappings
            for (const auto& port : st.submachine->port_mappings) {
                if (name_map.count(port.entry_point) != 0) {
                    st.initial_sub_state = name_map[port.entry_point];
                }
            }

            // Clear submachine ref now that it has been inlined
            st.submachine = std::nullopt;
        }

        // Append inlined entities
        for (auto& s : inlined_states) {
            ir.states.push_back(std::move(s));
        }
        for (auto& t : inlined_transitions) {
            ir.transitions.push_back(std::move(t));
        }

        return true;
    }

  private:
    SubmachineResolver resolver_;
};

}  // namespace fsm::codegen

namespace fsm {
using SubmachineInliningPass = ::fsm::codegen::SubmachineInliningPass;
}  // namespace fsm
