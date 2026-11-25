#include "fsm/ir/ir_simulator.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <sstream>
#include <string_view>
#include <variant>

#include "fsm/ir/expression.hpp"
#include "ir_simulator_expr.hpp"

namespace fsm::ir {

std::string SimulationReport::to_json() const {
    auto esc = [](std::string_view s) {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '"') {
                out += "\\\"";
            } else if (c == '\n') {
                out += "\\n";
            } else {
                out.push_back(c);
            }
        }
        return out;
    };
    auto emit_str_array = [&](std::ostringstream& os, const std::vector<std::string>& arr) {
        os << "[";
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) {
                os << ", ";
            }
            os << "\"" << esc(arr[i]) << "\"";
        }
        os << "]";
    };

    std::ostringstream os;
    os << "{\n";
    os << "  \"model\": \"" << esc(model_name) << "\",\n";
    os << "  \"initial_state\": \"" << esc(initial_state) << "\",\n";
    os << "  \"final_state\": \"" << esc(final_state) << "\",\n";
    os << "  \"final_registers\": {";
    bool first_reg = true;
    for (const auto& [k, v] : final_registers) {
        if (!first_reg) {
            os << ", ";
        }
        os << "\"" << esc(k) << "\": " << v;
        first_reg = false;
    }
    os << "},\n";
    os << "  \"steps\": [\n";
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& st = steps[i];
        os << "    {\n";
        os << "      \"step\": " << st.step_index << ",\n";
        os << "      \"from\": \"" << esc(st.from_state) << "\",\n";
        os << "      \"event\": \"" << esc(st.event) << "\",\n";
        os << "      \"to\": \"" << esc(st.to_state) << "\",\n";
        os << "      \"handled\": " << (st.handled ? "true" : "false") << ",\n";
        os << "      \"deferred\": " << (st.deferred ? "true" : "false") << ",\n";
        os << "      \"recalled\": " << (st.recalled ? "true" : "false") << ",\n";
        os << "      \"exited\": ";
        emit_str_array(os, st.exited_states);
        os << ",\n";
        os << "      \"entered\": ";
        emit_str_array(os, st.entered_states);
        os << ",\n";
        os << "      \"actions\": ";
        emit_str_array(os, st.executed_actions);
        os << "\n";
        os << "    }" << (i + 1 < steps.size() ? "," : "") << "\n";
    }
    os << "  ]\n";
    os << "}\n";
    return os.str();
}

