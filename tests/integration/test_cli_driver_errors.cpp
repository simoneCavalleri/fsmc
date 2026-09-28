/**
 * @file test_cli_driver_errors.cpp
 * @brief Comprehensive regression tests for controlled fsmc and fsm-opt driver failure contracts.
 *
 * Implements Phase 6.1 of roadmap/implementation plan:
 * - missing input;
 * - unknown option;
 * - missing option argument;
 * - nonexistent or unreadable file;
 * - malformed model;
 * - unsupported format/export;
 * - missing external verifier/plugin;
 * - unwritable output;
 * - nuXmv missing-tool graceful fallback.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "tools/fsm-opt/opt_driver.hpp"
#include "tools/fsm-opt/opt_options.hpp"
#include "tools/fsmc/fsmc_driver.hpp"
#include "tools/fsmc/fsmc_options.hpp"

namespace fs = std::filesystem;

namespace {

const char* kValidSysml = R"(
state def SimpleFSM {
    entry; then Idle;
    state Idle;
    state Active;
    transition t1 first Idle accept EvStart then Active;
}
)";

/**
 * @brief Test Intent: Ensure fsmc and fsm-opt drivers reject missing input specifications cleanly.
 * Scenario: Invoke FsmcDriver and OptDriver with empty input paths.
 * Expected: Both drivers return non-zero exit code (1) without crashing or raising unhandled exceptions.
 */
TEST(FsmcDriver, MissingInput_ReturnsNonZero) {
    fsm::tools::FsmcOptions opts;
    EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);

    fsm::tools::OptOptions opt_opts;
    EXPECT_EQ(fsm::tools::OptDriver::run(opt_opts), 1);
}

/**
 * @brief Test Intent: Ensure unrecognized command-line arguments are rejected before execution.
 * Scenario: Pass an unknown flag `--not-a-real-option` to CLI argument parsers for fsmc and fsm-opt.
 * Expected: Parsers report invalid arguments with descriptive error message and execution returns exit code 1.
 */
TEST(CliOptions, UnknownArguments_AreRejectedBeforeDriverExecution) {
    char p1[] = "fsmc";
    char a1[] = "--not-a-real-option";
    char* argv1[] = {p1, a1};
    const auto fsmc_options = fsm::tools::parse_cli_args(2, argv1);
    EXPECT_FALSE(fsmc_options.is_valid);
    EXPECT_NE(fsmc_options.error_message.find("Unknown option"), std::string::npos);
    EXPECT_EQ(fsm::tools::FsmcDriver::run(fsmc_options), 1);

    char p2[] = "fsm-opt";
    char a2[] = "--not-a-real-option";
    char* argv2[] = {p2, a2};
    const auto opt_options = fsm::tools::parse_opt_args(2, argv2);
    EXPECT_FALSE(opt_options.is_valid);
    EXPECT_NE(opt_options.error_message.find("Unknown option"), std::string::npos);
    EXPECT_EQ(fsm::tools::OptDriver::run(opt_options), 1);
}

/**
 * @brief Test Intent: Ensure options requiring values fail with explicit missing argument diagnostics.
 * Scenario: Provide options like `-i`, `-o`, `-e`, `--std` without corresponding operand.
 * Expected: Parser rejects invocation and outputs 'Missing argument for option' diagnostic.
 */
