#include "fsm/middleend/passes/pipe_through_pass.hpp"

#if defined(_WIN32)
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <array>
#include <cstdio>
#include <cstdlib>

#include "fsm/ir/fsm_ir_deserializer.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool PipeThroughPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    if (command_.empty()) {
        diag.report(Diagnostic::error("E_PIPE_EMPTY", "PipeThroughPass: empty command specified."));
        return false;
    }

#if defined(_WIN32)
    std::string input_json = ir::FsmIrSerializer::serialize_json(ir);

    std::error_code ec;
    auto temp_dir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        temp_dir = std::filesystem::current_path();
    }
    auto timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto in_file = temp_dir / ("fsmc_pipe_in_" + timestamp + ".json");
    auto out_file = temp_dir / ("fsmc_pipe_out_" + timestamp + ".json");

    {
        std::ofstream ofs(in_file);
        if (!ofs.is_open()) {
            diag.report(Diagnostic::error("E_PIPE_FAIL", "Failed to create input file for external pass filter."));
            return false;
        }
        ofs << input_json;
    }

    std::string full_cmd = command_ + " < \"" + in_file.string() + "\" > \"" + out_file.string() + "\"";
    int ret = std::system(full_cmd.c_str());
    std::filesystem::remove(in_file, ec);

    if (ret != 0) {
        std::filesystem::remove(out_file, ec);
        diag.report(Diagnostic::error(
            "E_PIPE_EXEC", "External command '" + command_ + "' failed with exit code: " + std::to_string(ret)));
        return false;
    }

    std::string output_json;
    {
        std::ifstream ifs(out_file);
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        output_json = buffer.str();
    }
    std::filesystem::remove(out_file, ec);

    FsmIr transformed_ir;
    std::string parse_err;
    if (!ir::FsmIrDeserializer::deserialize_json(output_json, transformed_ir, parse_err)) {
        diag.report(Diagnostic::error(
            "E_PIPE_PARSE", "Failed to parse JSON output from external command '" + command_ + "': " + parse_err));
        return false;
    }

    ir = std::move(transformed_ir);
    diag.report(Diagnostic::info("I_PIPE_SUCCESS", "Successfully transformed IR via external filter: " + command_));
    return true;
#else
    std::string input_json = ir::FsmIrSerializer::serialize_json(ir);

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
    if (!ir::FsmIrDeserializer::deserialize_json(output_json, transformed_ir, parse_err)) {
        diag.report(Diagnostic::error(
            "E_PIPE_PARSE", "Failed to parse JSON output from external command '" + command_ + "': " + parse_err));
        return false;
    }

    ir = std::move(transformed_ir);
    diag.report(Diagnostic::info("I_PIPE_SUCCESS", "Successfully transformed IR via external filter: " + command_));
    return true;
#endif
}

}  // namespace fsm::middleend::passes
