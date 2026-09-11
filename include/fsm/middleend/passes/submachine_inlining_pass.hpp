/**
 * @file submachine_inlining_pass.hpp
 * @brief Submachine statechart resolution and inlining pass.
 */

#pragma once

#include <functional>
#include <string>
#include <utility>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class SubmachineInliningPass
 * @brief Target-Agnostic Middle-End Pass: Splicing and inlining of modular Submachine Statecharts.
 */
class SubmachineInliningPass {
  public:
    using SubmachineResolver = std::function<const FsmIr*(const std::string& submachine_name)>;

    explicit SubmachineInliningPass(SubmachineResolver resolver = nullptr) : resolver_(std::move(resolver)) {}

    [[nodiscard]] static std::string name() { return "SubmachineInlining"; }
    [[nodiscard]] static std::string description() {
        return "Resolves and inlines modular submachine statecharts into parent composite states";
    }

    void set_resolver(SubmachineResolver resolver) { resolver_ = std::move(resolver); }

    /**
     * @brief Traverses SubmachineRef nodes and inlines their statecharts into the parent model.
     * @param ir Target FsmIr to expand.
     * @param diag Diagnostic engine for reporting missing submachines.
     * @return True on success, false on unresolved submachines or cyclic references.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);

  private:
    SubmachineResolver resolver_;
};

}  // namespace fsm::middleend::passes
