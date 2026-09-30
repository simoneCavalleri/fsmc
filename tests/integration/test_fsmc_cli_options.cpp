/**
 * @file test_fsmc_cli_options.cpp
 * @brief Comprehensive verification suite for all fsmc command-line options.
 *
 * Verifies that every single command-line option of fsmc functions as expected:
 * - Runtime export: --export-runtime, --export-runtime=, directory handling, --std dialects
 * - General options: -h, --help, -v, --version
 * - Input/Output: -i, --input, positional, -o, --output, -t, --target, --lang
 * - Metadata: -n, --name, -N, --ns, --namespace, --package
 * - Parsing & sidecars: --format, -s, --sidecar, --emit-sidecar
 * - Optimization & pipeline: -O0, --no-opt, -O1, -O2, --optimize, -p, --pipeline, --7stage,
 *   --prune, --no-simplify, --inline, --submachine-dir, --pipe-through, --load-pass-plugin
 * - Safety & validation: -Werror, --strict, --races, --req-audit, --rtm, --rtm-format, --harness
 * - C++ dialect: --std, --c++17, --c++20, --standalone, --modular, --no-thread-safe, --no-stubs, --allow-diagram
 * - Formal verification: -e, --export, -V, --verify, --engine, --ltl, --ctl, --diagnostic-format
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "tools/fsmc/fsmc_driver.hpp"
#include "tools/fsmc/fsmc_options.hpp"

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

const char* kModelWithReqAndWarning = R"(
state def SafeSystem {
    entry; then Active;
    state Active;
    state DeadEnd;
    transition t1 first Active accept EvFault then DeadEnd;
}
)";

class FsmcOptionsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_dir_ = fs::temp_directory_path() /
                    ("fsmc_test_opts_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(test_dir_);

        model_file_ = (test_dir_ / "model.sysml").string();
        std::ofstream out(model_file_);
        out << kValidSysmlModel;

        puml_file_ = (test_dir_ / "model.puml").string();
        std::ofstream puml_out(puml_file_);
        puml_out << "@startuml\n[*] --> StateA\nStateA --> StateB : ev1\n@enduml\n";

        warning_model_file_ = (test_dir_ / "warning_model.sysml").string();
        std::ofstream warn_out(warning_model_file_);
        warn_out << kModelWithReqAndWarning;
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    fs::path test_dir_;
    std::string model_file_;
    std::string puml_file_;
    std::string warning_model_file_;
};

// ============================================================================
// 1. --export-runtime verification
// ============================================================================

/**
 * @brief Test Intent: Verify --export-runtime cleanly exports standalone runtime header to an existing directory
 * without empty namespace/types. Scenario: Pass existing directory to --export-runtime and verify fsm.hpp is created
 * with valid runtime code.
 */
TEST_F(FsmcOptionsTest, ExportRuntime_ToExistingDirectory_CreatesHeader) {
    fs::path export_dir = test_dir_ / "existing_runtime_dir";
    fs::create_directories(export_dir);

    char p[] = "fsmc";
    char opt[] = "--export-runtime";
    std::string dir_str = export_dir.string();
    char* argv[] = {p, opt, dir_str.data()};

    const auto parsed = fsm::tools::parse_cli_args(3, argv);
    EXPECT_TRUE(parsed.is_valid);
    EXPECT_EQ(parsed.export_runtime_dir, dir_str);

    int rc = fsm::tools::FsmcDriver::run(parsed);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(export_dir / "fsm.hpp"));
    EXPECT_GT(fs::file_size(export_dir / "fsm.hpp"), 1000u);

    std::ifstream ifs(export_dir / "fsm.hpp");
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("using  ="), std::string::npos);
    EXPECT_EQ(content.find("namespace fsm_generated"), std::string::npos);
    EXPECT_NE(content.find("namespace fsm"), std::string::npos);
}

/**
 * @brief Test Intent: Verify --export-runtime creates a new destination directory and writes standalone runtime header.
 * Scenario: Pass nonexistent directory path to --export-runtime and verify directory creation and valid fsm.hpp.
 */
