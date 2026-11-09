#include "fsm/middleend/passes/submachine_inlining_pass.hpp"

#include <cctype>
#include <map>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string rewrite_identifiers(const std::string& expr, const std::map<std::string, std::string>& var_map) {
    if (expr.empty() || var_map.empty()) {
        return expr;
    }
    std::string out;
    out.reserve(expr.size() + 16);
    std::size_t i = 0;
    const std::size_t n = expr.size();
    while (i < n) {
        if (expr[i] == '"' || expr[i] == '\'') {
            char quote = expr[i];
            out.push_back(expr[i++]);
            while (i < n) {
                char c = expr[i++];
                out.push_back(c);
                if (c == '\\' && i < n) {
                    out.push_back(expr[i++]);
                } else if (c == quote) {
                    break;
                }
            }
            continue;
        }
        if (is_ident_start(expr[i])) {
            bool is_member = (i > 0 && expr[i - 1] == '.') || (i > 1 && expr[i - 2] == '-' && expr[i - 1] == '>');
            std::size_t start = i;
            while (i < n && is_ident_char(expr[i])) {
                ++i;
            }
            std::string tok = expr.substr(start, i - start);
            auto it = var_map.find(tok);
            if (!is_member && it != var_map.end()) {
                out += it->second;
            } else {
                out += tok;
            }
            continue;
        }
        out.push_back(expr[i++]);
    }
    return out;
}

void rewrite_expr_ast(ExpressionAstNode& node, const std::map<std::string, std::string>& var_map) {
    if (var_map.empty()) {
        return;
    }
    if (node.kind == ExpressionKind::VariableRef) {
        auto it = var_map.find(node.symbol);
        if (it != var_map.end()) {
            node.symbol = it->second;
        }
    } else if (node.kind == ExpressionKind::RawExpression) {
        node.symbol = rewrite_identifiers(node.symbol, var_map);
    }
    for (auto& child : node.children) {
        rewrite_expr_ast(child, var_map);
    }
}

void rewrite_guard_ast(GuardAstNode& gnode, const std::map<std::string, std::string>& var_map) {
    if (var_map.empty()) {
        return;
    }
    if (!gnode.expression.empty()) {
        gnode.expression = rewrite_identifiers(gnode.expression, var_map);
    }
    for (auto& ch : gnode.children) {
        rewrite_guard_ast(ch, var_map);
    }
}

