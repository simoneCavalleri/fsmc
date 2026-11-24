/**
 * @file test_diagnostics.cpp
 * @brief Unit test suite for the compiler diagnostic engine and source caret rendering.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"

using namespace fsm::diagnostic;

namespace {

/**
 * @brief Verify diagnostic engine source code rendering with line numbers, caret underlines, and help tips.
 * @scenario Report a warning diagnostic with a specific SourceSpan (line 2, col 7, length 11) and help suggestion.
 * @expected Rendered output contains file location, source code excerpt, caret underline '^~~~~~~~~~~', and suggestion.
 */
TEST(DiagnosticEngine, WarningWithSourceSpan_RenderedWithCaretUnderlineAndHelp) {
    const std::string dummy_source =
        "@startuml\n"
        "state CheckChoice <<choice>>\n"
        "CheckChoice --> TargetState [guard]\n"
        "@enduml\n";

    DiagnosticEngine diag;
    SourceSpan span;
    span.file_path = "model.puml";
    span.line = 2;
    span.column = 7;
    span.length = 11;

    Diagnostic d = Diagnostic::warning("W0103", "Choice pseudostate lacks default fallback branch", span);
    d.help_suggestion = "add an unconditional fallback transition 'CheckChoice --> DefaultState'";
    diag.report(d);

    EXPECT_FALSE(diag.has_errors());
    EXPECT_EQ(diag.get_diagnostics().size(), 1u);

    std::string rendered = diag.render_to_string(dummy_source);
    EXPECT_NE(rendered.find("warning[W0103]"), std::string::npos);
    EXPECT_NE(rendered.find("model.puml:2:7"), std::string::npos);
    EXPECT_NE(rendered.find("state CheckChoice <<choice>>"), std::string::npos);
    EXPECT_NE(rendered.find("^~~~~~~~~~~"), std::string::npos);
    EXPECT_NE(rendered.find("add an unconditional fallback transition"), std::string::npos);
}

/**
 * @brief Verify GitHub Actions and JSON diagnostic format output.
 */
TEST(DiagnosticEngine, FormatOutput_GitHubAndJson) {
    DiagnosticEngine diag;
    SourceSpan span;
    span.file_path = "fms.sysml";
    span.line = 10;
    span.column = 5;
    span.length = 12;

    Diagnostic err = Diagnostic::error("E0101", "Deadlock trap state detected", span);
    err.help_suggestion = "Provide at least one outgoing transition";
    diag.report(err);

    Diagnostic warn = Diagnostic::warning("W0202", "Unprioritized branch collision");
    diag.report(warn);

    // GitHub Actions format
    std::string gh_out = diag.render_to_format(DiagnosticFormat::GitHub);
    EXPECT_NE(gh_out.find("::error file=fms.sysml,line=10,col=5,title=E0101::Deadlock trap state detected | Help: "
                          "Provide at least one outgoing transition"),
              std::string::npos);
    EXPECT_NE(gh_out.find("::warning title=W0202::Unprioritized branch collision"), std::string::npos);

    // JSON format
    std::string json_out = diag.render_to_format(DiagnosticFormat::Json);
    EXPECT_NE(json_out.find("\"severity\": \"error\""), std::string::npos);
    EXPECT_NE(json_out.find("\"code\": \"E0101\""), std::string::npos);
    EXPECT_NE(json_out.find("\"file\": \"fms.sysml\""), std::string::npos);
    EXPECT_NE(json_out.find("\"line\": 10"), std::string::npos);
    EXPECT_NE(json_out.find("\"severity\": \"warning\""), std::string::npos);
    EXPECT_NE(json_out.find("\"code\": \"W0202\""), std::string::npos);
}

/**
 * @brief Verify SourceManager multi-file source resolution, color toggle, and GitHub Actions property escaping.
 * @scenario Register two files in SourceManager, emit diagnostic with primary/secondary spans across files,
 *           disable ANSI colors, and verify comma/colon escaping in GitHub Actions file/title properties.
 * @expected Both source snippets are rendered from their respective files with zero ANSI escape sequences,
 *           and GitHub Actions properties escape ',' as '%2C' and ':' as '%3A'.
 */
TEST(DiagnosticEngine, MultiFileSourceManager_ColorDisableAndGitHubEscaping) {
    SourceManager sm;
    sm.register_source("main.sysml", "state A {\n  transition T1 -> B;\n}\n");
    sm.register_source("sub,dir:1/child.sysml", "state B {\n  exit action Cleanup;\n}\n");

    DiagnosticEngine diag;
    diag.set_color_enabled(false);
    EXPECT_FALSE(diag.is_color_enabled());

    Diagnostic d = Diagnostic::error("E:1,2", "Cross-file mismatch", SourceSpan{"main.sysml", 2, 3, 10});
    d.secondary_labels.emplace_back(SourceSpan{"sub,dir:1/child.sysml", 2, 3, 11}, "target defined here");
    diag.report(d);

    const std::string rendered = diag.render_to_string("", &sm);
    EXPECT_EQ(rendered.find("\033["), std::string::npos);
    EXPECT_NE(rendered.find("transition T1 -> B;"), std::string::npos);
    EXPECT_NE(rendered.find("exit action Cleanup;"), std::string::npos);

    DiagnosticEngine gh_diag;
    gh_diag.report(Diagnostic::error("E:1,2", "Bad path", SourceSpan{"sub,dir:1/child.sysml", 2, 3, 5}));
    const std::string gh_out = gh_diag.render_to_format(DiagnosticFormat::GitHub);
    EXPECT_NE(gh_out.find("file=sub%2Cdir%3A1/child.sysml"), std::string::npos);
    EXPECT_NE(gh_out.find("title=E%3A1%2C2"), std::string::npos);
}

}  // namespace
