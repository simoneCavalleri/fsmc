/**
 * @file dead_action_elimination_pass.cpp
 * @brief Implementation of DeadActionEliminationPass.
 */

#include "fsm/middleend/passes/dead_action_elimination_pass.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace fsm::middleend::passes {

namespace {

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::unordered_set<std::string> extract_identifiers(std::string_view text) {
    std::unordered_set<std::string> idents;
    std::string current;
    for (char c : text) {
        if (is_ident_char(c)) {
            current.push_back(c);
        } else {
            if (!current.empty() && !std::isdigit(static_cast<unsigned char>(current[0]))) {
                idents.insert(current);
            }
            current.clear();
        }
    }
    if (!current.empty() && !std::isdigit(static_cast<unsigned char>(current[0]))) {
        idents.insert(current);
    }
    return idents;
}

bool text_contains_var(std::string_view text, std::string_view var) {
    auto idents = extract_identifiers(text);
    return idents.find(std::string(var)) != idents.end();
}

bool action_reads_var(const ir::ActionAstNode& node, std::string_view var) {
    if (std::holds_alternative<ir::StoreOp>(node.op)) {
        const auto& op = std::get<ir::StoreOp>(node.op);
        if (op.op != ir::AssignmentOp::Assign && op.target.name == var) {
            return true;  // e.g. x += 1 reads x
        }
        return text_contains_var(op.expression, var);
    }
    if (std::holds_alternative<ir::PortWriteOp>(node.op)) {
        return text_contains_var(std::get<ir::PortWriteOp>(node.op).expression, var);
    }
    if (std::holds_alternative<ir::SignalEmitOp>(node.op)) {
        for (const auto& arg : std::get<ir::SignalEmitOp>(node.op).arguments) {
            if (text_contains_var(arg, var))
                return true;
        }
    }
    if (std::holds_alternative<ir::ActionCallOp>(node.op)) {
        const auto& op = std::get<ir::ActionCallOp>(node.op);
        for (const auto& r : op.read_set) {
            if (r == var)
                return true;
        }
        for (const auto& arg : op.arguments) {
            if (text_contains_var(arg, var))
                return true;
        }
    }
    return false;
}

bool assignment_reads_var(const ir::ActionAssignment& assign, std::string_view var) {
    if (assign.op != ir::AssignmentOp::Assign && assign.target.name == var) {
        return true;
    }
    return text_contains_var(assign.expression, var);
}

bool store_overwrites_target(const ir::LValueTarget& later, const ir::LValueTarget& earlier) {
    if (later.name != earlier.name) {
        return false;
    }
    if (later == earlier) {
        return true;
    }
    // Overwriting the entire object overwrites all members and indices
    if (later.member_path.empty() && !later.constant_index.has_value()) {
        return true;
    }
    // Overwriting a parent prefix overwrites nested members
    if (!later.constant_index.has_value() && !earlier.member_path.empty() &&
        later.member_path.size() < earlier.member_path.size()) {
        bool prefix_match = true;
        for (std::size_t k = 0; k < later.member_path.size(); ++k) {
            if (later.member_path[k] != earlier.member_path[k]) {
                prefix_match = false;
                break;
            }
        }
        if (prefix_match) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool DeadActionEliminationPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    std::unordered_set<std::string> known_vars;
    for (const auto& v : ir.variables) {
        known_vars.insert(v.name);
    }

    // 1. Collect all variable reads across model (guards, invariants, clocks, actions)
    std::unordered_set<std::string> read_vars;

    for (const auto& state : ir.states) {
        for (const auto& inv : state.invariants) {
            for (const auto& ident : extract_identifiers(inv.to_string())) {
                if (known_vars.count(ident))
                    read_vars.insert(ident);
            }
        }
    }

    for (const auto& edge : ir.transitions) {
        if (edge.guard_ast) {
            for (const auto& ident : extract_identifiers(edge.guard_ast->to_string())) {
                if (known_vars.count(ident))
                    read_vars.insert(ident);
            }
        } else if (edge.guard) {
            for (const auto& ident : extract_identifiers(*edge.guard)) {
                if (known_vars.count(ident))
                    read_vars.insert(ident);
            }
        }
        if (std::holds_alternative<ir::ChangeTrigger>(edge.trigger)) {
            const auto& ct = std::get<ir::ChangeTrigger>(edge.trigger);
            if (ct.predicate) {
                for (const auto& ident : extract_identifiers(ct.predicate->to_string())) {
                    if (known_vars.count(ident))
                        read_vars.insert(ident);
                }
            }
        }
    }

    auto collect_action_reads = [&](const ir::ActionSignature& act) {
        for (const auto& inst : act.instructions) {
            for (const auto& v : known_vars) {
                if (action_reads_var(inst, v)) {
                    read_vars.insert(v);
                }
            }
        }
        for (const auto& assign : act.assignments) {
            for (const auto& ident : extract_identifiers(assign.expression)) {
                if (known_vars.count(ident))
                    read_vars.insert(ident);
            }
            if (assign.op != ir::AssignmentOp::Assign && known_vars.count(assign.target.name)) {
                read_vars.insert(assign.target.name);
            }
        }
    };

    for (const auto& state : ir.states) {
        for (const auto& act : state.entry_actions)
            collect_action_reads(act);
        for (const auto& act : state.exit_actions)
            collect_action_reads(act);
    }
    for (const auto& edge : ir.transitions) {
        if (edge.transition_action)
            collect_action_reads(*edge.transition_action);
        if (edge.condition_action)
            collect_action_reads(*edge.condition_action);
    }

    // 2. Eliminate dead stores
    std::size_t eliminated_count = 0;

    auto optimize_action = [&](ir::ActionSignature& act) {
        // Optimize instructions
        if (!act.instructions.empty()) {
            std::vector<ir::ActionAstNode> live_insts;
            live_insts.reserve(act.instructions.size());

            for (std::size_t i = 0; i < act.instructions.size(); ++i) {
                const auto& inst = act.instructions[i];

                if (std::holds_alternative<ir::StoreOp>(inst.op)) {
                    const auto& store = std::get<ir::StoreOp>(inst.op);
                    const std::string& var_name = store.target.name;
                    std::string full_path = store.target.full_path();

                    // Case A: Identity assignment (e.g. x = x or battery.soc = battery.soc)
                    if (store.op == ir::AssignmentOp::Assign &&
                        (store.expression == var_name || store.expression == full_path)) {
                        ++eliminated_count;
                        continue;
                    }

                    // Case B: Variable is never read anywhere in the entire model
                    if (known_vars.count(var_name) && read_vars.find(var_name) == read_vars.end()) {
                        ++eliminated_count;
                        continue;
                    }

                    // Case C: Overwritten before being read within the same action block
                    if (store.op == ir::AssignmentOp::Assign) {
                        bool overwritten_before_read = false;
                        for (std::size_t j = i + 1; j < act.instructions.size(); ++j) {
                            if (action_reads_var(act.instructions[j], var_name)) {
                                break;  // Read detected, so current store is alive!
                            }
                            if (std::holds_alternative<ir::StoreOp>(act.instructions[j].op)) {
                                const auto& later_store = std::get<ir::StoreOp>(act.instructions[j].op);
                                if (later_store.op == ir::AssignmentOp::Assign &&
                                    store_overwrites_target(later_store.target, store.target)) {
                                    overwritten_before_read = true;
                                    break;
                                }
                            }
                        }
                        if (overwritten_before_read) {
                            for (const auto& asgn : act.assignments) {
                                if (assignment_reads_var(asgn, var_name)) {
                                    overwritten_before_read = false;
                                    break;
                                }
                            }
                        }
                        if (overwritten_before_read) {
                            ++eliminated_count;
                            continue;
                        }
                    }
                }

                live_insts.push_back(inst);
            }

            act.instructions = std::move(live_insts);
        }

        // Optimize assignments
        if (!act.assignments.empty()) {
            std::vector<ir::ActionAssignment> live_assigns;
            live_assigns.reserve(act.assignments.size());

            for (std::size_t i = 0; i < act.assignments.size(); ++i) {
                const auto& assign = act.assignments[i];
                const std::string& var_name = assign.target.name;
                std::string full_path = assign.target.full_path();

                // Case A: Identity assignment (e.g. x = x or battery.soc = battery.soc)
                if (assign.op == ir::AssignmentOp::Assign &&
                    (assign.expression == var_name || assign.expression == full_path)) {
                    ++eliminated_count;
                    continue;
                }

                // Case B: Variable is never read anywhere in the entire model
                if (known_vars.count(var_name) && read_vars.find(var_name) == read_vars.end()) {
                    ++eliminated_count;
                    continue;
                }

                // Case C: Overwritten before being read within the same assignment sequence
                if (assign.op == ir::AssignmentOp::Assign) {
                    bool overwritten_before_read = false;
                    for (std::size_t j = i + 1; j < act.assignments.size(); ++j) {
                        if (assignment_reads_var(act.assignments[j], var_name)) {
                            break;  // Read detected
                        }
                        if (act.assignments[j].op == ir::AssignmentOp::Assign &&
                            store_overwrites_target(act.assignments[j].target, assign.target)) {
                            overwritten_before_read = true;
                            break;
                        }
                    }
                    if (overwritten_before_read) {
                        for (const auto& inst : act.instructions) {
                            if (action_reads_var(inst, var_name)) {
                                overwritten_before_read = false;
                                break;
                            }
                        }
                    }
                    if (overwritten_before_read) {
                        ++eliminated_count;
                        continue;
                    }
                }

                live_assigns.push_back(assign);
            }

            act.assignments = std::move(live_assigns);
        }
    };

    for (auto& state : ir.states) {
        for (auto& act : state.entry_actions)
            optimize_action(act);
        for (auto& act : state.exit_actions)
            optimize_action(act);
    }
    for (auto& edge : ir.transitions) {
        if (edge.transition_action)
            optimize_action(*edge.transition_action);
        if (edge.condition_action)
            optimize_action(*edge.condition_action);
    }

    if (eliminated_count > 0) {
        diag.report(diagnostic::Diagnostic::info(
            "DeadActionElimination", "Pruned " + std::to_string(eliminated_count) + " dead action instructions."));
    }

    return true;
}

}  // namespace fsm::middleend::passes
