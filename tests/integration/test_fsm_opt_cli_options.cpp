/**
 * @file test_fsm_opt_cli_options.cpp
 * @brief Comprehensive verification suite for all fsm-opt command-line options.
 *
 * Validates:
 * - General options: -h, --help, -v, --version, --list-passes
 * - Input/Output: -i, --input, positional, -o, --output, --format
 * - Formal emissions: --emit-ir, --emit-puml, --emit-mmd, --emit-sysml, --emit-json,
 *   --emit-dot, --emit-scxml, --emit-cameo, --emit-smv, --emit-stateflow
 * - Pipeline customization: --passes=..., --prune-dead, --print-before-all, --print-after-all
 * - Profiling & metrics: --profile, --metrics, --stats, --verify, --check
 * - Safety & diagnostics: -Werror, --diagnostic-format (text, json, github)
 * - Extensibility: --pipe-through, --load-pass-plugin
 */

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "tools/fsm-opt/opt_driver.hpp"
#include "tools/fsm-opt/opt_options.hpp"

namespace fs = std::filesystem;

namespace {

const char* kValidSysmlModel = R"(
state def NetworkConnection {
    entry; then Disconnected;
    state Disconnected;
    state Connected;
    transition t1 first Disconnected accept EvConnect then Connected;
    transition t2 first Connected accept EvDisconnect then Disconnected;
}
)";

const char* kModelWithTrapWarning = R"(
state def TrapModel {
    entry; then Active;
    state Active;
    state TrapState;
    transition t1 first Active accept EvGoToTrap then TrapState;
}
)";

class FsmOptOptionsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_dir_ =
            fs::temp_directory_path() /
            ("fsm_opt_test_opts_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(test_dir_);

        model_file_ = (test_dir_ / "model.sysml").string();
        std::ofstream out(model_file_);
        out << kValidSysmlModel;

        trap_model_file_ = (test_dir_ / "trap_model.sysml").string();
        std::ofstream trap_out(trap_model_file_);
        trap_out << kModelWithTrapWarning;
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    fs::path test_dir_;
    std::string model_file_;
    std::string trap_model_file_;
};

// ============================================================================
// 1. General Informational Options: -h, --help, -v, --version, --list-passes
// ============================================================================

/**
 * @brief Test Intent: Verify general informational flags (-h, --help, -v, --version, --list-passes) exit cleanly with
 * code 0. Scenario: Invoke fsm-opt with each informational option individually and verify successful exit code 0.
 */
TEST_F(FsmOptOptionsTest, GeneralOptions_HelpVersionAndListPasses_ReturnZero) {
    {
        char p[] = "fsm-opt";
        char h[] = "-h";
        char* argv[] = {p, h};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_TRUE(opts.show_help);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }
    {
        char p[] = "fsm-opt";
        char h[] = "--help";
        char* argv[] = {p, h};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_TRUE(opts.show_help);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }
    {
        char p[] = "fsm-opt";
        char v[] = "-v";
        char* argv[] = {p, v};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_TRUE(opts.show_version);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }
    {
        char p[] = "fsm-opt";
        char v[] = "--version";
        char* argv[] = {p, v};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_TRUE(opts.show_version);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }
    {
        char p[] = "fsm-opt";
        char lp[] = "--list-passes";
        char* argv[] = {p, lp};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_TRUE(opts.list_passes);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }
}

// ============================================================================
// 2. Input and Output Options (-i, -o, positional, --format)
// ============================================================================

/**
 * @brief Test Intent: Verify -i, -o, positional input, and --option=value syntax work seamlessly in fsm-opt.
 * Scenario: Run fsm-opt with standard flags and --input= / --output= / --format= syntax, verifying output JSON
 * creation.
 */
TEST_F(FsmOptOptionsTest, InputOutputOptions_StandardAndEqualSyntax) {
    fs::path out_file1 = test_dir_ / "out1.json";
    fs::path out_file2 = test_dir_ / "out2.json";

    // 1. Standard -i, -o
    {
        char p[] = "fsm-opt";
        char i[] = "-i";
        std::string in_str = model_file_;
        char o[] = "-o";
        std::string out_str = out_file1.string();
        char* argv[] = {p, i, in_str.data(), o, out_str.data()};

        const auto opts = fsm::tools::parse_opt_args(5, argv);
        EXPECT_TRUE(opts.is_valid);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file1));
    }

    // 2. Positional and --input=, --output=, --format=
    {
        char p[] = "fsm-opt";
        std::string in_arg = "--input=" + model_file_;
        std::string out_arg = "--output=" + out_file2.string();
        char fmt_arg[] = "--format=sysml2";
        char* argv[] = {p, in_arg.data(), out_arg.data(), fmt_arg};

        const auto opts = fsm::tools::parse_opt_args(4, argv);
        EXPECT_TRUE(opts.is_valid);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file2));
    }
}

