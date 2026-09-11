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
        if (act.instructions.empty())
            return;

        std::vector<ir::ActionAstNode> live_insts;
        live_insts.reserve(act.instructions.size());

        for (std::size_t i = 0; i < act.instructions.size(); ++i) {
            const auto& inst = act.instructions[i];

            if (std::holds_alternative<ir::StoreOp>(inst.op)) {
                const auto& store = std::get<ir::StoreOp>(inst.op);
                const std::string& var_name = store.target.name;

                // Case A: Identity assignment (e.g. x = x)
                if (store.op == ir::AssignmentOp::Assign && store.expression == var_name) {
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
                            if (later_store.target.name == var_name && later_store.op == ir::AssignmentOp::Assign) {
                                overwritten_before_read = true;
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