void rewrite_action(ActionSignature& act, const std::map<std::string, std::string>& var_map) {
    if (var_map.empty()) {
        return;
    }
    if (!act.invocation.empty()) {
        act.invocation = rewrite_identifiers(act.invocation, var_map);
    }
    for (auto& assign : act.assignments) {
        auto it = var_map.find(assign.target.name);
        if (it != var_map.end()) {
            assign.target.name = it->second;
        }
        assign.expression = rewrite_identifiers(assign.expression, var_map);
        if (assign.expr_ast) {
            rewrite_expr_ast(*assign.expr_ast, var_map);
        }
    }
    for (auto& inst : act.instructions) {
        if (auto* store = std::get_if<StoreOp>(&inst.op)) {
            auto it = var_map.find(store->target.name);
            if (it != var_map.end()) {
                store->target.name = it->second;
            }
            store->expression = rewrite_identifiers(store->expression, var_map);
            if (store->expr_ast) {
                rewrite_expr_ast(*store->expr_ast, var_map);
            }
        } else if (auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
            pw->expression = rewrite_identifiers(pw->expression, var_map);
            if (pw->expr_ast) {
                rewrite_expr_ast(*pw->expr_ast, var_map);
            }
        } else if (auto* pr = std::get_if<PortReadOp>(&inst.op)) {
            auto it = var_map.find(pr->destination.name);
            if (it != var_map.end()) {
                pr->destination.name = it->second;
            }
        } else if (auto* sig = std::get_if<SignalEmitOp>(&inst.op)) {
            for (auto& arg : sig->arguments) {
                arg = rewrite_identifiers(arg, var_map);
            }
        } else if (auto* call = std::get_if<ActionCallOp>(&inst.op)) {
            for (auto& arg : call->arguments) {
                arg = rewrite_identifiers(arg, var_map);
            }
        }
    }
}

}  // namespace

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

        if (!ir.name.empty() && ir.name != "Fsm" && ir.name != "MyStateMachine" &&
            st.submachine->fsm_name == ir.name) {
            diag.report(Diagnostic::error("E_SUBMACHINE_RECURSIVE",
                                          "Recursive self-inlining of submachine '" + st.submachine->fsm_name +
                                              "' in state '" + st.name + "' is not allowed."));
            continue;
        }

        const FsmIr* sub_model = resolver_(st.submachine->fsm_name);
        if (sub_model == nullptr) {
            diag.report(Diagnostic::warning(
                "W_SUBMACHINE_NOT_FOUND",
                "Could not resolve submachine model '" + st.submachine->fsm_name + "' for state '" + st.name + "'."));
            continue;
        }

        if (sub_model == &ir ||
            (!ir.name.empty() && ir.name != "Fsm" && ir.name != "MyStateMachine" &&
             (sub_model->name == ir.name || st.submachine->fsm_name == ir.name))) {
            diag.report(Diagnostic::error("E_SUBMACHINE_RECURSIVE",
                                          "Recursive self-inlining of submachine '" + st.submachine->fsm_name +
                                              "' in state '" + st.name + "' is not allowed."));
            continue;
        }

        st.is_composite = true;
        st.kind = StateKind::Composite;

        // Map submachine variables first so we can rewrite references in states and transitions
        std::map<std::string, std::string> var_map;
        for (const auto& var : sub_model->variables) {
            VariableDefinition cloned_var = var;
            cloned_var.name = st.name + "_" + var.name;
            var_map[var.name] = cloned_var.name;
            if (ir.find_variable(cloned_var.name) == nullptr) {
                ir.variables.push_back(std::move(cloned_var));
            }
        }

        // Map submachine states into parent
        std::map<std::string, std::string> name_map;
        std::map<std::string, std::string> id_map;
        std::map<std::string, std::string> name_to_id_map;

        for (const auto& sub_st : sub_model->states) {
            std::string inlined_name = st.name + "_" + sub_st.name;
            name_map[sub_st.name] = inlined_name;
            std::string inlined_fqn = st.fqn + "." + sub_st.name;
            std::string inlined_id = compute_deterministic_id(inlined_fqn);
            id_map[sub_st.id] = inlined_id;
            id_map[sub_st.name] = inlined_id;
            name_to_id_map[inlined_name] = inlined_id;
        }

        for (const auto& sub_st : sub_model->states) {
            StateNode cloned = sub_st;
            std::string inlined_name = name_map[sub_st.name];

            cloned.name = inlined_name;
            cloned.fqn = st.fqn + "." + sub_st.name;
            cloned.id = name_to_id_map[inlined_name];
            if (sub_st.parent_state.empty()) {
                cloned.parent_state = st.name;
                cloned.parent_id = st.id;
                st.children_ids.push_back(cloned.id);
            } else {
                cloned.parent_state = name_map.count(sub_st.parent_state) ? name_map[sub_st.parent_state]
                                                                          : (st.name + "_" + sub_st.parent_state);
                cloned.parent_id = name_to_id_map.count(cloned.parent_state)
                                       ? name_to_id_map[cloned.parent_state]
                                       : compute_deterministic_id(cloned.parent_state);
            }

            if (!sub_st.initial_sub_state.empty()) {
                cloned.initial_sub_state = name_map.count(sub_st.initial_sub_state)
                                               ? name_map[sub_st.initial_sub_state]
                                               : (st.name + "_" + sub_st.initial_sub_state);
            }

            cloned.children_ids.clear();
            for (const auto& cid : sub_st.children_ids) {
                if (id_map.count(cid)) {
                    cloned.children_ids.push_back(id_map[cid]);
                }
            }

            for (auto& act : cloned.entry_actions) {
                rewrite_action(act, var_map);
            }
            for (auto& act : cloned.exit_actions) {
                rewrite_action(act, var_map);
            }
            for (auto& inv : cloned.invariants) {
                rewrite_guard_ast(inv, var_map);
            }

            inlined_states.push_back(std::move(cloned));
        }

        // Ensure nested states are registered in their inlined parent's children_ids
        for (const auto& sub_st : sub_model->states) {
            if (!sub_st.parent_state.empty()) {
                std::string parent_inlined = name_map.count(sub_st.parent_state)
                                                 ? name_map[sub_st.parent_state]
                                                 : (st.name + "_" + sub_st.parent_state);
                std::string child_id = name_to_id_map[name_map[sub_st.name]];
                for (auto& inl_st : inlined_states) {
                    if (inl_st.name == parent_inlined) {
                        if (std::find(inl_st.children_ids.begin(), inl_st.children_ids.end(), child_id) ==
                            inl_st.children_ids.end()) {
                            inl_st.children_ids.push_back(child_id);
                        }
                        break;
                    }
                }
            }
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
                if (branch.guard.has_value() && !branch.guard->empty() && *branch.guard != "else") {
                    branch.set_guard(rewrite_identifiers(*branch.guard, var_map));
                }
                if (branch.transition_action.has_value()) {
                    rewrite_action(*branch.transition_action, var_map);
                }
                if (branch.condition_action.has_value()) {
                    rewrite_action(*branch.condition_action, var_map);
                }
            }
            ir.choice_nodes.push_back(std::move(cloned_cn));
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

            if (cloned_t.guard.has_value() && !cloned_t.guard->empty() && *cloned_t.guard != "else") {
                cloned_t.set_guard(rewrite_identifiers(*cloned_t.guard, var_map));
            } else if (cloned_t.guard_ast.has_value()) {
                rewrite_guard_ast(*cloned_t.guard_ast, var_map);
            }
            if (cloned_t.transition_action.has_value()) {
                rewrite_action(*cloned_t.transition_action, var_map);
            }
            if (cloned_t.condition_action.has_value()) {
                rewrite_action(*cloned_t.condition_action, var_map);
            }

            cloned_t.id = compute_deterministic_id(cloned_t.source + "->" + cloned_t.target + ":" + cloned_t.event);
            inlined_transitions.push_back(std::move(cloned_t));
        }

        // Remap Entry and Exit Port Mappings
        for (const auto& port : st.submachine->port_mappings) {
            if (!port.entry_point.empty() && name_map.count(port.entry_point) != 0) {
                st.initial_sub_state = name_map[port.entry_point];
                for (auto& host_t : ir.transitions) {
                    if (host_t.target == port.entry_point || host_t.target == (st.name + "." + port.entry_point)) {
                        host_t.target = name_map[port.entry_point];
                        host_t.target_id = name_to_id_map[host_t.target];
                        host_t.target_ids = {host_t.target};
                        host_t.multi_target_ids = {host_t.target_id};
                    }
                }
            }
            if (!port.exit_point.empty() && name_map.count(port.exit_point) != 0) {
                const std::string& mapped_exit = name_map[port.exit_point];
                const std::string& mapped_exit_id = name_to_id_map[mapped_exit];
                for (auto& host_t : ir.transitions) {
                    if (host_t.source == port.exit_point || host_t.source == (st.name + "." + port.exit_point) ||
                        host_t.source == st.name) {
                        host_t.source = mapped_exit;
                        host_t.source_id = mapped_exit_id;
                        host_t.source_ids = {mapped_exit};
                        host_t.multi_source_ids = {mapped_exit_id};
                    }
                }
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