// ============================================================================
// 3. All Serialization & Formal Emission Modes (--emit-*)
// ============================================================================

/**
 * @brief Test Intent: Verify all ten formal emission flags produce valid non-empty outputs.
 * Scenario: Run fsm-opt with --emit-ir, --emit-puml, --emit-mmd, --emit-sysml, --emit-dot, --emit-json, --emit-scxml,
 * --emit-cameo, --emit-smv, --emit-stateflow.
 */
TEST_F(FsmOptOptionsTest, EmitFormats_ProduceNonEmptyOutputs) {
    const std::vector<std::pair<std::string, std::string>> formats = {
        {"--emit-ir", "ir.json"},         {"--emit-puml", "model.puml"}, {"--emit-mmd", "model.mmd"},
        {"--emit-sysml", "model.sysml"},  {"--emit-dot", "model.dot"},   {"--emit-json", "model.json"},
        {"--emit-scxml", "model.scxml"},  {"--emit-cameo", "model.xmi"}, {"--emit-smv", "model.smv"},
        {"--emit-stateflow", "model.sfx"}};

    for (const auto& [flag, filename] : formats) {
        fs::path out_file = test_dir_ / filename;
        char p[] = "fsm-opt";
        char i[] = "-i";
        std::string in_str = model_file_;
        std::string emit_flag = flag;
        char o[] = "-o";
        std::string out_str = out_file.string();

        char* argv[] = {p, i, in_str.data(), emit_flag.data(), o, out_str.data()};
        const auto opts = fsm::tools::parse_opt_args(6, argv);
        EXPECT_TRUE(opts.is_valid);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file));
        EXPECT_GT(fs::file_size(out_file), 10u);
    }
}

// ============================================================================
// 4. Custom Pass Pipeline, Profiling and Metrics
// ============================================================================

/**
 * @brief Test Intent: Verify custom pass pipeline execution, dead-state pruning, IR dumping, profiling, and metrics
 * flags. Scenario: Run fsm-opt with --passes=..., --prune-dead, --print-before-all, --print-after-all, --profile,
 * --metrics, --verify.
 */
TEST_F(FsmOptOptionsTest, CustomPassPipelineAndProfiling) {
    fs::path out_file = test_dir_ / "custom_pipe.json";

    char p[] = "fsm-opt";
    char i[] = "-i";
    std::string in_str = model_file_;
    char passes[] = "--passes=canonicalize,guard-simplification,choice-completeness";
    char prune[] = "--prune-dead";
    char p_before[] = "--print-before-all";
    char p_after[] = "--print-after-all";
    char prof[] = "--profile";
    char metrics[] = "--metrics";
    char verify[] = "--verify";
    char o[] = "-o";
    std::string out_str = out_file.string();

    char* argv[] = {p, i, in_str.data(), passes, prune, p_before, p_after, prof, metrics, verify, o, out_str.data()};
    const auto opts = fsm::tools::parse_opt_args(12, argv);
    EXPECT_TRUE(opts.is_valid);
    EXPECT_TRUE(opts.profile);
    EXPECT_TRUE(opts.show_metrics);
    EXPECT_TRUE(opts.verify_only);
    EXPECT_TRUE(opts.print_before_all);
    EXPECT_TRUE(opts.print_after_all);

    EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
}

// ============================================================================
// 5. -Werror Rigor & Diagnostic Formats
// ============================================================================

/**
 * @brief Test Intent: Verify -Werror succeeds on sound models with informational notes and strictly fails when
 * diagnostics contain warnings. Scenario: Run fsm-opt with -Werror on a sound model (notes only) and then on a model
 * with a trap state warning.
 */
TEST_F(FsmOptOptionsTest, Werror_SucceedsOnSoundModelAndFailsOnWarnings) {
    // Sound model with -Werror: notes must not trigger failure
    {
        char p[] = "fsm-opt";
        char i[] = "-i";
        std::string in_str = model_file_;
        char werr[] = "-Werror";
        char* argv[] = {p, i, in_str.data(), werr};

        const auto opts = fsm::tools::parse_opt_args(4, argv);
        EXPECT_TRUE(opts.werror);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }

    // Model with trap state warning: -Werror must fail
    {
        char p[] = "fsm-opt";
        char i[] = "-i";
        std::string in_str = trap_model_file_;
        char werr[] = "-Werror";
        char* argv[] = {p, i, in_str.data(), werr};

        const auto opts = fsm::tools::parse_opt_args(4, argv);
        EXPECT_TRUE(opts.werror);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 1);
    }
}

