#include "fsm/middleend/passes/submachine_inlining_pass.hpp"

#include <map>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool SubmachineInliningPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    if (!resolver_) {
        // If no resolver is supplied, pass completes with diagnostic notice if submachines exist
        for (const auto& s : ir.states) {
            if (s.submachine.has_value()) {
                diag.report(Diagnostic::info("I_SUBMACHINE_MODULAR", "State '" + s.name + "' references submachine '" +
                                                                         s.submachine->fsm_name +
                                                                         "' in modular (un-inlined) mode."));
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
            diag.report(Diagnostic::warning(
                "W_SUBMACHINE_NOT_FOUND",
                "Could not resolve submachine model '" + st.submachine->fsm_name + "' for state '" + st.name + "'."));
            continue;
        }

        st.is_composite = true;
        st.kind = StateKind::Composite;

        // Map submachine states into parent
        std::map<std::string, std::string> name_map;
        std::map<std::string, std::string> id_map;
        std::map<std::string, std::string> name_to_id_map;

        for (const auto& sub_st : sub_model->states) {
            StateNode cloned = sub_st;
            std::string inlined_name = st.name + "_" + sub_st.name;
            name_map[sub_st.name] = inlined_name;

            cloned.name = inlined_name;
            cloned.fqn = st.fqn + "." + sub_st.name;
            cloned.id = compute_deterministic_id(cloned.fqn);
            cloned.parent_state = st.name;
            cloned.parent_id = st.id;

            id_map[sub_st.id] = cloned.id;
            id_map[sub_st.name] = cloned.id;
            name_to_id_map[inlined_name] = cloned.id;

            inlined_states.push_back(std::move(cloned));
            st.children_ids.push_back(inlined_states.back().id);
        }

        // Map choice nodes
        for (const auto& cn : sub_model->choice_nodes) {
            ChoiceNodeModel cloned_cn = cn;
            std::string inlined_cn_name = st.name + "_" + cn.name;
            name_map[cn.name] = inlined_cn_name;
            cloned_cn.name = inlined_cn_name;
            std::string cn_id = compute_deterministic_id(cloned_cn.name);
            id_map[cn.name] = cn_id;
            name_to_id_map[inlined_cn_name] = cn_id;
            for (auto& branch : cloned_cn.outgoing_branches) {
                branch.source = inlined_cn_name;
                branch.source_id = cn_id;
                if (name_map.count(branch.target)) {
                    branch.target = name_map[branch.target];
                } else {
                    branch.target = st.name + "_" + branch.target;
                }
                branch.target_id = compute_deterministic_id(branch.target);
            }
            ir.choice_nodes.push_back(std::move(cloned_cn));
        }

        // Map submachine variables
        for (const auto& var : sub_model->variables) {
            VariableDefinition cloned_var = var;
            cloned_var.name = st.name + "_" + var.name;
            if (ir.find_variable(cloned_var.name) == nullptr) {
                ir.variables.push_back(std::move(cloned_var));
            }
        }

        // Map submachine transitions
        for (const auto& sub_t : sub_model->transitions) {
            TransitionEdge cloned_t = sub_t;
            cloned_t.source = name_map.count(sub_t.source) ? name_map[sub_t.source] : (st.name + "_" + sub_t.source);
            cloned_t.target = name_map.count(sub_t.target) ? name_map[sub_t.target] : (st.name + "_" + sub_t.target);

            auto resolve_id = [&](const std::string& orig_name, const std::string& orig_id,
                                  const std::string& mapped_name) -> std::string {
                if (!orig_id.empty() && id_map.count(orig_id)) {
                    return id_map[orig_id];
                }
                if (!orig_name.empty() && id_map.count(orig_name)) {
                    return id_map[orig_name];
                }
                if (name_to_id_map.count(mapped_name)) {
                    return name_to_id_map[mapped_name];
                }
                return compute_deterministic_id(mapped_name);
            };

            cloned_t.source_id = resolve_id(sub_t.source, sub_t.source_id, cloned_t.source);
            cloned_t.target_id = resolve_id(sub_t.target, sub_t.target_id, cloned_t.target);

            cloned_t.source_ids.clear();
            cloned_t.multi_source_ids.clear();
            if (sub_t.source_ids.empty()) {
                cloned_t.source_ids.push_back(cloned_t.source);
                cloned_t.multi_source_ids.push_back(cloned_t.source_id);
            } else {
                for (const auto& s : sub_t.source_ids) {
                    std::string ms = name_map.count(s) ? name_map[s] : (st.name + "_" + s);
                    cloned_t.source_ids.push_back(ms);
                    cloned_t.multi_source_ids.push_back(resolve_id(s, "", ms));
                }
            }

            cloned_t.target_ids.clear();
            cloned_t.multi_target_ids.clear();
            if (sub_t.target_ids.empty()) {
                cloned_t.target_ids.push_back(cloned_t.target);
                cloned_t.multi_target_ids.push_back(cloned_t.target_id);
            } else {
                for (const auto& t : sub_t.target_ids) {
                    std::string mt = name_map.count(t) ? name_map[t] : (st.name + "_" + t);
                    cloned_t.target_ids.push_back(mt);
                    cloned_t.multi_target_ids.push_back(resolve_id(t, "", mt));
                }
            }

            cloned_t.id = compute_deterministic_id(cloned_t.source + "->" + cloned_t.target + ":" + cloned_t.event);
            inlined_transitions.push_back(std::move(cloned_t));
        }

        // Remap Entry and Exit Port Mappings
        for (const auto& port : st.submachine->port_mappings) {
            if (name_map.count(port.entry_point) != 0) {
                st.initial_sub_state = name_map[port.entry_point];
            }
        }
        if (st.initial_sub_state.empty()) {
            if (!sub_model->initial_state.empty() && name_map.count(sub_model->initial_state) != 0) {
                st.initial_sub_state = name_map[sub_model->initial_state];
            } else if (!sub_model->states.empty()) {
                st.initial_sub_state = name_map[sub_model->states.front().name];
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

}  // namespace fsm::middleend::passes