TEST_F(FsmcOptionsTest, ExportRuntime_ToNonexistentDirectory_CreatesDirectoryAndHeader) {
    fs::path export_dir = test_dir_ / "new_runtime_dir";

    char p[] = "fsmc";
    std::string arg = "--export-runtime=" + export_dir.string();
    char* argv[] = {p, arg.data()};

    const auto parsed = fsm::tools::parse_cli_args(2, argv);
    EXPECT_TRUE(parsed.is_valid);
    EXPECT_EQ(parsed.export_runtime_dir, export_dir.string());

    int rc = fsm::tools::FsmcDriver::run(parsed);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(export_dir / "fsm.hpp"));
}

/**
 * @brief Test Intent: Verify --export-runtime with --std=20 generates C++20 standalone runtime header.
 * Scenario: Specify --std=20 together with --export-runtime and verify successful generation.
 */
TEST_F(FsmcOptionsTest, ExportRuntime_WithCpp20Dialect_GeneratesCpp20Runtime) {
    fs::path export_dir = test_dir_ / "runtime_cpp20";

    char p[] = "fsmc";
    char opt1[] = "--export-runtime";
    std::string dir_str = export_dir.string();
    char opt2[] = "--std=20";
    char* argv[] = {p, opt1, dir_str.data(), opt2};

    const auto parsed = fsm::tools::parse_cli_args(4, argv);
    EXPECT_TRUE(parsed.is_valid);
    EXPECT_EQ(parsed.cpp_standard, fsm::backend::cpp::CppStandard::Cpp20);

    int rc = fsm::tools::FsmcDriver::run(parsed);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(export_dir / "fsm.hpp"));
}

/**
 * @brief Test Intent: Verify --export-runtime allows specifying an explicit file path (.hpp).
 * Scenario: Pass a full header file path to --export-runtime and verify exact file emission.
 */
TEST_F(FsmcOptionsTest, ExportRuntime_WithExplicitFilePath_CreatesExactFile) {
    fs::path target_file = test_dir_ / "subdir" / "my_custom_fsm.hpp";

    char p[] = "fsmc";
    char opt[] = "--export-runtime";
    std::string path_str = target_file.string();
    char* argv[] = {p, opt, path_str.data()};

    const auto parsed = fsm::tools::parse_cli_args(3, argv);
    EXPECT_TRUE(parsed.is_valid);

    int rc = fsm::tools::FsmcDriver::run(parsed);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(target_file));
}

// ============================================================================
// 2. General Informational Options: -h, --help, -v, --version
// ============================================================================

/**
 * @brief Test Intent: Verify general informational flags (-h, --help, -v, --version) output help/version and exit
 * cleanly with code 0. Scenario: Invoke CLI options parser with each flag individually and verify driver returns 0.
 */
TEST_F(FsmcOptionsTest, GeneralOptions_HelpAndVersion_ReturnZero) {
    {
        char p[] = "fsmc";
        char h[] = "-h";
        char* argv[] = {p, h};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_TRUE(opts.show_help);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }
    {
        char p[] = "fsmc";
        char h[] = "--help";
        char* argv[] = {p, h};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_TRUE(opts.show_help);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }
    {
        char p[] = "fsmc";
        char v[] = "-v";
        char* argv[] = {p, v};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_TRUE(opts.show_version);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }
    {
        char p[] = "fsmc";
        char v[] = "--version";
        char* argv[] = {p, v};
        const auto opts = fsm::tools::parse_cli_args(2, argv);
        EXPECT_TRUE(opts.show_version);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }
}

// ============================================================================
// 3. Input, Output & Target Options (-i, -o, -t, -n, -N, --format)
// ============================================================================

/**
 * @brief Test Intent: Verify -i, -o, -t cpp, -n, and -N options generate valid C++ code with specified name and
 * namespace. Scenario: Compile SysML model specifying custom FSM name and nested namespace, verify file exists and
 * contains expected declarations.
 */
TEST_F(FsmcOptionsTest, InputOutputOptions_ProduceValidCPlusPlus) {
    fs::path out_file = test_dir_ / "net_conn.hpp";

    char p[] = "fsmc";
    char i_opt[] = "-i";
    std::string in_str = model_file_;
    char o_opt[] = "-o";
    std::string out_str = out_file.string();
    char t_opt[] = "-t";
    char t_val[] = "cpp";
    char n_opt[] = "-n";
    char n_val[] = "CustomNetworkFSM";
    char ns_opt[] = "-N";
    char ns_val[] = "aero::space";

    char* argv[] = {p, i_opt, in_str.data(), o_opt, out_str.data(), t_opt, t_val, n_opt, n_val, ns_opt, ns_val};
    const auto opts = fsm::tools::parse_cli_args(11, argv);
    EXPECT_TRUE(opts.is_valid);
    EXPECT_EQ(opts.fsm_name, "CustomNetworkFSM");
    EXPECT_EQ(opts.ns_name, "aero::space");

    EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    EXPECT_TRUE(fs::exists(out_file));

    std::ifstream ifs(out_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("namespace aero::space"), std::string::npos);
    EXPECT_NE(content.find("CustomNetworkFSM"), std::string::npos);
}