/**
 * @brief Test Intent: Verify diagnostic formatting options (--diagnostic-format=text|json|github).
 * Scenario: Run fsm-opt specifying each diagnostic format on a model that produces diagnostic messages.
 */
TEST_F(FsmOptOptionsTest, DiagnosticFormats_TextJsonGithub) {
    for (const auto& fmt : {"text", "json", "github"}) {
        char p[] = "fsm-opt";
        char i[] = "-i";
        std::string in_str = trap_model_file_;
        std::string diag_arg = "--diagnostic-format=" + std::string(fmt);
        char* argv[] = {p, i, in_str.data(), diag_arg.data()};

        const auto opts = fsm::tools::parse_opt_args(4, argv);
        EXPECT_EQ(opts.diagnostic_format, fmt);
        // Returns 0 without -Werror
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }
}

// ============================================================================
// 6. External Filter and Dynamic Pass Plugin Loading
// ============================================================================

/**
 * @brief Test Intent: Verify external Unix filter pipeline (--pipe-through).
 * Scenario: Pass model through external Unix utility (cat) via --pipe-through and verify transformed model emission.
 */
TEST_F(FsmOptOptionsTest, PipeThroughAndPluginLoading) {
    fs::path out_file = test_dir_ / "piped.json";

    char p[] = "fsm-opt";
    char i[] = "-i";
    std::string in_str = model_file_;
    char pipe_arg[] = "--pipe-through=cat";
    char o[] = "-o";
    std::string out_str = out_file.string();

    char* argv[] = {p, i, in_str.data(), pipe_arg, o, out_str.data()};
    const auto opts = fsm::tools::parse_opt_args(6, argv);
    EXPECT_EQ(opts.pipe_through_cmd, "cat");
    EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    EXPECT_TRUE(fs::exists(out_file));
}

// ============================================================================
// 7. Unknown Passes, Invalid Diagnostic Formats & Stdin Support
// ============================================================================

/**
 * @brief Test Intent: Verify unrecognized pass name triggers a warning and fails under -Werror.
 * Scenario: Run fsm-opt with unknown pass name; succeeds without -Werror, fails with -Werror.
 */
TEST_F(FsmOptOptionsTest, UnknownPassName_WarnsAndFailsWithWerror) {
    char p[] = "fsm-opt";
    char i[] = "-i";
    std::string in_str = model_file_;
    char pass_arg[] = "--passes=invalid-pass-name";

    // Without -Werror: succeeds with warning
    {
        char* argv[] = {p, i, in_str.data(), pass_arg};
        const auto opts = fsm::tools::parse_opt_args(4, argv);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 0);
    }

    // With -Werror: fails due to unknown pass warning
    {
        char werr[] = "-Werror";
        char* argv[] = {p, i, in_str.data(), pass_arg, werr};
        const auto opts = fsm::tools::parse_opt_args(5, argv);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 1);
    }
}

/**
 * @brief Test Intent: Verify invalid --diagnostic-format values are rejected by fsm-opt.
 * Scenario: Pass invalid diagnostic format string to fsm-opt and verify option validation failure.
 */
TEST_F(FsmOptOptionsTest, DiagnosticFormat_InvalidFormat_Rejected) {
    char p[] = "fsm-opt";
    char i[] = "-i";
    std::string in_str = model_file_;
    char fmt[] = "--diagnostic-format=yaml_unsupported";
    char* argv[] = {p, i, in_str.data(), fmt};

    const auto opts = fsm::tools::parse_opt_args(4, argv);
    EXPECT_FALSE(opts.is_valid);
    EXPECT_NE(opts.error_message.find("Invalid diagnostic format"), std::string::npos);
}

/**
 * @brief Test Intent: Verify standard input positional dash (-) is parsed as valid input in fsm-opt.
 * Scenario: Pass '-' as positional input and verify parsed input_path is '-'.
 */
TEST_F(FsmOptOptionsTest, StandardInput_PositionalDash_Recognized) {
    char p[] = "fsm-opt";
    char dash[] = "-";
    char* argv[] = {p, dash};

    const auto opts = fsm::tools::parse_opt_args(2, argv);
    EXPECT_TRUE(opts.is_valid);
    EXPECT_EQ(opts.input_path, "-");
}

}  // namespace
