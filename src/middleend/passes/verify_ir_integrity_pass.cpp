#include "fsm/middleend/passes/verify_ir_integrity_pass.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "fsm/ir/fsm_graph_ops.hpp"

namespace fsm::middleend::passes {

using diagnostic::Diagnostic;
using diagnostic::SourceSpan;
using ir::FsmIr;
using ir::PortDirection;
using ir::StateKind;

bool VerifyIrIntegrityPass::run(FsmIr& ir, diagnostic::DiagnosticEngine& diag) {
    bool ok = true;

    auto report_err = [&](const std::string& code, const std::string& msg, std::uint32_t line = 0) {
        SourceSpan span;
        if (line > 0) {
            span.line = line;
            span.column = 1;
        }
        diag.report(Diagnostic::error(code, msg, span));
        ok = false;
    };

    // 1. Root invariants (E_IR_ROOT)
    if (ir.name.empty()) {
        report_err("E_IR_ROOT", "FsmIr model name must not be empty.");
    }
    if (!ir.concurrency.is_valid()) {
        report_err("E_IR_ROOT",
                   "Invalid concurrency semantics: SynchronousReactive clock model incompatible with Interleaved "
                   "orthogonal resolution.");
    }
    if (!ir.initial_state.empty() && ir.find_state(ir.initial_state) == nullptr) {
        report_err("E_IR_ROOT", "Initial state '" + ir.initial_state + "' does not exist in FsmIr::states.");
    }

    // 2. Uniqueness of IDs, FQNs, ports, variables, custom types (E_IR_DUP_ID)
    std::unordered_set<std::string> seen_state_ids;
    std::unordered_set<std::string> seen_state_fqns;
    for (const auto& st : ir.states) {
        if (!st.id.empty() && !seen_state_ids.insert(st.id).second) {
            report_err("E_IR_DUP_ID", "Duplicate StateNode::id '" + st.id + "' on state '" + st.name + "'.",
                       st.source_line);
        }
        if (!st.fqn.empty() && !seen_state_fqns.insert(st.fqn).second) {
            report_err("E_IR_DUP_ID", "Duplicate StateNode::fqn '" + st.fqn + "'.", st.source_line);
        }
    }

    std::unordered_set<std::string> seen_ports;
    std::unordered_set<std::string> in_only_ports;
    for (const auto& p : ir.ports) {
        if (!seen_ports.insert(p.name).second) {
            report_err("E_IR_DUP_ID", "Duplicate PortDefinition::name '" + p.name + "'.");
        }
        if (p.direction == PortDirection::In) {
            in_only_ports.insert(p.name);
        }
    }

    std::unordered_set<std::string> seen_vars;
    for (const auto& v : ir.variables) {
        if (!seen_vars.insert(v.name).second) {
            report_err("E_IR_DUP_ID", "Duplicate VariableDefinition::name '" + v.name + "'.");
        }
    }

    std::unordered_set<std::string> seen_types;
    for (const auto& t : ir.custom_types) {
        if (!seen_types.insert(t.name).second) {
            report_err("E_IR_DUP_ID", "Duplicate TypeDefinition::name '" + t.name + "'.");
        }
    }

    std::unordered_map<std::string, const ir::ServiceMethodSignature*> declared_services;
    for (const auto& svc : ir.services) {
        if (!declared_services.emplace(svc.name, &svc).second) {
            report_err("E_IR_DUP_ID", "Duplicate ServiceMethodSignature::name '" + svc.name + "'.");
        }
    }

    // 3. Hierarchy integrity & acyclicity (E_IR_HIERARCHY)
    for (const auto& st : ir.states) {
        if (!st.parent_state.empty() && ir.find_state(st.parent_state) == nullptr) {
            report_err("E_IR_HIERARCHY",
                       "State '" + st.name + "' references non-existent parent_state '" + st.parent_state + "'.",
                       st.source_line);
        }
        if (!st.initial_sub_state.empty() && ir.find_state(st.initial_sub_state) == nullptr) {
            report_err("E_IR_HIERARCHY",
                       "Composite state '" + st.name + "' references non-existent initial_sub_state '" +
                           st.initial_sub_state + "'.",
                       st.source_line);
        }
        if ((st.kind == StateKind::ShallowHistory || st.kind == StateKind::DeepHistory) && st.parent_state.empty()) {
            report_err("E_IR_HIERARCHY",
                       "History pseudostate '" + st.name + "' must declare a valid composite parent_state.",
                       st.source_line);
        }
    }

    // Cycle detection on parent_state links (0 = unvisited, 1 = visiting, 2 = visited)
    std::unordered_map<std::string, std::uint8_t> color;
    for (const auto& st : ir.states) {
        if (color[st.name] != 0) {
            continue;
        }
        std::string cur = st.name;
        std::unordered_set<std::string> path;
        while (!cur.empty()) {
            if (color[cur] == 2) {
                break;
            }
            if (!path.insert(cur).second) {
                report_err("E_IR_HIERARCHY",
                           "Cyclic parent-child state hierarchy detected involving state '" + cur + "'.");
                break;
            }
            color[cur] = 1;
            const auto* node = ir.find_state(cur);
            if (node == nullptr) {
                break;
            }
            cur = node->parent_state;
        }
        for (const auto& visited : path) {
            color[visited] = 2;
        }
    }

    // 4. Transition endpoint integrity (E_IR_DANGLING_REF)
    std::unordered_set<std::string_view> choice_names;
    for (const auto& cn : ir.choice_nodes) {
        choice_names.insert(cn.name);
    }
    auto endpoint_exists = [&](const std::string& ep) -> bool {
        return ir.find_state(ep) != nullptr || choice_names.count(ep) != 0;
    };

    for (const auto& tr : ir.transitions) {
        if (!tr.source.empty() && !endpoint_exists(tr.source)) {
            report_err("E_IR_DANGLING_REF", "Transition references non-existent source state '" + tr.source + "'.",
                       tr.source_line);
        }
        if (!tr.target.empty() && !endpoint_exists(tr.target)) {
            report_err("E_IR_DANGLING_REF", "Transition references non-existent target state '" + tr.target + "'.",
                       tr.source_line);
        }
        for (const auto& sid : tr.source_ids) {
            if (!sid.empty() && !endpoint_exists(sid)) {
                report_err("E_IR_DANGLING_REF", "Transition source_ids references non-existent state '" + sid + "'.",
                           tr.source_line);
            }
        }
        for (const auto& tid : tr.target_ids) {
            if (!tid.empty() && !endpoint_exists(tid)) {
                report_err("E_IR_DANGLING_REF", "Transition target_ids references non-existent state '" + tid + "'.",
                           tr.source_line);
            }
        }
    }

    // 5. Datapath write-target integrity (E_IR_TYPE_MISMATCH: prohibit writes to read-only InPorts)
    auto check_action = [&](const ir::ActionSignature& act, std::uint32_t line) {
        for (const auto& assign : act.assignments) {
            if (!assign.target.name.empty() && in_only_ports.count(assign.target.name) != 0 &&
                seen_vars.count(assign.target.name) == 0) {
                report_err(
                    "E_IR_TYPE_MISMATCH",
                    "Action '" + act.name + "' attempts to write to read-only InPort '" + assign.target.name + "'.",
                    line);
            }
        }
        for (const auto& inst : act.instructions) {
            if (const auto* store = std::get_if<ir::StoreOp>(&inst.op)) {
                if (!store->target.name.empty() && in_only_ports.count(store->target.name) != 0 &&
                    seen_vars.count(store->target.name) == 0) {
                    report_err("E_IR_TYPE_MISMATCH",
                               "Action '" + act.name + "' StoreOp attempts to write to read-only InPort '" +
                                   store->target.name + "'.",
                               line);
                }
            } else if (const auto* pw = std::get_if<ir::PortWriteOp>(&inst.op)) {
                if (!pw->port_name.empty() && in_only_ports.count(pw->port_name) != 0) {
                    report_err(
                        "E_IR_TYPE_MISMATCH",
                        "Action '" + act.name + "' PortWriteOp targets read-only InPort '" + pw->port_name + "'.",
                        line);
                }
            } else if (const auto* call = std::get_if<ir::ActionCallOp>(&inst.op)) {
                if (call->result_target.has_value() && !call->result_target->name.empty() &&
                    in_only_ports.count(call->result_target->name) != 0 &&
                    seen_vars.count(call->result_target->name) == 0) {
                    report_err("E_IR_TYPE_MISMATCH",
                               "Action '" + act.name + "' ActionCallOp '" + call->function_name +
                                   "' attempts to store result into read-only InPort '" + call->result_target->name +
                                   "'.",
                               line);
                }
                if (!declared_services.empty()) {
                    auto it = declared_services.find(call->function_name);
                    if (it == declared_services.end()) {
                        report_err(
                            "E_IR_TYPE_MISMATCH",
                            "Action '" + act.name + "' invokes undeclared service '" + call->function_name + "'.",
                            line);
                    } else {
                        const auto* svc = it->second;
                        if (call->arguments.size() != svc->parameters.size()) {
                            report_err("E_IR_TYPE_MISMATCH",
                                       "Action '" + act.name + "' calls service '" + call->function_name + "' with " +
                                           std::to_string(call->arguments.size()) + " argument(s), expected " +
                                           std::to_string(svc->parameters.size()) + ".",
                                       line);
                        }
                        if (call->result_target.has_value() && !call->result_target->empty() && svc->is_void_return()) {
                            report_err("E_IR_TYPE_MISMATCH",
                                       "Action '" + act.name + "' assigns result of void service '" +
                                           call->function_name + "' to '" + call->result_target->full_path() + "'.",
                                       line);
                        }
                    }
                }
            }
        }
    };

    for (const auto& tr : ir.transitions) {
        if (tr.condition_action.has_value()) {
            check_action(*tr.condition_action, tr.source_line);
        }
        if (tr.transition_action.has_value()) {
            check_action(*tr.transition_action, tr.source_line);
        }
    }
    for (const auto& st : ir.states) {
        for (const auto& act : st.entry_actions) {
            check_action(act, st.source_line);
        }
        for (const auto& act : st.exit_actions) {
            check_action(act, st.source_line);
        }
    }

    return ok;
}

}  // namespace fsm::middleend::passes
