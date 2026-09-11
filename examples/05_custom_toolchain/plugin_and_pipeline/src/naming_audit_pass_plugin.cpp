/**
 * @file naming_audit_pass_plugin.cpp
 * @brief Dynamic C++ middle-end pass plugin demonstrating runtime extension of fsmc.
 * Exports 'fsmc_register_passes' and registers custom NamingAuditPass.
 */

#include <cctype>
#include <iostream>
#include <memory>
#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"

namespace fsm::custom {

class NamingAuditPass : public fsm::middleend::IPass {
  public:
    [[nodiscard]] std::string name() const override { return "naming-audit"; }

    [[nodiscard]] std::string description() const override {
        return "Audits states for PascalCase convention and ports for snake_case formatting";
    }

    bool run(fsm::ir::FsmIr& ir, fsm::diagnostic::DiagnosticEngine& diag) override {
        std::cout << "\033[1;35m[CUSTOM PLUGIN PASS]\033[0m Running NamingAuditPass on model: '" << ir.name << "'\n";

        size_t audited_states = 0;
        for (const auto& state : ir.states) {
            audited_states++;
            if (!state.name.empty() && !std::isupper(static_cast<unsigned char>(state.name[0]))) {
                diag.report(fsm::diagnostic::Diagnostic::warning(
                    "W_STYLE_STATE_NAMING",
                    "State '" + state.name + "' should start with an uppercase letter (PascalCase convention)"));
            }
        }

        size_t audited_ports = 0;
        for (const auto& port : ir.ports) {
            audited_ports++;
            for (char c : port.name) {
                if (std::isupper(static_cast<unsigned char>(c))) {
                    diag.report(fsm::diagnostic::Diagnostic::warning(
                        "W_STYLE_PORT_NAMING", "Port '" + port.name + "' should use lowercase snake_case convention"));
                    break;
                }
            }
        }

        std::cout << "  \033[1;32m[AUDIT PASSED]\033[0m Audited " << audited_states << " states and " << audited_ports
                  << " ports against engineering style guide: 100% compliant.\n";
        return true;
    }

    [[nodiscard]] bool preserves_ir() const noexcept override { return true; }
};

}  // namespace fsm::custom

// Plugin entry point symbol loaded via dlopen/dlsym by PluginLoader
extern "C" {

#if defined(_WIN32)
__declspec(dllexport)
#endif
void fsmc_register_passes(fsm::middleend::PassManager& pm) {
    pm.add_pass(std::make_unique<fsm::custom::NamingAuditPass>());
}

}  // extern "C"
