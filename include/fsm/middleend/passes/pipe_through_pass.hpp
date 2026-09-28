/**
 * @file pipe_through_pass.hpp
 * @brief External process Unix filter pipeline pass.
 */

#pragma once

#include <string>
#include <utility>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class PipeThroughPass
 * @brief Middle-End Open Toolchain Pass: External Unix Filter Pipe.
 */
class PipeThroughPass {
  public:
    explicit PipeThroughPass(std::string command) : command_(std::move(command)) {}

    [[nodiscard]] static std::string name() { return "PipeThrough"; }
    [[nodiscard]] static std::string description() {
        return "Filters and transforms IR by piping JSON representation through an external command";
    }

    /**
     * @brief Serializes IR to JSON, pipes through child process stdout/stdin, and re-parses.
     * @param ir Target FsmIr to filter.
     * @param diag Diagnostic engine for error reporting.
     * @return True on successful process termination and valid JSON output, false otherwise.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);

  private:
    std::string command_;
};

}  // namespace fsm::middleend::passes
