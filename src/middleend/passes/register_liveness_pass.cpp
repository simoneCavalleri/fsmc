/**
 * @file register_liveness_pass.cpp
 * @brief Implementation of RegisterLivenessPass.
 */

#include "fsm/middleend/passes/register_liveness_pass.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
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

template <typename SetType>
void collect_action_use_def(const ir::ActionSignature& act, const std::unordered_set<std::string>& known_vars,
                            SetType& use, SetType& def) {
    for (const auto& inst : act.instructions) {
        if (const auto* st = std::get_if<ir::StoreOp>(&inst.op)) {
            if (st->op != ir::AssignmentOp::Assign && known_vars.count(st->target.name) &&
                def.count(st->target.name) == 0) {
                use.insert(st->target.name);
            }
            for (const auto& id : extract_identifiers(st->expression)) {
                if (known_vars.count(id) && def.count(id) == 0) {
                    use.insert(id);
                }
            }
            if (st->op == ir::AssignmentOp::Assign && st->target.member_path.empty() &&
                !st->target.constant_index.has_value() && known_vars.count(st->target.name) &&
                use.count(st->target.name) == 0) {
                def.insert(st->target.name);
            }
        } else if (const auto* pw = std::get_if<ir::PortWriteOp>(&inst.op)) {
            for (const auto& id : extract_identifiers(pw->expression)) {
                if (known_vars.count(id) && def.count(id) == 0) {
                    use.insert(id);
                }
            }
        } else if (const auto* pr = std::get_if<ir::PortReadOp>(&inst.op)) {
            if (!pr->destination.name.empty() && pr->destination.member_path.empty() &&
                !pr->destination.constant_index.has_value() && known_vars.count(pr->destination.name) &&
                use.count(pr->destination.name) == 0) {
                def.insert(pr->destination.name);
            }
        } else if (const auto* se = std::get_if<ir::SignalEmitOp>(&inst.op)) {
            for (const auto& arg : se->arguments) {
                for (const auto& id : extract_identifiers(arg)) {
                    if (known_vars.count(id) && def.count(id) == 0) {
                        use.insert(id);
                    }
                }
            }
        } else if (const auto* ac = std::get_if<ir::ActionCallOp>(&inst.op)) {
            for (const auto& r : ac->read_set) {
                if (known_vars.count(r) && def.count(r) == 0) {
                    use.insert(r);
                }
            }
            for (const auto& arg : ac->arguments) {
                for (const auto& id : extract_identifiers(arg)) {
                    if (known_vars.count(id) && def.count(id) == 0) {
                        use.insert(id);
                    }
                }
            }
            for (const auto& w : ac->write_set) {
                if (known_vars.count(w) && use.count(w) == 0) {
                    def.insert(w);
                }
            }
        }
    }
    for (const auto& assign : act.assignments) {
        if (assign.op != ir::AssignmentOp::Assign && known_vars.count(assign.target.name) &&
            def.count(assign.target.name) == 0) {
            use.insert(assign.target.name);
        }
        for (const auto& id : extract_identifiers(assign.expression)) {
            if (known_vars.count(id) && def.count(id) == 0) {
                use.insert(id);
            }
        }
        if (assign.op == ir::AssignmentOp::Assign && assign.target.member_path.empty() &&
            !assign.target.constant_index.has_value() && known_vars.count(assign.target.name) &&
            use.count(assign.target.name) == 0) {
            def.insert(assign.target.name);
        }
    }
}

}  // namespace

