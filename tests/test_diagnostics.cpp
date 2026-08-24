#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"

using namespace fsm::codegen;

namespace {

TEST(DiagnosticEngineTest, ErrorRenderingWithCaret) {
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

}  // namespace