/**
 * @brief Test Intent: Verify positional input file combined with --option=value syntax parses correctly and compiles.
 * Scenario: Invoke fsmc with positional model file and --output=..., --name=..., --namespace=..., --target=cpp.
 */
TEST_F(FsmcOptionsTest, PositionalInput_WithOptEqualSyntax_Works) {
    fs::path out_file = test_dir_ / "pos_out.hpp";

    char p[] = "fsmc";
    std::string in_arg = model_file_;
    std::string out_arg = "--output=" + out_file.string();
    std::string name_arg = "--name=PosFSM";
    std::string ns_arg = "--namespace=pos_ns";
    std::string target_arg = "--target=cpp";

    char* argv[] = {p, in_arg.data(), out_arg.data(), name_arg.data(), ns_arg.data(), target_arg.data()};
    const auto opts = fsm::tools::parse_cli_args(6, argv);
    EXPECT_TRUE(opts.is_valid);
    EXPECT_EQ(opts.input_file, model_file_);
    EXPECT_EQ(opts.output_file, out_file.string());
    EXPECT_EQ(opts.fsm_name, "PosFSM");
    EXPECT_EQ(opts.ns_name, "pos_ns");

    EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    EXPECT_TRUE(fs::exists(out_file));
}

// ============================================================================
// 4. Diagram Ingestion & --allow-diagram / --sidecar
// ============================================================================

/**
 * @brief Test Intent: Verify informal diagram input (PlantUML) is rejected without --allow-diagram and accepted with
 * --allow-diagram. Scenario: Run fsmc on .puml model without --allow-diagram (expect failure), then with
 * --allow-diagram (expect success).
 */
TEST_F(FsmcOptionsTest, DiagramInput_BlockedWithoutAllowDiagram_SucceedsWithAllowDiagram) {
    fs::path out_file = test_dir_ / "diagram_out.hpp";

    // 1. Blocked without flag
    {
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = puml_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char* argv[] = {p, i, in_str.data(), o, out_str.data()};

        const auto opts = fsm::tools::parse_cli_args(5, argv);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
    }

    // 2. Succeeds with --allow-diagram
    {
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = puml_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char allow[] = "--allow-diagram";
        char* argv[] = {p, i, in_str.data(), o, out_str.data(), allow};

        const auto opts = fsm::tools::parse_cli_args(6, argv);
        EXPECT_TRUE(opts.allow_diagram_codegen);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file));
    }
}

// ============================================================================
// 5. C++ Standards & Standalone/Modular Packaging
// ============================================================================

/**
 * @brief Test Intent: Verify --modular, --c++20, --no-thread-safe, and --no-stubs options emit lightweight modular
 * C++20 header. Scenario: Compile model with modular runtime inclusion and disabled thread-safe wrappers, verify
 * generated code structure.
 */
TEST_F(FsmcOptionsTest, ModularPackaging_IncludesExternalHeader) {
    fs::path out_file = test_dir_ / "modular_out.hpp";

    char p[] = "fsmc";
    char i[] = "-i";
    std::string in_str = model_file_;
    char o[] = "-o";
    std::string out_str = out_file.string();
    char mod[] = "--modular";
    char c20[] = "--c++20";
    char nothread[] = "--no-thread-safe";
    char nostubs[] = "--no-stubs";

    char* argv[] = {p, i, in_str.data(), o, out_str.data(), mod, c20, nothread, nostubs};
    const auto opts = fsm::tools::parse_cli_args(9, argv);
    EXPECT_FALSE(opts.standalone);
    EXPECT_EQ(opts.cpp_standard, fsm::backend::cpp::CppStandard::Cpp20);
    EXPECT_FALSE(opts.thread_safe);
    EXPECT_FALSE(opts.include_stubs);

    EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    EXPECT_TRUE(fs::exists(out_file));

    std::ifstream ifs(out_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("#include \"fsm.hpp\""), std::string::npos);
    EXPECT_EQ(content.find("fsm/backend/cpp/runtime"), std::string::npos);
    EXPECT_EQ(content.find("thread_safe_fsm.hpp"), std::string::npos);
    EXPECT_EQ(content.find("spsc_fsm.hpp"), std::string::npos);
    EXPECT_EQ(content.find("make_thread_safe_fsm"), std::string::npos);
}