SimulationReport IrSimulator::simulate_detailed(const FsmIr& model, const std::string& trace_content, std::ostream& out,
                                                const SimulationOptions& sim_opts) {
    SimulationReport report;
    report.model_name = model.name;

    std::unordered_map<std::string, double> env;
    for (const auto& var : model.variables) {
        if (!var.initial_value.empty()) {
            try {
                env[var.name] = std::stod(var.initial_value);
            } catch (...) {
                env[var.name] = 0.0;
            }
        } else {
            env[var.name] = 0.0;
        }
    }

    // Ancestor chain helper: root -> ... -> state
    auto get_ancestors = [&](const std::string& state_name) -> std::vector<std::string> {
        std::vector<std::string> chain;
        std::string cur = state_name;
        std::size_t guard_depth = 0;
        while (!cur.empty() && guard_depth++ < 64) {
            chain.push_back(cur);
            const auto* st = model.find_state(cur);
            if (st == nullptr || st->parent_state.empty()) {
                break;
            }
            cur = st->parent_state;
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    };

    auto eval_rhs = [&](const std::string& expr_str, const std::unordered_map<std::string, double>& s_env,
                        double& rhs_out) -> bool {
        auto ast = ExpressionAstNode::parse(expr_str);
        if (ast.kind != ExpressionKind::RawExpression) {
            if (auto v = ast.evaluate_numeric(s_env); v.has_value()) {
                rhs_out = *v;
                return true;
            }
        }
        if (auto it = s_env.find(expr_str); it != s_env.end()) {
            rhs_out = it->second;
            return true;
        }
        try {
            rhs_out = std::stod(expr_str);
            return true;
        } catch (...) {
            return false;
        }
    };

    auto apply_assign_op = [&](const std::string& target_name, AssignmentOp op, double rhs,
                               std::unordered_map<std::string, double>& s_env) {
        if (op == AssignmentOp::Assign) {
            env[target_name] = rhs;
        } else if (op == AssignmentOp::AddAssign) {
            env[target_name] += rhs;
        } else if (op == AssignmentOp::SubAssign) {
            env[target_name] -= rhs;
        } else if (op == AssignmentOp::MulAssign) {
            env[target_name] *= rhs;
        } else if (op == AssignmentOp::DivAssign && rhs != 0.0) {
            env[target_name] /= rhs;
        }
        s_env[target_name] = env[target_name];
    };

    auto apply_action_sig = [&](const ActionSignature& act_sig, std::unordered_map<std::string, double>& s_env,
                                std::vector<std::string>* action_log) {
        if (action_log != nullptr && !act_sig.name.empty()) {
            action_log->push_back(act_sig.name);
        }
        for (const auto& assign : act_sig.assignments) {
            double rhs = 0.0;
            if (eval_rhs(assign.expression, s_env, rhs)) {
                apply_assign_op(assign.target.name, assign.op, rhs, s_env);
            }
        }
        for (const auto& inst : act_sig.instructions) {
            if (const auto* st = std::get_if<StoreOp>(&inst.op)) {
                double rhs = 0.0;
                if (eval_rhs(st->expression, s_env, rhs)) {
                    apply_assign_op(st->target.name, st->op, rhs, s_env);
                }
            } else if (const auto* pw = std::get_if<PortWriteOp>(&inst.op)) {
                double rhs = 0.0;
                if (eval_rhs(pw->expression, s_env, rhs)) {
                    env[pw->port_name] = rhs;
                    s_env[pw->port_name] = rhs;
                }
            }
        }
    };

    // History tables keyed by composite parent state name
    std::unordered_map<std::string, std::string> shallow_history;
    std::unordered_map<std::string, std::string> deep_history;

    auto resolve_entry_target = [&](const std::string& raw_target) -> std::string {
        std::string target = raw_target;
        std::size_t depth = 0;
        while (const auto* st = model.find_state(target)) {
            if (++depth > 64) {
                break;
            }
            if (st->kind == StateKind::DeepHistory && !st->parent_state.empty()) {
                if (auto it = deep_history.find(st->parent_state); it != deep_history.end()) {
                    return it->second;
                }
                const auto* parent = model.find_state(st->parent_state);
                if (parent != nullptr && !parent->initial_sub_state.empty()) {
                    target = parent->initial_sub_state;
                    continue;
                }
                break;
            }
            if (st->kind == StateKind::ShallowHistory && !st->parent_state.empty()) {
                if (auto it = shallow_history.find(st->parent_state); it != shallow_history.end()) {
                    target = it->second;
                    continue;
                }
                const auto* parent = model.find_state(st->parent_state);
                if (parent != nullptr && !parent->initial_sub_state.empty()) {
                    target = parent->initial_sub_state;
                    continue;
                }
                break;
            }
            if (st->has_deep_history) {
                if (auto it = deep_history.find(st->name); it != deep_history.end()) {
                    return it->second;
                }
            }
            if (st->has_history) {
                if (auto it = shallow_history.find(st->name); it != shallow_history.end()) {
                    target = it->second;
                    continue;
                }
            }
            if (!st->initial_sub_state.empty()) {
                target = st->initial_sub_state;
            } else {
                break;
            }
        }
        return target;
    };

    std::string current_state = model.initial_state;
    if (current_state.empty() && !model.states.empty()) {
        current_state = model.states.front().name;
    }
    current_state = resolve_entry_target(current_state);
    report.initial_state = current_state;

    // Execute initial entry actions along initial ancestor chain
    {
        auto init_env = env;
        for (const auto& s_name : get_ancestors(current_state)) {
            if (const auto* st = model.find_state(s_name)) {
                for (const auto& act : st->entry_actions) {
                    apply_action_sig(act, init_env, nullptr);
                }
            }
        }
    }

    out << "============================================================================\n"
        << " fsmc IR Simulator : " << model.name << "\n"
        << " Initial State     : " << current_state << "\n"
        << "----------------------------------------------------------------------------\n";

    auto is_event_deferred_in_state = [&](const std::string& state_name, const std::string& ev_name) -> bool {
        for (const auto& anc : get_ancestors(state_name)) {
            if (const auto* st = model.find_state(anc)) {
                if (std::find(st->deferred_events.begin(), st->deferred_events.end(), ev_name) !=
                    st->deferred_events.end()) {
                    return true;
                }
            }
        }
        return false;
    };

    std::deque<SimulatedEvent> deferred_queue;
    std::size_t step_counter = 0;

    auto process_event = [&](const SimulatedEvent& ev, bool is_recalled) -> bool {
        const std::size_t step = step_counter++;
        SimulationTraceStep trace_step;
        trace_step.step_index = step;
        trace_step.from_state = current_state;
        trace_step.event = ev.name;
        trace_step.recalled = is_recalled;

        auto step_env = env;
        for (const auto& [k, v] : ev.numeric_payload) {
            step_env[k] = v;
            step_env["payload." + k] = v;
            step_env["cmd." + k] = v;
            step_env["event." + k] = v;
        }

        // 1. Safe payload rejection check against SignalDefinition::validators
        if (const auto* sig = model.find_signal(ev.name)) {
            bool payload_valid = true;
            for (const auto& val_expr : sig->validators) {
                bool sym = false;
                if (!simulator_detail::eval_numeric_predicate(val_expr, step_env, sym, &model,
                                                              sim_opts.strict_guards)) {
                    payload_valid = false;
                    break;
                }
            }
            if (!payload_valid) {
                out << "[STEP " << step << "] " << current_state << " --(" << ev.name
                    << ")--> [REJECTED: invalid signal payload]\n";
                trace_step.to_state = current_state;
                for (const auto& [k, v] : env) {
                    trace_step.registers_snapshot[k] = v;
                }
                report.steps.push_back(std::move(trace_step));
                return false;
            }
        }

        // 2. Match transition along active state hierarchy (leaf-to-root)
        const TransitionEdge* matched = nullptr;
        bool used_symbolic_guard = false;
        std::string probe = current_state;
        while (!probe.empty() && matched == nullptr) {
            for (const auto& tr : model.transitions) {
                if (tr.source != probe) {
                    continue;
                }
                if (tr.get_trigger_name() != ev.name) {
                    continue;
                }
                bool sym = false;
                auto resolve_guard_atom = [&](const std::string& expr) -> bool {
                    bool atom_sym = false;
                    bool ok = simulator_detail::eval_numeric_predicate(expr, step_env, atom_sym, &model,
                                                                       sim_opts.strict_guards);
                    if (atom_sym) {
                        sym = true;
                    }
                    return ok;
                };
                auto eval_guard_ast = [&](auto& self, const GuardAstNode& node) -> bool {
                    if (node.op == GuardOp::None) {
                        return resolve_guard_atom(node.expression);
                    }
                    if (node.op == GuardOp::Not && node.children.size() == 1) {
                        return !self(self, node.children[0]);
                    }
                    if (node.op == GuardOp::And && !node.children.empty()) {
                        for (const auto& ch : node.children) {
                            if (!self(self, ch)) {
                                return false;
                            }
                        }
                        return true;
                    }
                    if (node.op == GuardOp::Or && !node.children.empty()) {
                        for (const auto& ch : node.children) {
                            if (self(self, ch)) {
                                return true;
                            }
                        }
                        return false;
                    }
                    return resolve_guard_atom(node.expression);
                };
                if (const auto* st_trig = std::get_if<SignalTrigger>(&tr.trigger)) {
                    if (!st_trig->payload_binding.empty()) {
                        for (const auto& [k, v] : ev.numeric_payload) {
                            step_env[st_trig->payload_binding + "." + k] = v;
                        }
                    }
                }
                bool guard_passed = false;
                if (tr.guard_ast.has_value() && tr.guard_ast->op != GuardOp::None) {
                    guard_passed = eval_guard_ast(eval_guard_ast, *tr.guard_ast);
                } else {
                    guard_passed = resolve_guard_atom(tr.guard.value_or(""));
                }
                if (guard_passed) {
                    matched = &tr;
                    used_symbolic_guard = sym;
                    break;
                }
            }
            if (matched == nullptr) {
                const auto* st = model.find_state(probe);
                probe = (st != nullptr) ? st->parent_state : "";
            }
        }

        if (matched == nullptr) {
            if (!is_recalled && is_event_deferred_in_state(current_state, ev.name)) {
                deferred_queue.push_back(ev);
                out << "[STEP " << step << "] " << current_state << " --(" << ev.name << ")--> [DEFERRED]\n";
                trace_step.deferred = true;
            } else {
                out << "[STEP " << step << "] " << current_state << " --(" << ev.name << ")--> [UNHANDLED]\n";
            }
            trace_step.to_state = current_state;
            for (const auto& [k, v] : env) {
                trace_step.registers_snapshot[k] = v;
            }
            report.steps.push_back(std::move(trace_step));
            return false;
        }

        trace_step.handled = true;
        trace_step.used_symbolic_guard = used_symbolic_guard;

        const bool is_internal = (matched->kind == TransitionEdgeKind::Internal);
        std::string next_state = is_internal ? current_state : resolve_entry_target(matched->target);

        if (is_internal) {
            if (matched->condition_action.has_value()) {
                apply_action_sig(*matched->condition_action, step_env, &trace_step.executed_actions);
            }
            if (matched->transition_action.has_value()) {
                apply_action_sig(*matched->transition_action, step_env, &trace_step.executed_actions);
            }
        } else {
            // Compute LCA between current_state and next_state
            const auto src_chain = get_ancestors(current_state);
            const auto dst_chain = get_ancestors(next_state);
            std::size_t lca_len = 0;
            const bool is_self = (current_state == next_state);
            if (!is_self) {
                while (lca_len < src_chain.size() && lca_len < dst_chain.size() &&
                       src_chain[lca_len] == dst_chain[lca_len]) {
                    ++lca_len;
                }
            } else if (src_chain.size() > 1) {
                lca_len = src_chain.size() - 1;
            }

            // Record shallow and deep history for exited composite ancestors
            for (std::size_t idx = src_chain.size(); idx > lca_len; --idx) {
                const std::string& exiting = src_chain[idx - 1];
                if (idx >= 2) {
                    shallow_history[src_chain[idx - 2]] = exiting;
                }
                deep_history[exiting] = current_state;
                if (idx >= 2) {
                    deep_history[src_chain[idx - 2]] = current_state;
                }
                trace_step.exited_states.push_back(exiting);
                if (const auto* st = model.find_state(exiting)) {
                    for (const auto& act : st->exit_actions) {
                        apply_action_sig(act, step_env, &trace_step.executed_actions);
                    }
                }
            }

            // Transition actions
            if (matched->condition_action.has_value()) {
                apply_action_sig(*matched->condition_action, step_env, &trace_step.executed_actions);
            }
            if (matched->transition_action.has_value()) {
                apply_action_sig(*matched->transition_action, step_env, &trace_step.executed_actions);
            }

            // Enter target states top-down from LCA
            for (std::size_t idx = lca_len; idx < dst_chain.size(); ++idx) {
                const std::string& entering = dst_chain[idx];
                trace_step.entered_states.push_back(entering);
                if (const auto* st = model.find_state(entering)) {
                    for (const auto& act : st->entry_actions) {
                        apply_action_sig(act, step_env, &trace_step.executed_actions);
                    }
                    if (st->do_activity.has_value() && !st->do_activity->empty()) {
                        trace_step.executed_actions.push_back(*st->do_activity);
                    }
                }
            }
        }

        out << "[STEP " << step << "] " << current_state << " --(" << ev.name;
        if (!ev.numeric_payload.empty()) {
            out << " {";
            bool first = true;
            for (const auto& [k, v] : ev.numeric_payload) {
                if (!first) {
                    out << ", ";
                }
                out << k << "=" << v;
                first = false;
            }
            out << "}";
        }
        out << ")--> " << next_state;
        if (is_recalled) {
            out << " [recalled]";
        }
        if (used_symbolic_guard) {
            out << " [symbolic-guard]";
        }
        out << "\n";

        const bool state_changed = (next_state != current_state);
        current_state = std::move(next_state);
        trace_step.to_state = current_state;
        for (const auto& [k, v] : env) {
            trace_step.registers_snapshot[k] = v;
        }
        report.steps.push_back(std::move(trace_step));
        return state_changed;
    };

    auto events = simulator_detail::parse_trace_input(trace_content);
    for (const auto& ev : events) {
        bool changed = process_event(ev, false);
        // Recall deferred events whenever state changes (bounded to prevent infinite loops)
        std::size_t recall_budget = 64;
        while (changed && !deferred_queue.empty() && recall_budget-- > 0) {
            changed = false;
            std::size_t q_size = deferred_queue.size();
            for (std::size_t i = 0; i < q_size; ++i) {
                SimulatedEvent dev = std::move(deferred_queue.front());
                deferred_queue.pop_front();
                if (is_event_deferred_in_state(current_state, dev.name)) {
                    deferred_queue.push_back(std::move(dev));
                } else {
                    if (process_event(dev, true)) {
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    report.final_state = current_state;
    for (const auto& [k, v] : env) {
        report.final_registers[k] = v;
    }

    out << "----------------------------------------------------------------------------\n"
        << " Final State       : " << current_state << "\n"
        << "============================================================================\n";
    return report;
}

std::string IrSimulator::simulate_trace(const FsmIr& model, const std::string& trace_content, std::ostream& out) {
    return simulate_detailed(model, trace_content, out).final_state;
}

}  // namespace fsm::ir
