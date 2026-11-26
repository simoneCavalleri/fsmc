#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tools/fsmc/driver_internal.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/middleend/analysis/counterexample_exporter.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "tools/common/file_utils.hpp"

namespace fsm::tools::driver_detail {

using namespace fsm::ir;
using namespace fsm::diagnostic;
using namespace fsm::middleend::analysis;
using namespace fsm::backend::formal;

bool is_nuxmv_available() noexcept {
    const char* path_env = std::getenv("PATH");
    if (!path_env) {
        return false;
    }
#if defined(_WIN32)
    const char delimiter = ';';
    const std::vector<std::string> candidates = {"nuXmv.exe", "nuXmv", "nuxmv.exe"};
#else
    const char delimiter = ':';
    const std::vector<std::string> candidates = {"nuXmv", "nuxmv"};
#endif
    std::string path_str(path_env);
    std::size_t start = 0;
    while (start < path_str.size()) {
        std::size_t end = path_str.find(delimiter, start);
        if (end == std::string::npos) {
            end = path_str.size();
        }
        std::string dir = path_str.substr(start, end - start);
        if (!dir.empty()) {
            std::error_code ec;
            fs::path dir_path(dir);
            for (const auto& cand : candidates) {
                fs::path full = dir_path / cand;
                if (fs::exists(full, ec) && fs::is_regular_file(full, ec)) {
#if !defined(_WIN32)
                    if (::access(full.c_str(), X_OK) == 0) {
                        return true;
                    }
#else
                    return true;
#endif
                }
            }
        }
        start = end + 1;
    }
    return false;
}

void perform_req_audit(const fsm::ir::FsmIr& model, std::ostream& out) {
    out << "============================================================================\n"
        << " Requirement Traceability Matrix (@fsm:req) : " << model.name << "\n"
        << "============================================================================\n";
    std::size_t req_count = 0;
    if (!model.satisfies_reqs.empty()) {
        out << " Model '" << model.name << "' -> Requirements: ";
        for (const auto& r : model.satisfies_reqs) {
            out << "[" << r << "] ";
            ++req_count;
        }
        out << "\n";
    }
    for (const auto& st : model.states) {
        if (!st.traceability_reqs.empty()) {
            out << " State '" << st.name << "' -> Requirements: ";
            for (const auto& r : st.traceability_reqs) {
                out << "[" << r << "] ";
                ++req_count;
            }
            out << "\n";
        }
    }
    for (const auto& t : model.transitions) {
        if (!t.traceability_reqs.empty()) {
            out << " Transition '" << t.source << " -> " << t.target << "' -> Requirements: ";
            for (const auto& r : t.traceability_reqs) {
                out << "[" << r << "] ";
                ++req_count;
            }
            out << "\n";
        }
    }
    for (const auto& prop : model.properties) {
        if (!prop.traceability_req.empty()) {
            out << " Formal Property '" << prop.name << "' -> Requirement: [" << prop.traceability_req << "]\n";
            ++req_count;
        }
    }
    out << " Total Requirements Mapped: " << req_count << "\n"
        << "============================================================================\n\n";
}

int run_nuxmv_verification(const FsmcOptions& opts, const fsm::ir::FsmIr& model, const DiagnosticEngine& diag_engine) {
    std::cout << "============================================================================\n"
              << " Formal Model Verification Report (Engine: nuXmv): " << model.name << "\n"
              << "============================================================================\n"
              << " Input File:       " << opts.input_file << "\n"
              << " States:           " << model.states.size() << "\n"
              << " Total Events:     " << model.signals.size() << "\n"
              << " Transitions:      " << model.transitions.size() << "\n"
              << " Formal Properties:" << model.properties.size() << "\n"
              << "----------------------------------------------------------------------------\n";

    // Serialize model to temporary SMV file in standard temp directory
    std::string smv_content = fsm::backend::formal::SmvSerializer::serialize(model);
    std::error_code ec;
    fs::path temp_dir = fs::temp_directory_path(ec);
    if (ec) {
        temp_dir = fs::current_path();
    }
    fs::path temp_smv_path =
        temp_dir /
        ("fsmc_verify_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".smv");
    std::string temp_smv = temp_smv_path.string();
    std::string write_err;
    if (!write_file_content(temp_smv, smv_content, write_err)) {
        std::cerr << "[ERROR] Could not write temporary SMV file '" << temp_smv << "': " << write_err << "\n";
        return print_verification_report(opts, model, diag_engine);
    }

    // Direct process execution without shell (/bin/sh or cmd.exe) to prevent command injection.
    // Hardcoded trusted executable names are used with system PATH resolution to prevent uncontrolled process
    // operation.
    std::string nuxmv_output;
    int exit_code = -1;
#if !defined(_WIN32)
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        std::cerr << "[ERROR] Failed to create IPC pipe for nuXmv execution.\n";
        fs::remove(temp_smv_path, ec);
        return print_verification_report(opts, model, diag_engine);
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        std::cerr << "[ERROR] Failed to fork process for nuXmv execution.\n";
        fs::remove(temp_smv_path, ec);
        return print_verification_report(opts, model, diag_engine);
    }

    if (pid == 0) {
        // Child: redirect stdout and stderr to the write-end of the pipe
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        char* const argv_caps[] = {const_cast<char*>("nuXmv"), const_cast<char*>(temp_smv.c_str()), nullptr};
        execvp("nuXmv", argv_caps);

        char* const argv_lower[] = {const_cast<char*>("nuxmv"), const_cast<char*>(temp_smv.c_str()), nullptr};
        execvp("nuxmv", argv_lower);

        _exit(127);
    }

    // Parent: read output from child pipe with configurable timeout (FSMC_NUXMV_TIMEOUT_SEC, default 60s)
    close(pipe_fds[1]);
    int timeout_sec = 60;
    if (const char* env_timeout = std::getenv("FSMC_NUXMV_TIMEOUT_SEC"); env_timeout != nullptr) {
        int parsed = std::atoi(env_timeout);
        if (parsed > 0) {
            timeout_sec = parsed;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    bool timed_out = false;
    char out_buf[512];
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            timed_out = true;
            break;
        }
        const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd pfd{};
        pfd.fd = pipe_fds[0];
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, static_cast<int>(std::min<long long>(remaining_ms, 1000)));
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pr == 0) {
            continue;
        }
        ssize_t bytes_read = read(pipe_fds[0], out_buf, sizeof(out_buf) - 1);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (bytes_read == 0) {
            break;
        }
        out_buf[bytes_read] = '\0';
        nuxmv_output += out_buf;
    }
    close(pipe_fds[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (timed_out) {
        fs::remove(temp_smv_path, ec);
        std::cerr << "[ERROR] nuXmv verification timed out after " << timeout_sec << " seconds.\n";
        return 1;
    }
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (exit_code == 127 && nuxmv_output.empty()) {
        fs::remove(temp_smv_path, ec);
        std::cerr << "[WARNING W0401] nuXmv binary ('nuXmv') not found in PATH.\n"
                  << "Falling back to embedded formal verification engine (ModelChecker).\n\n";
        return print_verification_report(opts, model, diag_engine);
    }
#else
    HANDLE h_read = NULL;
    HANDLE h_write = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    bool process_started = false;
    if (CreatePipe(&h_read, &h_write, &sa, 0)) {
        SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdOutput = h_write;
        si.hStdError = h_write;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        const char* candidates[] = {"nuXmv.exe", "nuXmv", "nuxmv.exe"};
        for (const char* cand : candidates) {
            std::string cmd_line = std::string(cand) + " \"" + temp_smv + "\"";
            std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
            cmd_buf.push_back('\0');

            if (CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                process_started = true;
                break;
            }
        }

        if (process_started) {
            CloseHandle(h_write);
            h_write = NULL;

            char out_buf[512];
            DWORD dw_read = 0;
            while (ReadFile(h_read, out_buf, sizeof(out_buf) - 1, &dw_read, NULL) && dw_read > 0) {
                out_buf[dw_read] = '\0';
                nuxmv_output += out_buf;
            }

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD dw_exit = 0;
            if (GetExitCodeProcess(pi.hProcess, &dw_exit)) {
                exit_code = static_cast<int>(dw_exit);
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            if (h_write)
                CloseHandle(h_write);
        }
        if (h_read)
            CloseHandle(h_read);
    }

    if (!process_started) {
        fs::remove(temp_smv_path, ec);
        std::cerr << "[WARNING W0401] nuXmv binary ('nuXmv') not found in PATH.\n"
                  << "Falling back to embedded formal verification engine (ModelChecker).\n\n";
        return print_verification_report(opts, model, diag_engine);
    }
#endif
    fs::remove(temp_smv_path, ec);

    std::cout << "nuXmv Verification Output:\n";
    std::istringstream iss(nuxmv_output);
    std::string line;
    bool all_passed = (exit_code == 0);
    bool found_spec = false;
    while (std::getline(iss, line)) {
        if (line.find("-- specification") != std::string::npos) {
            found_spec = true;
            if (line.find("is true") != std::string::npos) {
                std::cout << "  [PASSED] " << line << "\n";
            } else if (line.find("is false") != std::string::npos) {
                std::cout << "  [VIOLATION] " << line << "\n";
                all_passed = false;
            } else {
                std::cout << "  " << line << "\n";
            }
        } else if (line.find("Trace Description:") != std::string::npos ||
                   line.find("Counterexample") != std::string::npos || line.find("-> State:") != std::string::npos) {
            std::cout << "    " << line << "\n";
        }
    }

    if (!found_spec) {
        std::cout << nuxmv_output;
    }

    const bool is_valid = !diag_engine.has_errors();
    if (opts.werror && diag_engine.has_warnings()) {
        std::cerr << "\n[ERROR] -Werror enabled: verification failed due to warnings.\n";
        all_passed = false;
    }

    std::cout << "----------------------------------------------------------------------------\n"
              << " nuXmv Status: "
              << (all_passed && is_valid ? "PASSED (Model Sound & Properties Formally Verified)"
                                         : "FAILED (Violations Detected)")
              << "\n============================================================================\n";

    return (all_passed && is_valid) ? 0 : 1;
}

int print_verification_report(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                              const DiagnosticEngine& diag_engine) {
    std::size_t total_deferred = 0;
    for (const auto& s : model.states) {
        total_deferred += s.deferred_events.size();
    }

    std::cout << "============================================================================\n"
              << " Formal Model Verification Report: " << model.name << "\n"
              << "============================================================================\n"
              << " Input File:       " << opts.input_file << "\n"
              << " States:           " << model.states.size() << "\n"
              << " Total Events:     " << model.signals.size() << "\n"
              << " Transitions:      " << model.transitions.size() << "\n"
              << " Choice Nodes:     " << model.choice_nodes.size() << "\n"
              << " Deferred Triggers:" << total_deferred << "\n"
              << "----------------------------------------------------------------------------\n"
              << " Diagnostics:\n";

    const auto& diagnostics = diag_engine.get_diagnostics();
    if (diagnostics.empty()) {
        std::cout << "  (No warnings or errors detected. Model is formally sound!)\n";
    } else {
        for (const auto& diag : diagnostics) {
            std::string level_tag = "[INFO]";
            if (diag.severity == DiagnosticSeverity::Warning) {
                level_tag = "[WARNING]";
            } else if (diag.severity == DiagnosticSeverity::SafetyCritical) {
                level_tag = "[SAFETY CRITICAL]";
            } else if (diag.severity == DiagnosticSeverity::Error) {
                level_tag = "[ERROR]";
            }
            std::cout << "  " << level_tag << " (" << (diag.code.empty() ? "Validation" : diag.code)
                      << "): " << diag.message << "\n";
        }
    }

    bool all_valid = !diag_engine.has_errors();
    if (opts.werror && diag_engine.has_warnings()) {
        std::cerr << "\n[ERROR] -Werror enabled: verification failed due to warnings.\n";
        all_valid = false;
    }

    if (!model.properties.empty()) {
        std::cout << "----------------------------------------------------------------------------\n"
                  << " Formal Temporal Properties (" << model.properties.size() << "):\n";
        ModelChecker checker(model);
        auto mc_results = checker.verify_all();
        for (const auto& res : mc_results) {
            if (res.passed) {
                std::cout << "  [PASSED] " << res.property_name << " (" << res.property_formula << ")\n";
            } else {
                all_valid = false;
                std::cout << "  [VIOLATION] " << res.property_name << " (" << res.property_formula << ")\n";
                if (!res.violation_reason.empty()) {
                    std::cout << "    Reason: " << res.violation_reason << "\n";
                }
                std::string ce = res.format_counterexample();
                if (!ce.empty()) {
                    std::cout << "  " << ce;
                }
                if (!opts.export_cex_gtest.empty() && !res.counterexample_trace.empty()) {
                    const std::string hdr_hint =
                        opts.output_file.empty() ? "" : std::filesystem::path(opts.output_file).filename().string();
                    std::string gtest_code =
                        CounterexampleGTestExporter::export_gtest(model, res, hdr_hint, opts.ns_name);
                    std::string write_err;
                    if (!write_file_content(opts.export_cex_gtest, gtest_code, write_err)) {
                        std::cerr << "Error exporting counterexample GTest: " << write_err << "\n";
                    } else {
                        std::cout << "  [EXPORTED] Counterexample GTest written to: " << opts.export_cex_gtest << "\n";
                    }
                }
            }
        }
    }

    std::cout << "----------------------------------------------------------------------------\n"
              << " Verification Status: "
              << (all_valid ? "PASSED (Model Sound & Properties Verified)" : "FAILED (Errors/Violations Detected)")
              << "\n"
              << "============================================================================\n";

    return all_valid ? 0 : 1;
}

}  // namespace fsm::tools::driver_detail
