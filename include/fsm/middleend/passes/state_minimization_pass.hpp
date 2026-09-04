#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Middle-End Optimization Pass: DFA State Minimization (Hopcroft/Moore Partitioning).
 *
 * Merges bisimilar/equivalent states that exhibit identical input/output transition
 * behaviors, entry/exit actions, and hierarchical parent contexts, significantly reducing
 * ROM lookup table size for embedded targets.
 */
class StateMinimizationPass {
  public:
    [[nodiscard]] static std::string name() { return "StateMinimization"; }
    [[nodiscard]] static std::string description() {
        return "Minimizes state count by merging behaviorally equivalent states";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        if (ir.states.size() <= 1) {
            return false;
        }

        std::string init_st = ir.initial_state_id.empty() ? ir.initial_state : ir.initial_state_id;
        if (init_st.empty() && !ir.states.empty()) {
            init_st = ir.states.front().name;
        }

        // Collect all distinct events
        std::set<std::string> alphabet;
        for (const auto& t : ir.transitions) {
            alphabet.insert(t.event);
        }

        // 1. Initial partition based on signature:
        // (kind, is_initial, parent_state, entry_actions, exit_actions)
        auto get_actions_sig = [](const std::vector<ActionSignature>& acts) -> std::string {
            std::string s;
            for (const auto& a : acts) {
                s += a.name + ";";
            }
            return s;
        };

        std::map<std::string, std::vector<std::string>> initial_blocks;
        for (const auto& s : ir.states) {
            if (ir.is_choice_node(s.name) || s.is_composite) {
                // Keep choice nodes and composite states in individual classes
                initial_blocks[s.name] = {s.name};
                continue;
            }

            bool is_init = (s.name == init_st || s.id == init_st);
            std::string key = std::to_string(static_cast<int>(s.kind)) + "|" + (is_init ? "INIT" : "NORM") + "|" +
                              s.parent_state + "|" + get_actions_sig(s.entry_actions) + "|" +
                              get_actions_sig(s.exit_actions);

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
                        std::string target_desc = "NONE";
                        for (const auto& t : ir.transitions) {
                            if (t.source == s && t.event == ev) {
                                auto it = state_to_block.find(t.target);
                                std::size_t b_idx = (it != state_to_block.end()) ? it->second : 999999;
                                target_desc =
                                    std::to_string(b_idx) + ":" + t.guard.value_or("") + ":" + t.action.value_or("");
                                break;
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

        // Update transitions: redirect sources and targets
        for (auto& t : ir.transitions) {
            if (remapping.count(t.source) > 0) {
                t.source = remapping[t.source];
            }
            if (remapping.count(t.target) > 0) {
                t.target = remapping[t.target];
            }
        }

        // Remove duplicate transitions
        std::vector<TransitionEdge> unique_transitions;
        for (const auto& t : ir.transitions) {
            bool exists = false;
            for (const auto& u : unique_transitions) {
                if (u.source == t.source && u.target == t.target && u.event == t.event && u.guard == t.guard &&
                    u.action == t.action) {
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
};

}  // namespace fsm::codegen
