/**
 * @file test_pipeline_extensibility.cpp
 * @brief Integration tests for pipeline extensibility, external Unix pipe filters, and dynamic plugin loading.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/pipe_through_pass.hpp"
#include "fsm/middleend/plugin/plugin_loader.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::middleend::plugin;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify PipeThroughPass filters IR faithfully through an external Unix command.
 * @scenario Pipe FSM model through standard POSIX utility 'cat'.
 * @expected IR roundtrips with preserved states, transitions, and structural topology.
 */
TEST(PipeThroughPass, PosixFilterExecution_RoundtripsIrFidelity) {
    FsmIr model;
    model.name = "PipedModel";
    model.add_state("Active");
    model.add_state("Inactive");
    model.initial_state = "Active";

    TransitionEdge t;
    t.source = "Active";
    t.target = "Inactive";
    t.event = "PowerOff";
    model.add_transition(t);

    PipeThroughPass pass("cat");
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    EXPECT_EQ(model.name, "PipedModel");
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 1u);
}

/**
 * @brief Verify PluginLoader handles non-existent or invalid plugin files gracefully.
 * @scenario Attempt to load a non-existent shared library '/non/existent/path/to/plugin.so'.
 * @expected Function returns false and logs diagnostic error E_PLUGIN_LOAD.
 */
TEST(PluginLoader, MissingSharedObject_FailsGracefullyWithDiagnostic) {
    PassManager pm;
    DiagnosticEngine diag;
    PluginLoader loader;

    bool ok = loader.load_plugin("/non/existent/path/to/plugin.so", pm, diag);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.has_errors());
}

/**
 * @brief Verify PipeThroughPass error handling for empty command, failing exit code, invalid JSON, and >64KB payload.
 */
TEST(PipeThroughPass, ErrorPathsAndLargePayload_HandledGracefully) {
    FsmIr model;
    model.name = "PipeEdgeModel";
    model.initial_state = "S0";
    for (int i = 0; i < 450; ++i) {
        model.add_state("S" + std::to_string(i));
    }

    // 1. Empty command -> E_PIPE_EMPTY
    {
        DiagnosticEngine diag;
        PipeThroughPass empty_pass("");
        EXPECT_FALSE(empty_pass.run(model, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 2. Non-zero exit code -> E_PIPE_EXEC
    {
        DiagnosticEngine diag;
        PipeThroughPass fail_pass("exit 2");
        EXPECT_FALSE(fail_pass.run(model, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 3. Invalid JSON output -> E_PIPE_PARSE
    {
        DiagnosticEngine diag;
        PipeThroughPass bad_json_pass("echo not_valid_json");
        EXPECT_FALSE(bad_json_pass.run(model, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 4. Large >64KB payload roundtrip through cat (exercises async writer thread)
    {
        DiagnosticEngine diag;
        PipeThroughPass cat_pass("cat");
        EXPECT_TRUE(cat_pass.run(model, diag));
        EXPECT_FALSE(diag.has_errors());
        EXPECT_EQ(model.states.size(), 450u);
    }
}

/**
 * @brief Verify PluginLoader validation, dlopen/dlsym error handling, exception safety, and valid plugin loading.
 */
TEST(PluginLoader, ValidationErrorsExceptionsAndValidPlugin_HandledCorrectly) {
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::temp_directory_path() / "fsmc_plugin_loader_cov_test";
    fs::remove_all(tmp_dir);
    fs::create_directories(tmp_dir);

    PassManager pm;
    PluginLoader loader;

    // 1. Empty path -> E_PLUGIN_PATH_INVALID
    {
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin("", pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 2. Invalid extension on existing regular file -> E_PLUGIN_LOAD
    {
        const fs::path txt_file = tmp_dir / "not_a_lib.txt";
        {
            std::ofstream ofs(txt_file);
            ofs << "dummy";
        }
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(txt_file.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 3. Corrupted .so file -> dlopen failure (E_PLUGIN_LOAD)
    {
        const fs::path bad_so = tmp_dir / "corrupt.so";
        {
            std::ofstream ofs(bad_so);
            ofs << "not an elf shared object";
        }
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(bad_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

#if !defined(_WIN32)
    auto compile_so = [&](const std::string& name, const std::string& body) -> fs::path {
        const fs::path src = tmp_dir / (name + ".cpp");
        const fs::path so = tmp_dir / (name + ".so");
        {
            std::ofstream ofs(src);
            ofs << body;
        }
        const fs::path include_dir =
            fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "include";
        const std::string cmd = "g++ -std=c++20 -shared -fPIC -I\"" + include_dir.string() + "\" \"" + src.string() +
                                "\" -o \"" + so.string() + "\"";
        if (std::system(cmd.c_str()) != 0) {
            return {};
        }
        return so;
    };

    // 4. Valid .so without fsmc_register_passes symbol -> E_PLUGIN_SYMBOL
    {
        const fs::path no_sym_so = compile_so("no_sym", "extern \"C\" int other_fn() { return 42; }\n");
        ASSERT_FALSE(no_sym_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(no_sym_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 5. Valid .so whose fsmc_register_passes throws std::runtime_error -> E_PLUGIN_INIT
    {
        const fs::path throw_std_so = compile_so("throw_std", R"cpp(
#include <stdexcept>
            namespace fsm::middleend {
            class PassManager;
            }
            extern "C" void fsmc_register_passes(fsm::middleend::PassManager&) {
                throw std::runtime_error("boom");
            }
        )cpp");
        ASSERT_FALSE(throw_std_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(throw_std_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 6. Valid .so whose fsmc_register_passes throws unknown exception -> E_PLUGIN_INIT
    {
        const fs::path throw_unk_so = compile_so("throw_unk", R"cpp(
            namespace fsm::middleend {
            class PassManager;
            }
            extern "C" void fsmc_register_passes(fsm::middleend::PassManager&) {
                throw 123;
            }
        )cpp");
        ASSERT_FALSE(throw_unk_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(throw_unk_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 7. Valid .so registering cleanly -> I_PLUGIN_REGISTERED + unload_all()
    {
        const fs::path ok_so = compile_so("ok_plugin", R"cpp(
            namespace fsm::middleend {
            class PassManager;
            }
            extern "C" void fsmc_register_passes(fsm::middleend::PassManager&) {}
        )cpp");
        ASSERT_FALSE(ok_so.empty());
        DiagnosticEngine diag;
        EXPECT_TRUE(loader.load_plugin(ok_so.string(), pm, diag));
        EXPECT_FALSE(diag.has_errors());
        loader.unload_all();
    }
#endif

    fs::remove_all(tmp_dir);
}

}  // namespace