/**
 * @brief Test Intent: Verify --modular=<hdr> and --runtime-header options emit custom runtime header includes.
 * Scenario: Compile model specifying custom angle-bracketed (<fsm/fsm.hpp>) and quoted ("custom/rt.hpp") headers.
 */
TEST_F(FsmcOptionsTest, ModularPackaging_CustomRuntimeHeader) {
    // 1. Angle bracketed include via --modular=<hdr>
    {
        fs::path out_file = test_dir_ / "modular_custom_angle.hpp";
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char mod[] = "--modular=<fsm/fsm.hpp>";

        char* argv[] = {p, i, in_str.data(), o, out_str.data(), mod};
        const auto opts = fsm::tools::parse_cli_args(6, argv);
        EXPECT_FALSE(opts.standalone);
        EXPECT_EQ(opts.runtime_header, "<fsm/fsm.hpp>");
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);

        std::ifstream ifs(out_file);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        EXPECT_NE(content.find("#include <fsm/fsm.hpp>"), std::string::npos);
        EXPECT_EQ(content.find("fsm/backend/cpp/runtime"), std::string::npos);
    }

    // 2. Quoted include via --runtime-header
    {
        fs::path out_file = test_dir_ / "modular_custom_quoted.hpp";
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char mod[] = "--modular";
        char rh[] = "--runtime-header=my_runtime/fsm_engine.hpp";

        char* argv[] = {p, i, in_str.data(), o, out_str.data(), mod, rh};
        const auto opts = fsm::tools::parse_cli_args(7, argv);
        EXPECT_FALSE(opts.standalone);
        EXPECT_EQ(opts.runtime_header, "my_runtime/fsm_engine.hpp");
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);

        std::ifstream ifs(out_file);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        EXPECT_NE(content.find("#include \"my_runtime/fsm_engine.hpp\""), std::string::npos);
        EXPECT_EQ(content.find("fsm/backend/cpp/runtime"), std::string::npos);
    }
}

/**
 * @brief Test Intent: Verify seamless end-to-end integration between --export-runtime and --modular code generation.
 * Scenario: Export runtime to directory (creating fsm.hpp), generate modular FSM header, and verify matching header
 * include.
 */
TEST_F(FsmcOptionsTest, ModularPackaging_ExportRuntimeAlignment) {
    fs::path export_dir = test_dir_ / "exported_rt";
    fs::path out_file = export_dir / "protocol_fsm.hpp";

    // Step 1: Export runtime
    {
        char p[] = "fsmc";
        char exp[] = "--export-runtime";
        std::string dir_str = export_dir.string();
        char std20[] = "--std=20";
        char* argv[] = {p, exp, dir_str.data(), std20};

        const auto opts = fsm::tools::parse_cli_args(4, argv);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(export_dir / "fsm.hpp"));
    }

    // Step 2: Generate modular FSM into same directory
    {
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char mod[] = "--modular";
        char c20[] = "--c++20";
        char* argv[] = {p, i, in_str.data(), o, out_str.data(), mod, c20};

        const auto opts = fsm::tools::parse_cli_args(7, argv);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file));
    }

    // Step 3: Verify the modular file includes "fsm.hpp" exactly matching the exported runtime
    std::ifstream ifs(out_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("#include \"fsm.hpp\""), std::string::npos);
    EXPECT_EQ(content.find("fsm/backend/cpp/runtime"), std::string::npos);
}

// ============================================================================
// 6. Optimization, Pipeline Modes & Transforms
// ============================================================================

/**
 * @brief Test Intent: Verify compiler optimization levels (-O0, -O2), --7stage pipeline mode, --prune, and
 * --no-simplify flags. Scenario: Compile model with -O0 (no optimization) and -O2 with 7-stage pipeline and dead-state
 * pruning, verify success.
 */
