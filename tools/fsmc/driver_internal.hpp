#pragma once

#include <memory>
#include <optional>
#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "tools/fsmc/fsmc_options.hpp"

namespace fsm::tools::driver_detail {

std::optional<int> handle_auxiliary_modes(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                                          const fsm::diagnostic::DiagnosticEngine& middleend_diag);

int emit_cpp_target(const FsmcOptions& opts, const std::string& content, fsm::ir::FsmIr& model,
                    const fsm::frontend::IParser& parser, bool has_sidecar, fsm::diagnostic::DiagnosticFormat diag_fmt);

bool is_nuxmv_available() noexcept;
void perform_req_audit(const fsm::ir::FsmIr& model, std::ostream& out = std::cout);
int run_nuxmv_verification(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                           const fsm::diagnostic::DiagnosticEngine& diag_engine);
int print_verification_report(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                              const fsm::diagnostic::DiagnosticEngine& diag_engine);

}  // namespace fsm::tools::driver_detail