bool RegisterLivenessPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    if (ir.variables.empty()) {
        return true;
    }

    std::unordered_set<std::string> known_vars;
    for (const auto& v : ir.variables) {
        known_vars.insert(v.name);
    }

    // Map each state ID to uses and defs
    std::unordered_map<std::string, std::unordered_set<std::string>> state_uses;
    std::unordered_map<std::string, std::unordered_set<std::string>> state_defs;
    std::unordered_map<std::string, std::vector<std::string>> successors;
    std::unordered_map<std::string, std::vector<std::unordered_set<std::string>>> outgoing_edge_defs;
    std::unordered_map<std::string, bool> has_guarded_outgoing;

    for (const auto& state : ir.states) {
        state_uses[state.id] = {};
        state_defs[state.id] = {};
        successors[state.id] = {};

        // State entry actions execute first upon entering the state:
        // track reads that occur before an unconditional write vs writes that occur before any read.
        for (const auto& act : state.entry_actions) {
            collect_action_use_def(act, known_vars, state_uses[state.id], state_defs[state.id]);
        }

        // Invariants use variables while in the state
        for (const auto& inv : state.invariants) {
            for (const auto& id : extract_identifiers(inv.to_string())) {
                if (known_vars.count(id) && state_defs[state.id].count(id) == 0)
                    state_uses[state.id].insert(id);
            }
        }

        // State exit actions
        for (const auto& act : state.exit_actions) {
            collect_action_use_def(act, known_vars, state_uses[state.id], state_defs[state.id]);
        }
    }

    struct EdgeInfo {
        std::string src_id;
        std::string dst_id;
        std::set<std::string> uses;
        std::set<std::string> defs;
    };
    std::vector<EdgeInfo> edges;
    edges.reserve(ir.transitions.size());

    for (const auto& edge : ir.transitions) {
        std::string src_id = edge.source_id;
        if (auto* s = ir.find_state_by_id(src_id); !s) {
            if (auto* s2 = ir.find_state_by_name(edge.source.empty() ? src_id : edge.source)) {
                src_id = s2->id;
            }
        }
        std::string dst_id = edge.target_id;
        if (auto* d = ir.find_state_by_id(dst_id); !d) {
            if (auto* d2 = ir.find_state_by_name(edge.target.empty() ? dst_id : edge.target)) {
                dst_id = d2->id;
            }
        }

        std::set<std::string> trans_uses;
        // Transition guards use variables
        if (edge.guard_ast.has_value()) {
            for (const auto& id : extract_identifiers(edge.guard_ast->to_string())) {
                if (known_vars.count(id)) {
                    trans_uses.insert(id);
                }
            }
        } else if (edge.guard && !edge.guard->empty()) {
            for (const auto& id : extract_identifiers(*edge.guard)) {
                if (known_vars.count(id)) {
                    trans_uses.insert(id);
                }
            }
            for (const auto& gm : ir.guards) {
                if (gm.name == *edge.guard) {
                    if (gm.raw_expression.has_value()) {
                        for (const auto& id : extract_identifiers(*gm.raw_expression)) {
                            if (known_vars.count(id)) {
                                trans_uses.insert(id);
                            }
                        }
                    }
                    if (gm.normalized_expression.has_value()) {
                        for (const auto& id : extract_identifiers(*gm.normalized_expression)) {
                            if (known_vars.count(id)) {
                                trans_uses.insert(id);
                            }
                        }
                    }
                    break;
                }
            }
        }
        if (const auto* ct = std::get_if<ir::ChangeTrigger>(&edge.trigger)) {
            if (ct->predicate.has_value()) {
                for (const auto& id : extract_identifiers(ct->predicate->to_string())) {
                    if (known_vars.count(id)) {
                        trans_uses.insert(id);
                    }
                }
            }
            for (const auto& id : extract_identifiers(ct->raw_expression)) {
                if (known_vars.count(id)) {
                    trans_uses.insert(id);
                }
            }
        } else if (const auto* tt = std::get_if<ir::TimeTrigger>(&edge.trigger)) {
            if (!tt->dynamic_expression.empty()) {
                for (const auto& id : extract_identifiers(tt->dynamic_expression)) {
                    if (known_vars.count(id)) {
                        trans_uses.insert(id);
                    }
                }
            }
        }

        // Condition actions execute before transition actions
        std::set<std::string> edge_defs;
        if (edge.condition_action.has_value()) {
            collect_action_use_def(*edge.condition_action, known_vars, trans_uses, edge_defs);
        }

        // Transition actions
        if (edge.transition_action.has_value()) {
            collect_action_use_def(*edge.transition_action, known_vars, trans_uses, edge_defs);
        }

        edges.push_back({src_id, dst_id, std::move(trans_uses), std::move(edge_defs)});
    }

    // Backward dataflow fixed point for LiveIn and LiveOut
    std::unordered_map<std::string, std::set<std::string>> live_in;
    std::unordered_map<std::string, std::set<std::string>> live_out;
    std::vector<std::set<std::string>> edge_live_in(edges.size());

    bool changed = true;
    while (changed) {
        changed = false;

        // 1. Edge LiveIn = (Target LiveIn \ Edge Defs) U Edge Uses
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const auto& e = edges[i];
            const auto& dst_in = live_in[e.dst_id];
            std::set<std::string> new_edge_in = dst_in;
            for (const auto& d : e.defs) {
                new_edge_in.erase(d);
            }
            for (const auto& u : e.uses) {
                new_edge_in.insert(u);
            }
            if (new_edge_in != edge_live_in[i]) {
                edge_live_in[i] = std::move(new_edge_in);
                changed = true;
            }
        }

        // 2. State LiveOut = Union(LiveIn of outgoing edges)
        //    State LiveIn  = (LiveOut \ State Defs) U State Uses
        for (const auto& state : ir.states) {
            const std::string& sid = state.id;

            std::set<std::string> new_out;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                if (edges[i].src_id == sid) {
                    new_out.insert(edge_live_in[i].begin(), edge_live_in[i].end());
                }
            }

            std::set<std::string> new_in = new_out;
            for (const auto& d : state_defs[sid]) {
                new_in.erase(d);
            }
            for (const auto& u : state_uses[sid]) {
                new_in.insert(u);
            }

            if (new_out != live_out[sid]) {
                live_out[sid] = std::move(new_out);
                changed = true;
            }
            if (new_in != live_in[sid]) {
                live_in[sid] = std::move(new_in);
                changed = true;
            }
        }
    }

    // Build interference graph: edges between variables live simultaneously
    std::map<std::string, std::set<std::string>> interference;
    for (const auto& v : ir.variables) {
        interference[v.name] = {};
    }

    auto add_interference_clique = [&](const std::set<std::string>& live_set) {
        std::vector<std::string> vars(live_set.begin(), live_set.end());
        for (std::size_t i = 0; i < vars.size(); ++i) {
            for (std::size_t j = i + 1; j < vars.size(); ++j) {
                interference[vars[i]].insert(vars[j]);
                interference[vars[j]].insert(vars[i]);
            }
        }
    };

    for (const auto& state : ir.states) {
        add_interference_clique(live_in[state.id]);
        add_interference_clique(live_out[state.id]);
    }
    for (const auto& eli : edge_live_in) {
        add_interference_clique(eli);
    }

    // Only share a hardware register_index between variables with the same DataType
    for (std::size_t i = 0; i < ir.variables.size(); ++i) {
        for (std::size_t j = i + 1; j < ir.variables.size(); ++j) {
            if (ir.variables[i].type != ir.variables[j].type) {
                interference[ir.variables[i].name].insert(ir.variables[j].name);
                interference[ir.variables[j].name].insert(ir.variables[i].name);
            }
        }
    }

    // Graph coloring (Welsh-Powell heuristic)
    std::vector<std::string> var_names;
    var_names.reserve(ir.variables.size());
    for (const auto& v : ir.variables) {
        var_names.push_back(v.name);
    }

    std::sort(var_names.begin(), var_names.end(), [&](const std::string& a, const std::string& b) {
        return interference[a].size() > interference[b].size();
    });

    std::unordered_map<std::string, std::size_t> colors;
    std::size_t max_color = 0;

    for (const auto& var : var_names) {
        std::set<std::size_t> used_colors;
        for (const auto& neighbor : interference[var]) {
            auto it = colors.find(neighbor);
            if (it != colors.end()) {
                used_colors.insert(it->second);
            }
        }

        std::size_t assigned_color = 0;
        while (used_colors.count(assigned_color)) {
            ++assigned_color;
        }

        colors[var] = assigned_color;
        if (assigned_color > max_color) {
            max_color = assigned_color;
        }
    }

    // Assign register index to variables
    for (auto& v : ir.variables) {
        if (colors.count(v.name)) {
            v.register_index = colors[v.name];
        }
    }

    std::size_t total_regs = max_color + 1;
    diag.report(diagnostic::Diagnostic::info("RegisterLiveness",
                                             "Allocated " + std::to_string(total_regs) + " hardware register(s) for " +
                                                 std::to_string(ir.variables.size()) + " state variable(s)."));

    return true;
}

}  // namespace fsm::middleend::passes
