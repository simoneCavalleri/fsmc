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
#if defined(FSMC_TEST_CXX_COMPILER)
        const std::string cxx = FSMC_TEST_CXX_COMPILER;
#else
        const std::string cxx = "g++";
#endif
        const std::string cmd = "\"" + cxx + "\" -std=c++20 -shared -fPIC -I\"" + include_dir.string() + "\" \"" +
                                src.string() + "\" -o \"" + so.string() + "\"";
        if (std::system(cmd.c_str()) != 0) {
            return {};
        }
        return so;
    };

    // 4. Valid .so without ABI symbol -> E_PLUGIN_ABI
    {
        const fs::path no_abi_so = compile_so("no_abi", "extern \"C\" int other_fn() { return 42; }\n");
        ASSERT_FALSE(no_abi_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(no_abi_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 4b. Valid .so with mismatched ABI version -> E_PLUGIN_ABI
    {
        const fs::path bad_ver_so = compile_so("bad_ver", R"cpp(
            #include <cstdint>
            extern "C" std::uint32_t fsmc_plugin_abi_version() { return 999U; }
        )cpp");
        ASSERT_FALSE(bad_ver_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(bad_ver_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 4c. Valid .so with mismatched ABI fingerprint -> E_PLUGIN_ABI
    {
        const fs::path bad_fp_so = compile_so("bad_fp", R"cpp(
            #include "fsm/middleend/plugin/plugin_loader.hpp"
            extern "C" std::uint32_t fsmc_plugin_abi_version() {
                return ::fsm::middleend::plugin::kPluginAbiVersion;
            }
            extern "C" const char* fsmc_plugin_abi_fingerprint() { return "wrong-stl-00"; }
        )cpp");
        ASSERT_FALSE(bad_fp_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(bad_fp_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 4d. Valid .so with ABI but without fsmc_register_passes symbol -> E_PLUGIN_SYMBOL
    {
        const fs::path no_sym_so = compile_so("no_sym", R"cpp(
            #include "fsm/middleend/plugin/plugin_loader.hpp"
            FSMC_DEFINE_PLUGIN_ABI()
        )cpp");
        ASSERT_FALSE(no_sym_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(no_sym_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 5. Valid .so whose fsmc_register_passes adds a pass then throws std::runtime_error -> E_PLUGIN_INIT + rollback
    {
        const fs::path throw_std_so = compile_so("throw_std", R"cpp(
            #include <memory>
            #include <stdexcept>
            #include "fsm/middleend/pass_manager.hpp"
            namespace {
            class DummyPluginPass : public fsm::middleend::IPass {
              public:
                std::string name() const override { return "dummy-throw"; }
                std::string description() const override { return "dummy"; }
                bool run(fsm::ir::FsmIr&, fsm::diagnostic::DiagnosticEngine&) override { return true; }
                bool preserves_ir() const noexcept override { return true; }
            };
            }
            FSMC_DEFINE_PLUGIN_ABI()
            extern "C" void fsmc_register_passes(fsm::middleend::PassManager& target_pm) {
                target_pm.add_pass(std::make_unique<DummyPluginPass>());
                throw std::runtime_error("boom");
            }
        )cpp");
        ASSERT_FALSE(throw_std_so.empty());
        const std::size_t before = pm.pass_count();
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(throw_std_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
        ASSERT_FALSE(diag.get_diagnostics().empty());
        EXPECT_EQ(diag.get_diagnostics().front().code, "E_PLUGIN_INIT");
        EXPECT_EQ(pm.pass_count(), before);
    }

    // 6. Valid .so whose fsmc_register_passes throws unknown exception -> E_PLUGIN_INIT
    {
        const fs::path throw_unk_so = compile_so("throw_unk", R"cpp(
            #include "fsm/middleend/plugin/plugin_loader.hpp"
            FSMC_DEFINE_PLUGIN_ABI()
            extern "C" void fsmc_register_passes(fsm::middleend::PassManager&) {
                throw 123;
            }
        )cpp");
        ASSERT_FALSE(throw_unk_so.empty());
        DiagnosticEngine diag;
        EXPECT_FALSE(loader.load_plugin(throw_unk_so.string(), pm, diag));
        EXPECT_TRUE(diag.has_errors());
    }

    // 7. Valid .so registering a real pass + versioned .so.1 + PassManager move-assignment safety
    {
        const fs::path ok_so = compile_so("ok_plugin", R"cpp(
            #include <memory>
            #include "fsm/middleend/pass_manager.hpp"
            namespace {
            class OkPluginPass : public fsm::middleend::IPass {
              public:
                std::string name() const override { return "ok-plugin-pass"; }
                std::string description() const override { return "ok"; }
                bool run(fsm::ir::FsmIr&, fsm::diagnostic::DiagnosticEngine&) override { return true; }
                bool preserves_ir() const noexcept override { return true; }
            };
            }
            FSMC_DEFINE_PLUGIN_ABI()
            extern "C" void fsmc_register_passes(fsm::middleend::PassManager& target_pm) {
                target_pm.add_pass(std::make_unique<OkPluginPass>());
            }
        )cpp");
        ASSERT_FALSE(ok_so.empty());

        // Test versioned POSIX filename (.so.1)
        const fs::path versioned_so = tmp_dir / "ok_plugin.so.1";
        fs::copy_file(ok_so, versioned_so, fs::copy_options::overwrite_existing);

        PassManager pm_owned;
        DiagnosticEngine diag;
        EXPECT_TRUE(pm_owned.load_plugin(versioned_so.string(), diag));
        EXPECT_FALSE(diag.has_errors());
        EXPECT_EQ(pm_owned.pass_count(), 1U);

        // Move-assign over pm_owned: must destroy old plugin passes BEFORE dlclose!
        PassManager replacement;
        pm_owned = std::move(replacement);
        EXPECT_EQ(pm_owned.pass_count(), 0U);
    }
#endif

    fs::remove_all(tmp_dir);
}

}  // namespace