TEST(CliOptions, MissingOptionArgument_ReportsClearDiagnosticAndFails) {
    // fsmc missing args
    {
        char p[] = "fsmc";
        char opt[] = "-i";
        char* argv[] = {p, opt};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_FALSE(opts.is_valid);
        EXPECT_NE(opts.error_message.find("Missing argument for option: -i"), std::string::npos);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
    }
    {
        char p[] = "fsmc";
        char opt[] = "-o";
        char* argv[] = {p, opt};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_FALSE(opts.is_valid);
        EXPECT_NE(opts.error_message.find("Missing argument for option: -o"), std::string::npos);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
    }
    {
        char p[] = "fsmc";
        char opt[] = "-e";
        char* argv[] = {p, opt};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_FALSE(opts.is_valid);
        EXPECT_NE(opts.error_message.find("Missing argument for option: -e"), std::string::npos);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
    }
    {
        char p[] = "fsmc";
        char opt[] = "--std";
        char* argv[] = {p, opt};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_FALSE(opts.is_valid);
        EXPECT_NE(opts.error_message.find("Missing argument for option: --std"), std::string::npos);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
    }

    // fsm-opt missing args
    {
        char p[] = "fsm-opt";
        char opt[] = "-i";
        char* argv[] = {p, opt};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_FALSE(opts.is_valid);
        EXPECT_NE(opts.error_message.find("Missing argument for option: -i"), std::string::npos);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 1);
    }
    {
        char p[] = "fsm-opt";
        char opt[] = "-o";
        char* argv[] = {p, opt};
        const auto opts = fsm::tools::parse_opt_args(2, argv);
        EXPECT_FALSE(opts.is_valid);
        EXPECT_NE(opts.error_message.find("Missing argument for option: -o"), std::string::npos);
        EXPECT_EQ(fsm::tools::OptDriver::run(opts), 1);
    }
}

/**
 * @brief Test Intent: Verify controlled error handling on nonexistent or unreadable input model files.
 * Scenario: Invoke CLI drivers specifying a path that does not exist in the filesystem.
 * Expected: Non-zero exit code returned with clear diagnostic without segment violations.
 */
TEST(CliDriver, NonexistentOrUnreadableFile_ReturnsNonZero) {
    fsm::tools::FsmcOptions fsmc_opts;
    fsmc_opts.input_file = "test-data/does-not-exist-at-all-12345.sysml";
    EXPECT_EQ(fsm::tools::FsmcDriver::run(fsmc_opts), 1);

    fsm::tools::OptOptions opt_opts;
    opt_opts.input_path = "test-data/does-not-exist-at-all-12345.sysml";
    EXPECT_EQ(fsm::tools::OptDriver::run(opt_opts), 1);
}

/**
 * @brief Test Intent: Verify parser failure propagation on syntactically malformed input files.
 * Scenario: Provide broken syntax file with unbalanced delimiters to fsmc and fsm-opt.
 * Expected: Both drivers fail with exit code 1 and emit parser diagnostic errors.
 */
TEST(CliDriver, MalformedModel_ReturnsNonZero) {
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_cli_malformed_test";
    fs::create_directories(temp_dir);
    fs::path bad_file = temp_dir / "broken.sysml";
    {
        std::ofstream out(bad_file);
        out << "this is totally broken syntax !!! {{{}}";
    }

    fsm::tools::FsmcOptions fsmc_opts;
    fsmc_opts.input_file = bad_file.string();
    EXPECT_EQ(fsm::tools::FsmcDriver::run(fsmc_opts), 1);

    fsm::tools::OptOptions opt_opts;
    opt_opts.input_path = bad_file.string();
    EXPECT_EQ(fsm::tools::OptDriver::run(opt_opts), 1);

    fs::remove_all(temp_dir);
}

/**
 * @brief Test Intent: Verify controlled failure on unsupported diagram export formats or target languages.
 * Scenario: Specify an invalid export format string or unsupported language code.
 * Expected: Drivers reject the request and return non-zero exit code.
 */
TEST(CliDriver, UnsupportedFormatOrExport_ReturnsNonZero) {
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_cli_export_test";
    fs::create_directories(temp_dir);
    fs::path valid_file = temp_dir / "valid.sysml";
    {
        std::ofstream out(valid_file);
        out << kValidSysml;
    }

    // Unsupported export format in fsmc
    fsm::tools::FsmcOptions fsmc_opts;
    fsmc_opts.input_file = valid_file.string();
    fsmc_opts.export_diagram_format = "totally_unsupported_format_xyz";
    EXPECT_EQ(fsm::tools::FsmcDriver::run(fsmc_opts), 1);

    // Unsupported target language in fsmc args
    char p[] = "fsmc";
    char a_i[] = "-i";
    std::string valid_file_str = valid_file.string();
    char a_f[] = "-t";
    char a_lang[] = "unsupported_rust_or_ada";
    char* argv[] = {p, a_i, valid_file_str.data(), a_f, a_lang};
    const auto parsed = fsm::tools::parse_cli_args(5, argv);
    EXPECT_FALSE(parsed.is_valid);
    EXPECT_EQ(fsm::tools::FsmcDriver::run(parsed), 1);

    fs::remove_all(temp_dir);
}