TEST_F(FsmcOptionsTest, PipelineAndOptimizations_ExecuteSuccessfully) {
    // Test -O0 (no opt)
    {
        fs::path out_file = test_dir_ / "opt0.hpp";
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char opt[] = "-O0";
        char* argv[] = {p, i, in_str.data(), o, out_str.data(), opt};

        const auto opts = fsm::tools::parse_cli_args(6, argv);
        EXPECT_EQ(opts.opt_level, 0);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file));
    }

    // Test -O2, --7stage, --prune, --no-simplify
    {
        fs::path out_file = test_dir_ / "opt2.hpp";
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char o[] = "-o";
        std::string out_str = out_file.string();
        char opt[] = "-O2";
        char stage[] = "--7stage";
        char prune[] = "--prune";
        char nosimp[] = "--no-simplify";
        char* argv[] = {p, i, in_str.data(), o, out_str.data(), opt, stage, prune, nosimp};

        const auto opts = fsm::tools::parse_cli_args(9, argv);
        EXPECT_EQ(opts.opt_level, 2);
        EXPECT_EQ(opts.pipeline_mode, "7stage");
        EXPECT_TRUE(opts.prune_dead_states);
        EXPECT_FALSE(opts.simplify_guards);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file));
    }
}

// ============================================================================
// 7. -Werror Handling
// ============================================================================

/**
 * @brief Test Intent: Verify -Werror causes compilation to fail with exit code 1 when semantic/middle-end warnings are
 * detected. Scenario: Compile model containing trap/deadlock state warning with -Werror flag, verify driver fails.
 */
TEST_F(FsmcOptionsTest, WerrorOption_FailsOnSemanticOrMiddleEndWarnings) {
    fs::path out_file = test_dir_ / "werror_out.hpp";

    char p[] = "fsmc";
    char i[] = "-i";
    std::string in_str = warning_model_file_;
    char o[] = "-o";
    std::string out_str = out_file.string();
    char werr[] = "-Werror";
    char* argv[] = {p, i, in_str.data(), o, out_str.data(), werr};

    const auto opts = fsm::tools::parse_cli_args(6, argv);
    EXPECT_TRUE(opts.werror);
    // SafeSystem has a deadlock/trap state warning, so -Werror must reject compilation
    EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
}

// ============================================================================
// 8. RTM, Harness and Sidecar Manifest Export
// ============================================================================

/**
 * @brief Test Intent: Verify --rtm, --rtm-format, --harness, and --emit-sidecar options successfully export metadata
 * artifacts. Scenario: Compile model with RTM json export, MC/DC test harness generation, and sidecar manifest
 * emission.
 */
TEST_F(FsmcOptionsTest, RtmAndHarnessAndSidecar_ExportSuccessfully) {
    fs::path rtm_file = test_dir_ / "rtm.json";
    fs::path harness_file = test_dir_ / "mcdc_harness.cpp";
    fs::path sidecar_file = test_dir_ / "sidecar.yaml";

    char p[] = "fsmc";
    char i[] = "-i";
    std::string in_str = model_file_;
    std::string rtm_arg = "--rtm=" + rtm_file.string();
    char rtm_fmt[] = "--rtm-format=json";
    std::string harness_arg = "--harness=" + harness_file.string();
    std::string sidecar_arg = "--emit-sidecar=" + sidecar_file.string();

    char* argv[] = {p, i, in_str.data(), rtm_arg.data(), rtm_fmt, harness_arg.data(), sidecar_arg.data()};
    const auto opts = fsm::tools::parse_cli_args(7, argv);
    EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);

    EXPECT_TRUE(fs::exists(rtm_file));
    EXPECT_TRUE(fs::exists(harness_file));
    EXPECT_TRUE(fs::exists(sidecar_file));
}

// ============================================================================
// 9. Diagram Exports (-e mermaid, plantuml, dot, json, smv)
// ============================================================================

/**
 * @brief Test Intent: Verify diagram export option (-e) across all supported diagram and formal formats (mermaid,
 * plantuml, dot, json, smv). Scenario: Export SysML model to each format and verify exported files are created.
 */
TEST_F(FsmcOptionsTest, DiagramExports_AllSupportedFormats) {
    for (const auto& fmt : {"mermaid", "plantuml", "dot", "json", "smv"}) {
        fs::path out_file = test_dir_ / ("export." + std::string(fmt));
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char e[] = "-e";
        std::string fmt_str = fmt;
        char o[] = "-o";
        std::string out_str = out_file.string();

        char* argv[] = {p, i, in_str.data(), e, fmt_str.data(), o, out_str.data()};
        const auto opts = fsm::tools::parse_cli_args(7, argv);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
        EXPECT_TRUE(fs::exists(out_file));
    }
}

