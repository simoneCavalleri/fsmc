#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/action.hpp"
#include "fsm/ir/expression.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

/**
 * @brief Analysis result structure holding semantic validity status and diagnostics.
 */
struct SemanticAnalysisResult {
    bool valid{true};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/**
 * @brief Middle-End Semantic Analyzer and Type Checker for Canonical FsmIr.
 *
 * Performs static semantic validation on the state machine datapath and actions:
 * 1. Type resolution: All variable, port, signal attribute, and struct field types
 *    must resolve to built-in primitives or registered compound types (`custom_types`).
 * 2. Datapath integrity: Assignment targets must exist in `variables` or `ports`.
 * 3. Access control: Prohibits writing to read-only InPorts.
 * 4. Action Type Checking: Analyzes algebraic expression ASTs against destination registers.
 */
class SemanticAnalyzer {
  public:
    /**
     * @brief Validates datapath and type semantics, populating error and warning vectors.
     */
    static bool validate(const FsmIr& ir, std::vector<std::string>& errors,
                         std::vector<std::string>& warnings) {
        bool valid = true;

        auto is_known_primitive = [](std::string_view t) -> bool {
            return (t == "bool" || t == "int" || t == "int8_t" || t == "uint8_t" || t == "int16_t" ||
                    t == "uint16_t" || t == "int32_t" || t == "uint32_t" || t == "int64_t" || t == "uint64_t" ||
                    t == "float" || t == "double" || t == "size_t" || t == "std::string" || t == "string" ||
                    t == "char" || t == "void" || t == "auto");
        };

        auto is_known_type = [&](std::string_view t) -> bool {
            if (t.empty())
                return true;
            std::string clean(t);
            if (clean.starts_with("std::optional<") && clean.ends_with(">")) {
                clean = clean.substr(14, clean.size() - 15);
            }
            if (clean.starts_with("const ")) {
                clean = clean.substr(6);
            }
            while (!clean.empty() && (clean.back() == '&' || clean.back() == '*')) {
                clean.pop_back();
            }
            while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.back())) != 0) {
                clean.pop_back();
            }
            while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.front())) != 0) {
                clean.erase(clean.begin());
            }
            if (clean.empty())
                return true;
            if (is_known_primitive(clean))
                return true;
            return ir.has_type(clean);
        };

        // 1. Verify Variable Types
        for (const auto& var : ir.variables) {
            if (!var.type.empty() && !is_known_type(var.type)) {
                errors.push_back("Variable '" + var.name + "' references unknown type '" + var.type + "'");
                valid = false;
            }
        }

        // 2. Verify Port Types
        for (const auto& port : ir.ports) {
            if (!port.type.empty() && !is_known_type(port.type)) {
                errors.push_back("Port '" + port.name + "' references unknown type '" + port.type + "'");
                valid = false;
            }
        }

        // 3. Verify Signal Attribute Types
        for (const auto& sig : ir.signals) {
            for (const auto& attr : sig.attributes) {
                if (!attr.type.empty() && !is_known_type(attr.type)) {
                    errors.push_back("Signal '" + sig.name + "' attribute '" + attr.name +
                                     "' references unknown type '" + attr.type + "'");
                    valid = false;
                }
            }
        }

        // 4. Verify Struct Field Types in custom_types
        for (const auto& ct : ir.custom_types) {
            if (ct.kind == TypeKind::Struct) {
                for (const auto& f : ct.fields) {
                    if (!f.type.empty() && !is_known_type(f.type)) {
                        errors.push_back("Type '" + ct.name + "' field '" + f.name +
                                         "' references unknown type '" + f.type + "'");
                        valid = false;
                    }
                }
            }
        }

        // 5. Verify Assignments in transitions
        auto check_assignment = [&](const ActionAssignment& assign, const std::string& ctx) {
            if (assign.target_variable.empty())
                return;

            const auto* var = ir.find_variable(assign.target_variable);
            const auto* port = ir.find_port(assign.target_variable);

            if (var == nullptr && port == nullptr) {
                errors.push_back("Assignment target '" + assign.target_variable + "' in " + ctx +
                                 " not found in variables or ports");
                valid = false;
                return;
            }

            if (port != nullptr && port->is_in()) {
                errors.push_back("Cannot assign to read-only InPort '" + assign.target_variable + "' in " + ctx);
                valid = false;
                return;
            }

            // Elementary type mismatch check if expr_ast is present
            if (assign.expr_ast.has_value()) {
                std::string target_type = var ? var->type : (port ? port->type : "");
                if (!target_type.empty()) {
                    const auto& ast = *assign.expr_ast;
                    bool is_target_bool = (target_type == "bool");
                    bool is_target_numeric =
                        (target_type == "int" || target_type == "int8_t" || target_type == "uint8_t" ||
                         target_type == "int16_t" || target_type == "uint16_t" || target_type == "int32_t" ||
                         target_type == "uint32_t" || target_type == "int64_t" || target_type == "uint64_t" ||
                         target_type == "float" || target_type == "double");

                    if (is_target_bool) {
                        if (ast.kind == ExpressionKind::IntegerLiteral || ast.kind == ExpressionKind::FloatLiteral) {
                            warnings.push_back("Type mismatch in assignment to boolean target '" +
                                               assign.target_variable + "': assigned numeric literal");
                        } else if (ast.kind == ExpressionKind::EnumLiteral) {
                            warnings.push_back("Type mismatch in assignment to boolean target '" +
                                               assign.target_variable + "': assigned enum literal");
                        }
                    } else if (is_target_numeric) {
                        if (ast.kind == ExpressionKind::BooleanLiteral) {
                            warnings.push_back("Type mismatch in assignment to numeric target '" +
                                               assign.target_variable + "': assigned boolean literal");
                        }
                    }
                }
            }
        };

        for (const auto& tr : ir.transitions) {
            if (tr.action_sig.has_value()) {
                for (const auto& assign : tr.action_sig->assignments) {
                    check_assignment(assign, "transition '" + tr.source + " -> " + tr.target + "'");
                }
            }
        }

        return valid;
    }

    /**
     * @brief Performs semantic analysis returning a structured result object.
     */
    static SemanticAnalysisResult analyze(const FsmIr& ir) {
        SemanticAnalysisResult res;
        res.valid = validate(ir, res.errors, res.warnings);
        return res;
    }

    /**
     * @brief Validates semantics and reports findings directly to DiagnosticEngine.
     */
    static bool validate(const FsmIr& ir, DiagnosticEngine& diag) {
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
};

}  // namespace fsm::middleend::analysis

namespace fsm::middleend {
using analysis::SemanticAnalyzer;
using analysis::SemanticAnalysisResult;
}  // namespace fsm::middleend