/**
 * @brief Test Intent: Verify graceful error reporting when a requested pass plugin shared object is missing.
 * Scenario: Provide nonexistent shared library path to `--load-pass-plugin`.
 * Expected: Plugin loader reports failure gracefully and drivers exit with status 1.
 */
TEST(CliDriver, MissingPassPlugin_ReturnsNonZero) {
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_cli_plugin_test";
    fs::create_directories(temp_dir);
    fs::path valid_file = temp_dir / "valid.sysml";
    {
        std::ofstream out(valid_file);
        out << kValidSysml;
    }

    fsm::tools::FsmcOptions fsmc_opts;
    fsmc_opts.input_file = valid_file.string();
    fsmc_opts.pass_plugins.push_back("nonexistent_plugin_xyz.so");
    EXPECT_EQ(fsm::tools::FsmcDriver::run(fsmc_opts), 1);

    fsm::tools::OptOptions opt_opts;
    opt_opts.input_path = valid_file.string();
    opt_opts.pass_plugins.push_back("nonexistent_plugin_xyz.so");
    EXPECT_EQ(fsm::tools::OptDriver::run(opt_opts), 1);

    fs::remove_all(temp_dir);
}

/**
 * @brief Test Intent: Verify controlled failure when output path cannot be created or written.
 * Scenario: Set output path inside an uncreatable directory hierarchy.
 * Expected: Drivers catch I/O error and exit with status 1 rather than unhandled exception.
 */
TEST(CliDriver, UnwritableOutput_ReturnsNonZero) {
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_cli_unwritable_test";
    fs::create_directories(temp_dir);
    fs::path valid_file = temp_dir / "valid.sysml";
    {
        std::ofstream out(valid_file);
        out << kValidSysml;
    }

    // Create a regular file as a blocker in path hierarchy so subdirectories cannot be created
    fs::path blocker_file = temp_dir / "file_blocker";
    {
        std::ofstream blocker(blocker_file);
        blocker << "blocker";
    }
    std::string bad_out = (blocker_file / "uncreatable_dir" / "unwritable_out.hpp").string();

    fsm::tools::FsmcOptions fsmc_opts;
    fsmc_opts.input_file = valid_file.string();
    fsmc_opts.output_file = bad_out;
    EXPECT_EQ(fsm::tools::FsmcDriver::run(fsmc_opts), 1);

    fsm::tools::OptOptions opt_opts;
    opt_opts.input_path = valid_file.string();
    opt_opts.output_path = bad_out;
    EXPECT_EQ(fsm::tools::OptDriver::run(opt_opts), 1);

    fs::remove_all(temp_dir);
}

/**
 * @brief Test Intent: Verify graceful fallback when external nuXmv binary is unavailable on system PATH.
 * Scenario: Request formal verification with nuXmv engine on a sound model when nuXmv is absent.
 * Expected: Driver falls back to internal verification passes and succeeds without crashing.
 */
TEST(CliDriver, NuXmvMissingTool_FallsBackGracefully) {
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_cli_nuxmv_test";
    fs::create_directories(temp_dir);
    fs::path valid_file = temp_dir / "valid.sysml";
    {
        std::ofstream out(valid_file);
        out << kValidSysml;
    }

    // When nuXmv engine is requested, if nuXmv is absent from PATH, the driver must
    // report a warning and gracefully fall back to internal verification without crashing
    fsm::tools::FsmcOptions fsmc_opts;
    fsmc_opts.input_file = valid_file.string();
    fsmc_opts.verify_mode = true;
    fsmc_opts.verify_engine = "nuxmv";

    int ret = fsm::tools::FsmcDriver::run(fsmc_opts);
    // Since the model is sound, internal verification passes (returns 0)
    EXPECT_EQ(ret, 0);

    fs::remove_all(temp_dir);
}

}  // namespace