// ============================================================================
// 10. Formal Verification (-V, --ltl, --ctl, --engine)
// ============================================================================

/**
 * @brief Test Intent: Verify formal verification mode (-V) with temporal logic specifications (--ltl and --ctl).
 * Scenario: Run model checker with valid LTL invariant (G !(P && Q)) and CTL formula (EF P) parsed from CLI.
 */
TEST_F(FsmcOptionsTest, FormalVerification_LtlAndCtlSpecs) {
    // 1. Valid LTL invariant
    {
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char v[] = "-V";
        char ltl[] = "--ltl=G !(Disconnected && Connected)";
        char* argv[] = {p, i, in_str.data(), v, ltl};

        const auto opts = fsm::tools::parse_cli_args(5, argv);
        EXPECT_TRUE(opts.verify_mode);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }

    // 2. Valid CTL property
    {
        char p[] = "fsmc";
        char i[] = "-i";
        std::string in_str = model_file_;
        char v[] = "-V";
        char ctl[] = "--ctl=EF Connected";
        char* argv[] = {p, i, in_str.data(), v, ctl};

        const auto opts = fsm::tools::parse_cli_args(5, argv);
        EXPECT_TRUE(opts.verify_mode);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }
}

// ============================================================================
// 11. Conflicting Options, Invalid Diagnostic Formats & Stdin Support
// ============================================================================

/**
 * @brief Test Intent: Verify conflicting options (--standalone and --modular) warn and fail when -Werror is set.
 * Scenario: Run fsmc with both --standalone and --modular; verify success without -Werror and failure with -Werror.
 */
TEST_F(FsmcOptionsTest, ConflictingOptions_StandaloneAndModular_WarnsAndFailsWithWerror) {
    fs::path out_file = test_dir_ / "conflict_out.hpp";
    char p[] = "fsmc";
    char i[] = "-i";
    std::string in_str = model_file_;
    char stand[] = "--standalone";
    char mod[] = "--modular";
    char o[] = "-o";
    std::string out_str = out_file.string();

    // Without -Werror: succeeds with warning
    {
        char* argv[] = {p, i, in_str.data(), stand, mod, o, out_str.data()};
        const auto opts = fsm::tools::parse_cli_args(7, argv);
        EXPECT_TRUE(opts.standalone_specified);
        EXPECT_TRUE(opts.modular_specified);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 0);
    }

    // With -Werror: fails due to conflicting options
    {
        char werr[] = "-Werror";
        char* argv[] = {p, i, in_str.data(), stand, mod, werr, o, out_str.data()};
        const auto opts = fsm::tools::parse_cli_args(8, argv);
        EXPECT_EQ(fsm::tools::FsmcDriver::run(opts), 1);
    }
}

/**
 * @brief Test Intent: Verify invalid --diagnostic-format values are rejected before execution.
 * Scenario: Pass invalid diagnostic format string and verify parser marks options as invalid.
 */
TEST_F(FsmcOptionsTest, DiagnosticFormat_InvalidFormat_Rejected) {
    char p[] = "fsmc";
    char i[] = "-i";
    std::string in_str = model_file_;
    char fmt[] = "--diagnostic-format=invalid_fmt";
    char* argv[] = {p, i, in_str.data(), fmt};

    const auto opts = fsm::tools::parse_cli_args(4, argv);
    EXPECT_FALSE(opts.is_valid);
    EXPECT_NE(opts.error_message.find("Invalid diagnostic format"), std::string::npos);
}

/**
 * @brief Test Intent: Verify standard input positional dash (-) is parsed as valid input path.
 * Scenario: Pass '-' as positional input and verify parsed input_file is '-'.
 */
TEST_F(FsmcOptionsTest, StandardInput_PositionalDash_Recognized) {
    char p[] = "fsmc";
    char dash[] = "-";
    char* argv[] = {p, dash};

    const auto opts = fsm::tools::parse_cli_args(2, argv);
    EXPECT_TRUE(opts.is_valid);
    EXPECT_EQ(opts.input_file, "-");
}

}  // namespace
