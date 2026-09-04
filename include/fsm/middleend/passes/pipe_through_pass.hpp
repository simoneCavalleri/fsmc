#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "fsm/backend/diagram/json_serializer.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Middle-End Open Toolchain Pass: External Unix Filter Pipe.
 *
 * Serializes FsmIr into JSON representation, streams it to an external process via
 * stdin, and captures transformed JSON from stdout to update the IR. Enables seamless
 * integration of custom scripts (Python, Rust, shell) into the compiler pipeline.
 */
class PipeThroughPass {
  public:
    explicit PipeThroughPass(std::string command) : command_(std::move(command)) {}

    [[nodiscard]] static std::string name() { return "PipeThrough"; }
    [[nodiscard]] static std::string description() {
        return "Filters and transforms IR by piping JSON representation through an external command";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        if (command_.empty()) {
            diag.report(Diagnostic::error("E_PIPE_EMPTY", "PipeThroughPass: empty command specified."));
            return false;
        }

        std::string input_json = JsonSerializer::serialize(ir);

        int in_pipe[2];   // Parent writes, child reads
        int out_pipe[2];  // Child writes, parent reads

        if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
            diag.report(Diagnostic::error("E_PIPE_FAIL", "Failed to create IPC pipes for external pass filter."));
            return false;
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
            close(out_pipe[0]);
            close(out_pipe[1]);
            diag.report(Diagnostic::error("E_FORK_FAIL", "Failed to fork subprocess for external pass filter."));
            return false;
        }

        if (pid == 0) {
            // Child process
            close(in_pipe[1]);
            close(out_pipe[0]);

            dup2(in_pipe[0], STDIN_FILENO);
            dup2(out_pipe[1], STDOUT_FILENO);

            close(in_pipe[0]);
            close(out_pipe[1]);

            execl("/bin/sh", "sh", "-c", command_.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }

        // Parent process
        close(in_pipe[0]);
        close(out_pipe[1]);

        // Write input JSON to child stdin
        size_t written = 0;
        while (written < input_json.size()) {
            ssize_t n = write(in_pipe[1], input_json.data() + written, input_json.size() - written);
            if (n <= 0)
                break;
            written += static_cast<size_t>(n);
        }
        close(in_pipe[1]);

        // Read output JSON from child stdout
        std::string output_json;
        std::array<char, 4096> buf;
        ssize_t n_read = 0;
        while ((n_read = read(out_pipe[0], buf.data(), buf.size())) > 0) {
            output_json.append(buf.data(), static_cast<size_t>(n_read));
        }
        close(out_pipe[0]);

        int status = 0;
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            diag.report(Diagnostic::error("E_PIPE_EXEC", "External command '" + command_ + "' failed with exit code: " +
                                                             std::to_string(WEXITSTATUS(status))));
            return false;
        }

        // Parse transformed JSON back into FsmIr
        FsmIr transformed_ir;
        std::string parse_err;
        JsonStateParser parser;
        if (!parser.parse(output_json, transformed_ir, parse_err)) {
            diag.report(Diagnostic::error(
                "E_PIPE_PARSE", "Failed to parse JSON output from external command '" + command_ + "': " + parse_err));
            return false;
        }

        ir = std::move(transformed_ir);
        diag.report(Diagnostic::info("I_PIPE_SUCCESS", "Successfully transformed IR via external filter: " + command_));
        return true;
    }

  private:
    std::string command_;
};

}  // namespace fsm::codegen
