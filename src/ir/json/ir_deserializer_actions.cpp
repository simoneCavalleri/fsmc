#include "fsm/ir/identifier_utils.hpp"
#include "ir_deserializer_internal.hpp"

namespace fsm::ir::json_detail {

ActionAssignment parse_assignment_object(const JsonValue& a_val) {
    std::string scoped_tgt = a_val.get_string("scoped_target");
    std::string tgt_str = !scoped_tgt.empty() ? scoped_tgt : a_val.get_string("target", a_val.get_string("variable"));
    LValueTarget tgt(tgt_str);
    std::string scope_str = a_val.get_string("scope");
    if (scope_str == "Register") {
        tgt.scope = LValueScope::Register;
    } else if (scope_str == "OutPort") {
        tgt.scope = LValueScope::OutPort;
    } else if (scope_str == "Local") {
        tgt.scope = LValueScope::Local;
    }
    std::string op_str = a_val.get_string("op", "=");
    std::string expr = a_val.get_string("expression");
    return ActionAssignment(std::move(tgt), std::move(expr), string_to_assignment_op(op_str));
}

ActionSignature parse_action_signature_object(const JsonValue& act_val) {
    ActionSignature sig;
    sig.name = sanitize_identifier(act_val.get_string("name"));
    sig.invocation = act_val.get_string("invocation", sig.name);
    if (const auto* ae = act_val.get_child("accepts_event")) {
        if (ae->type == JsonType::Bool) {
            sig.accepts_event = ae->bool_val;
        }
    }
    if (const auto* assigns_val = act_val.get_child("assignments")) {
        if (assigns_val->is_array()) {
            for (const auto& a_val : assigns_val->arr_val) {
                if (a_val.is_object()) {
                    sig.assignments.push_back(parse_assignment_object(a_val));
                }
            }
        }
    }
    if (const auto* insts_val = act_val.get_child("instructions")) {
        if (insts_val->is_array()) {
            auto parse_str_vec = [](const JsonValue* arr_node) -> std::vector<std::string> {
                std::vector<std::string> res;
                if (arr_node != nullptr && arr_node->is_array()) {
                    for (const auto& elem : arr_node->arr_val) {
                        if (elem.is_string()) {
                            res.push_back(elem.str_val);
                        }
                    }
                }
                return res;
            };
            auto parse_scope = [](const std::string& scope_str) -> LValueScope {
                if (scope_str == "OutPort") {
                    return LValueScope::OutPort;
                }
                if (scope_str == "Local") {
                    return LValueScope::Local;
                }
                return LValueScope::Register;
            };
            for (const auto& i_val : insts_val->arr_val) {
                if (!i_val.is_object()) {
                    continue;
                }
                std::string kind = i_val.get_string("kind");
                std::string desc = i_val.get_string("description");
                if (kind == "store") {
                    StoreOp op;
                    op.target = LValueTarget(i_val.get_string("target"));
                    std::string sc = i_val.get_string("scope");
                    if (!sc.empty()) {
                        op.target.scope = parse_scope(sc);
                    }
                    op.op = string_to_assignment_op(i_val.get_string("op", "="));
                    op.expression = i_val.get_string("expression");
                    if (const auto* has_ast = i_val.get_child("has_expr_ast");
                        has_ast != nullptr && has_ast->type == JsonType::Bool && has_ast->bool_val) {
                        op.expr_ast = ExpressionAstNode::parse(op.expression);
                    }
                    sig.instructions.emplace_back(std::move(op), std::move(desc));
                } else if (kind == "port_write") {
                    PortWriteOp op;
                    op.port_name = i_val.get_string("port_name");
                    op.expression = i_val.get_string("expression");
                    if (const auto* il = i_val.get_child("is_latched"); il != nullptr && il->type == JsonType::Bool) {
                        op.is_latched = il->bool_val;
                    }
                    if (const auto* has_ast = i_val.get_child("has_expr_ast");
                        has_ast != nullptr && has_ast->type == JsonType::Bool && has_ast->bool_val) {
                        op.expr_ast = ExpressionAstNode::parse(op.expression);
                    }
                    sig.instructions.emplace_back(std::move(op), std::move(desc));
                } else if (kind == "port_read") {
                    PortReadOp op;
                    op.port_name = i_val.get_string("port_name");
                    op.destination = LValueTarget(i_val.get_string("destination"));
                    std::string sc = i_val.get_string("scope");
                    if (!sc.empty()) {
                        op.destination.scope = parse_scope(sc);
                    }
                    sig.instructions.emplace_back(std::move(op), std::move(desc));
                } else if (kind == "signal_emit") {
                    SignalEmitOp op;
                    op.signal_name = i_val.get_string("signal_name");
                    op.arguments = parse_str_vec(i_val.get_child("arguments"));
                    if (const auto* tp = i_val.get_child("target_port"); tp != nullptr && tp->is_string()) {
                        op.target_port = tp->str_val;
                    }
                    sig.instructions.emplace_back(std::move(op), std::move(desc));
                } else if (kind == "action_call") {
                    ActionCallOp op;
                    op.function_name = i_val.get_string("function_name");
                    op.arguments = parse_str_vec(i_val.get_child("arguments"));
                    const std::string res_tgt = i_val.get_string("result_target");
                    if (!res_tgt.empty()) {
                        LValueTarget tgt(res_tgt);
                        const std::string res_scope = i_val.get_string("result_scope");
                        if (res_scope == "OutPort") {
                            tgt.scope = LValueScope::OutPort;
                        } else if (res_scope == "Register") {
                            tgt.scope = LValueScope::Register;
                        } else if (res_scope == "Local") {
                            tgt.scope = LValueScope::Local;
                        }
                        op.result_target = std::move(tgt);
                    }
                    op.read_set = parse_str_vec(i_val.get_child("read_set"));
                    op.write_set = parse_str_vec(i_val.get_child("write_set"));
                    sig.instructions.emplace_back(std::move(op), std::move(desc));
                }
            }
        }
    }
    return sig;
}

}  // namespace fsm::ir::json_detail
