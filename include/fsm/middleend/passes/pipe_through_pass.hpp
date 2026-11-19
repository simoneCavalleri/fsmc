/**
 * @file pipe_through_pass.hpp
 * @brief External process Unix filter pipeline pass.
 */

#pragma once

#include <string>
#include <utility>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class PipeThroughPass
 * @brief Middle-End Open Toolchain Pass: External Unix Filter Pipe.
 */
class PipeThroughPass : public ::fsm::middleend::IPass {
  public:
    explicit PipeThroughPass(std::string command) : command_(std::move(command)) {}

    /**
     * @brief Serializes IR to JSON, pipes through child process stdout/stdin, and re-parses.
     * @param ir Target FsmIr to filter.
     * @param diag Diagnostic engine for error reporting.
     * @return True on successful process termination and valid JSON output, false otherwise.
     */
    [[nodiscard]] std::string name() const override { return "pipe-through"; }
    [[nodiscard]] std::string description() const override {
        return "Pipes serialized Canonical IR JSON through an external filter command.";
    }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;

  private:
    std::string command_;
};

}  // namespace fsm::middleend::passes
