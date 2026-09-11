#include "fsm/middleend/analysis/semantic_analyzer.hpp"

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool SemanticAnalyzer::validate(const FsmIr& ir, std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    bool valid = true;

    // 0. Concurrency Semantics Validation
    if (!ir.concurrency.is_valid()) {
        errors.push_back(
            "Semantic contradiction in concurrency configuration: SynchronousReactive clock semantics incompatible "
            "with Interleaved orthogonal resolution.");
        valid = false;
    }

    auto is_known_data_type = [&](const DataType& dt) -> bool {
        if (dt.is_primitive()) {
            return true;
        }
        const auto& name = dt.custom_name();
        if (name.empty()) {
            return true;
        }
        return ir.has_type(name);
    };

    // 1. Verify Variable Types
    for (const auto& var : ir.variables) {
        if (!is_known_data_type(var.type)) {
            errors.push_back("Variable '" + var.name + "' references unknown type '" + var.type.to_canonical_string() +
                             "'");
            valid = false;
        }
    }

    // 2. Verify Port Types
    for (const auto& port : ir.ports) {
        if (!is_known_data_type(port.type)) {
            errors.push_back("Port '" + port.name + "' references unknown type '" + port.type.to_canonical_string() +
                             "'");
            valid = false;
        }
    }

    // 3. Verify Signal Attribute Types
    for (const auto& sig : ir.signals) {
        for (const auto& attr : sig.attributes) {
            if (!is_known_data_type(attr.type)) {
                errors.push_back("Signal '" + sig.name + "' attribute '" + attr.name + "' references unknown type '" +
                                 attr.type.to_canonical_string() + "'");
                valid = false;
            }
        }
    }

    // 4. Verify Struct Field Types in custom_types
    for (const auto& ct : ir.custom_types) {
        if (ct.kind == TypeKind::Struct) {
            for (const auto& f : ct.fields) {
                if (!is_known_data_type(f.type)) {
                    errors.push_back("Type '" + ct.name + "' field '" + f.name + "' references unknown type '" +
                                     f.type.to_canonical_string() + "'");
                    valid = false;
                }
            }
        }
    }

    // 5. Verify Assignments in transitions
    auto check_assignment = [&](const ActionAssignment& assign, const std::string& ctx) {
        if (assign.target.name.empty())
            return;

        const auto* var = ir.find_variable(assign.target.name);
        const auto* port = ir.find_port(assign.target.name);

        if (var == nullptr && port == nullptr) {
            errors.push_back("Assignment target '" + assign.target.name + "' in " + ctx +
                             " not found in variables or ports");
            valid = false;
            return;
        }

        if (port != nullptr && port->is_in()) {
            errors.push_back("Cannot assign to read-only InPort '" + assign.target.name + "' in " + ctx);
            valid = false;
            return;
        }

        // Elementary type mismatch check if expr_ast is present
        if (assign.expr_ast.has_value()) {
            const DataType* target_type = var ? &var->type : (port ? &port->type : nullptr);
            if (target_type != nullptr) {
                const auto& ast = *assign.expr_ast;
                bool is_target_bool = target_type->is_boolean();
                bool is_target_numeric = target_type->is_integer() || target_type->is_floating_point();

                if (is_target_bool) {
                    if (ast.kind == ExpressionKind::IntegerLiteral || ast.kind == ExpressionKind::FloatLiteral) {
                        warnings.push_back("Type mismatch in assignment to boolean target '" + assign.target.name +
                                           "': assigned numeric literal");
                    } else if (ast.kind == ExpressionKind::EnumLiteral) {
                        warnings.push_back("Type mismatch in assignment to boolean target '" + assign.target.name +
                                           "': assigned enum literal");
                    }
                } else if (is_target_numeric) {
                    if (ast.kind == ExpressionKind::BooleanLiteral) {
                        warnings.push_back("Type mismatch in assignment to numeric target '" + assign.target.name +
                                           "': assigned boolean literal");
                    }
                }
            }
        }
    };

    for (const auto& tr : ir.transitions) {
        if (tr.condition_action.has_value()) {
            for (const auto& assign : tr.condition_action->assignments) {
                check_assignment(assign, "transition '" + tr.source + " -> " + tr.target + "'");
            }
        }
        if (tr.transition_action.has_value()) {
            for (const auto& assign : tr.transition_action->assignments) {
                check_assignment(assign, "transition '" + tr.source + " -> " + tr.target + "'");
            }
        }
    }

    return valid;
}

SemanticAnalysisResult SemanticAnalyzer::analyze(const FsmIr& ir) {
    SemanticAnalysisResult res;
    res.valid = validate(ir, res.errors, res.warnings);
    return res;
}

bool SemanticAnalyzer::validate(const FsmIr& ir, DiagnosticEngine& diag) {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok = validate(ir, errors, warnings);
    for (const auto& err : errors) {
        diag.report(Diagnostic::error("E0501", err));
    }
    for (const auto& warn : warnings) {
        diag.report(Diagnostic::warning("W0501", warn));
    }
    return ok;
}

}  // namespace fsm::middleend::analysis
