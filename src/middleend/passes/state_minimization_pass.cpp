#include "fsm/middleend/passes/state_minimization_pass.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool StateMinimizationPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    if (ir.states.size() <= 1) {
        return false;
    }

    std::string init_st = ir.initial_state;
    if (init_st.empty()) {
        if (const auto* s = ir.find_state_by_id(ir.initial_state_id)) {
            init_st = s->name;
        } else {
            init_st = ir.initial_state_id;
        }
    }
    if (init_st.empty() && !ir.states.empty()) {
        init_st = ir.states.front().name;
    }

    // Collect all distinct trigger names / events
    std::set<std::string> alphabet;
    for (const auto& t : ir.transitions) {
        alphabet.insert(t.get_trigger_name());
    }

    auto get_single_action_sig = [](const ActionSignature& a) -> std::string {
        std::string s = a.name + ":" + a.invocation + ":" + (a.accepts_event ? "1" : "0") + "{";
        for (const auto& asgn : a.assignments) {
            s +=
                asgn.target.full_path() + "=" + std::to_string(static_cast<int>(asgn.op)) + ":" + asgn.expression + ",";
        }
        for (const auto& inst : a.instructions) {
            if (const auto* st = std::get_if<StoreOp>(&inst.op)) {
                s += "S:" + st->target.full_path() + ":" + std::to_string(static_cast<int>(st->op)) + ":" +
                     st->expression + ",";
            } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                s += "PW:" + pw->port_name + ":" + pw->expression + ",";
            } else if (const auto* pr = std::get_if<PortReadOp>(&inst.op)) {
                s += "PR:" + pr->port_name + ":" + pr->destination.full_path() + ",";
            } else if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
                s += "SE:" + se->signal_name + "(";
                for (const auto& arg : se->arguments)
                    s += arg + ",";
                s += "),";
            } else if (const auto* ac = std::get_if<ActionCallOp>(&inst.op)) {
                s += "AC:" + ac->function_name + "(";
                for (const auto& arg : ac->arguments)
                    s += arg + ",";
                s += "),";
            }
        }
        s += "}";
        return s;
    };

    // 1. Initial partition based on signature:
    // (kind, is_initial, parent_state, entry_actions, exit_actions, invariants, time_invariant, etc.)
    auto get_actions_sig = [&](const std::vector<ActionSignature>& acts) -> std::string {
        std::string s;
        for (const auto& a : acts) {
            s += get_single_action_sig(a) + ";";
        }
        return s;
    };

    auto get_deferred_sig = [](const std::vector<std::string>& evs) -> std::string {
        std::string s;
        for (const auto& e : evs) {
            s += e + ";";
        }
        return s;
    };

    auto get_invariants_sig = [](const std::vector<GuardAstNode>& invs) -> std::string {
        std::string s;
        for (const auto& inv : invs) {
            s += inv.to_string() + ";";
        }
        return s;
    };

    std::map<std::string, std::vector<std::string>> initial_blocks;
    for (const auto& s : ir.states) {
        if (ir.is_choice_node(s.name) || s.is_composite || !s.children_ids.empty()) {
            // Keep choice nodes and composite states in individual classes
            initial_blocks["__unique_" + s.name] = {s.name};
            continue;
        }

        bool is_init = (s.name == init_st || s.id == init_st);
        std::string key = std::to_string(static_cast<int>(s.kind)) + "|" + (is_init ? "INIT" : "NORM") + "|" +
                          s.parent_state + "|" + get_actions_sig(s.entry_actions) + "|" +
                          get_actions_sig(s.exit_actions) + "|" + s.do_activity.value_or("") + "|" +
                          (s.time_invariant.has_value() ? s.time_invariant->to_string() : "") + "|" +
                          get_invariants_sig(s.invariants) + "|" + (s.has_history ? "H" : "") + "|" +
                          (s.has_deep_history ? "DH" : "") + "|" + get_deferred_sig(s.deferred_events);

        initial_blocks[key].push_back(s.name);
    }

    std::vector<std::vector<std::string>> partition;
    for (auto& [_, block] : initial_blocks) {
        partition.push_back(std::move(block));
    }

    // Map state to partition block index
    std::unordered_map<std::string, std::size_t> state_to_block;
    auto update_map = [&]() {
        state_to_block.clear();
        for (std::size_t i = 0; i < partition.size(); ++i) {
            for (const auto& s : partition[i]) {
                state_to_block[s] = i;
            }
        }
    };
    update_map();

    // 2. Iterative partition refinement
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<std::vector<std::string>> new_partition;

        for (const auto& block : partition) {
            if (block.size() <= 1) {
                new_partition.push_back(block);
                continue;
            }

            // Sub-partition block based on transition targets across all alphabet symbols
            std::map<std::string, std::vector<std::string>> sub_blocks;
            for (const auto& s : block) {
                std::string sig;
                for (const auto& ev : alphabet) {
                    std::vector<std::string> t_descs;
                    for (const auto& t : ir.transitions) {
                        if (t.source == s && t.get_trigger_name() == ev) {
                            auto it = state_to_block.find(t.target);
                            std::size_t b_idx = (it != state_to_block.end()) ? it->second : 999999;
                            std::string cond_act =
                                t.condition_action.has_value() ? get_single_action_sig(*t.condition_action) : "";
                            std::string trans_act =
                                t.transition_action.has_value() ? get_single_action_sig(*t.transition_action) : "";
                            std::string resets;
                            for (const auto& cr : t.clock_resets) {
                                resets += cr + ",";
                            }
                            std::string desc = std::to_string(b_idx) + ":" + std::to_string(static_cast<int>(t.kind)) +
                                               ":" + std::to_string(t.priority) + ":" + resets + ":" +
                                               t.guard.value_or("") + ":" + cond_act + "/" + trans_act;
                            t_descs.push_back(std::move(desc));
                        }
                    }
                    std::sort(t_descs.begin(), t_descs.end());
                    std::string target_desc;
                    if (t_descs.empty()) {
                        target_desc = "NONE";
                    } else {
                        for (std::size_t i = 0; i < t_descs.size(); ++i) {
                            if (i > 0) {
                                target_desc += ",";
                            }
                            target_desc += t_descs[i];
                        }
                    }
                    sig += ev + "->" + target_desc + "|";
                }
                sub_blocks[sig].push_back(s);
            }

            if (sub_blocks.size() > 1) {
                changed = true;
            }

            for (auto& [_, sb] : sub_blocks) {
                new_partition.push_back(std::move(sb));
            }
        }

        partition = std::move(new_partition);
        update_map();
    }

    // 3. Merge equivalent states
    std::unordered_map<std::string, std::string> remapping;
    std::size_t merged_count = 0;

    for (const auto& block : partition) {
        if (block.size() <= 1)
            continue;

        // Pick first lexicographically as representative
        std::string canonical = block.front();
        for (std::size_t i = 1; i < block.size(); ++i) {
            remapping[block[i]] = canonical;
            merged_count++;
        }
    }

    if (remapping.empty()) {
        return false;
    }

    // Map state IDs as well
    std::unordered_map<std::string, std::string> id_remapping;
    for (const auto& [old_name, new_name] : remapping) {
        const auto* old_st = ir.find_state(old_name);
        const auto* new_st = ir.find_state(new_name);
        std::string new_id =
            (new_st != nullptr && !new_st->id.empty()) ? new_st->id : compute_deterministic_id(new_name);
        if (old_st != nullptr && !old_st->id.empty()) {
            id_remapping[old_st->id] = new_id;
        }
        id_remapping[compute_deterministic_id(old_name)] = new_id;
    }

    if (remapping.count(ir.initial_state) > 0) {
        ir.initial_state = remapping[ir.initial_state];
    }
    if (id_remapping.count(ir.initial_state_id) > 0) {
        ir.initial_state_id = id_remapping[ir.initial_state_id];
    } else if (remapping.count(ir.initial_state_id) > 0) {
        ir.initial_state_id = remapping[ir.initial_state_id];
    }

    for (auto& s : ir.states) {
        if (remapping.count(s.initial_sub_state) > 0) {
            s.initial_sub_state = remapping[s.initial_sub_state];
        }
        if (remapping.count(s.parent_state) > 0) {
            s.parent_state = remapping[s.parent_state];
            s.parent_id = compute_deterministic_id(s.parent_state);
        }
    }

    // Update transitions: redirect sources and targets
    for (auto& t : ir.transitions) {
        if (remapping.count(t.source) > 0) {
            t.source = remapping[t.source];
            t.source_id = compute_deterministic_id(t.source);
        } else if (id_remapping.count(t.source_id) > 0) {
            t.source_id = id_remapping[t.source_id];
        }
        for (auto& sid : t.source_ids) {
            if (remapping.count(sid) > 0) {
                sid = remapping[sid];
            }
        }
        for (auto& msid : t.multi_source_ids) {
            if (id_remapping.count(msid) > 0) {
                msid = id_remapping[msid];
            }
        }

        if (remapping.count(t.target) > 0) {
            t.target = remapping[t.target];
            t.target_id = compute_deterministic_id(t.target);
        } else if (id_remapping.count(t.target_id) > 0) {
            t.target_id = id_remapping[t.target_id];
        }
        for (auto& tid : t.target_ids) {
            if (remapping.count(tid) > 0) {
                tid = remapping[tid];
            }
        }
        for (auto& mtid : t.multi_target_ids) {
            if (id_remapping.count(mtid) > 0) {
                mtid = id_remapping[mtid];
            }
        }
    }

    // Remove duplicate transitions
    std::vector<TransitionEdge> unique_transitions;
    for (const auto& t : ir.transitions) {
        bool exists = false;
        for (const auto& u : unique_transitions) {
            if (u.source == t.source && u.target == t.target && u.get_trigger_name() == t.get_trigger_name() &&
                u.guard == t.guard && u.kind == t.kind && u.priority == t.priority &&
                u.clock_resets == t.clock_resets && u.condition_action == t.condition_action &&
                u.transition_action == t.transition_action) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            unique_transitions.push_back(t);
        }
    }
    ir.transitions = std::move(unique_transitions);

    // Remove merged states from IR
    ir.states.erase(std::remove_if(ir.states.begin(), ir.states.end(),
                                   [&](const StateNode& s) { return remapping.count(s.name) > 0; }),
                    ir.states.end());

    ir.canonicalize();

    diag.report(Diagnostic::info(
        "I_STATE_MIN", "State minimization merged " + std::to_string(merged_count) + " equivalent state(s)."));
    return true;
}

}  // namespace fsm::middleend::passes
