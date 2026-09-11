#include "fsm/backend/cpp/cpp_backend_validator.hpp"

namespace fsm::backend::cpp {

bool CppBackendValidator::validate_model(const ir::FsmIr& model, diagnostic::DiagnosticEngine& diagnostics) {
    bool valid = true;

    for (const auto& state : model.states) {
        std::string code;
        std::string message;
        switch (state.kind) {
            case ir::StateKind::Parallel:
                code = "ECPP001";
                message = "parallel state '" + state.name +
                          "' must be lowered with OrthogonalProductPass before C++ emission";
                break;
            case ir::StateKind::Fork:
                code = "ECPP002";
                message = "fork pseudostate '" + state.name + "' must be lowered before C++ emission";
                break;
            case ir::StateKind::Join:
                code = "ECPP003";
                message = "join pseudostate '" + state.name + "' must be lowered before C++ emission";
                break;
            case ir::StateKind::Choice:
            case ir::StateKind::Junction:
                code = "ECPP004";
                message = "choice/junction pseudostate '" + state.name + "' must be inlined before C++ emission";
                break;
            case ir::StateKind::Terminate:
                code = "ECPP005";
                message = "terminate pseudostate '" + state.name + "' has no C++ runtime representation";
                break;
            default:
                break;
        }

        if (!code.empty()) {
            diagnostics.report(diagnostic::Diagnostic::error(code, std::move(message)));
            valid = false;
        }

        if (state.do_activity.has_value()) {
            diagnostics.report(diagnostic::Diagnostic::error(
                "ECPP006",
                "state '" + state.name + "' declares do_activity, which is not supported by the C++ runtime"));
            valid = false;
        }
    }

    for (const auto& transition : model.transitions) {
        if (transition.target_ids.size() > 1) {
            std::string id_str = transition.id.empty() ? (transition.source + "->" + transition.target) : transition.id;
            diagnostics.report(diagnostic::Diagnostic::error(
                "ECPP009", "multi-target transition '" + id_str +
                               "' must be lowered into product-state transition before C++ emission"));
            valid = false;
        }
        if (transition.source_ids.size() > 1) {
            std::string id_str = transition.id.empty() ? (transition.source + "->" + transition.target) : transition.id;
            diagnostics.report(diagnostic::Diagnostic::error(
                "ECPP010", "multi-source transition '" + id_str +
                               "' must be lowered into product-state transition before C++ emission"));
            valid = false;
        }
        if (std::holds_alternative<ir::ChangeTrigger>(transition.trigger)) {
            diagnostics.report(diagnostic::Diagnostic::error(
                "ECPP007",
                "change trigger on transition '" + transition.id + "' requires a sampled-state lowering pass"));
            valid = false;
        }
        if (std::holds_alternative<ir::TimeTrigger>(transition.trigger)) {
            const auto& time_trigger = std::get<ir::TimeTrigger>(transition.trigger);
            if (time_trigger.kind == ir::TimeTriggerKind::At || !time_trigger.dynamic_expression.empty()) {
                diagnostics.report(diagnostic::Diagnostic::error(
                    "ECPP008", "time trigger on transition '" + transition.id +
                                   "' requires unsupported absolute or dynamic timer lowering before C++ emission"));
                valid = false;
            }
        }
    }

    return valid;
}

}  // namespace fsm::backend::cpp
