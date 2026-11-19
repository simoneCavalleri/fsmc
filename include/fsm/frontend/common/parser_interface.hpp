/**
 * @file parser_interface.hpp
 * @brief Common Abstract Parser Interface, Frontend Classification, and Lexing Utilities.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend {

using ir::FsmIr;

/**
 * @brief Classification of state machine frontends for compilation guarantees.
 *
 * - Formal: Strict formal metamodels (SysML v2, W3C SCXML, Cameo/MagicDraw XMI).
 *           Contains typed variables, explicit physical units, event payloads, and deterministic semantics.
 * - Diagram: Visual diagramming & descriptive notations (PlantUML, Mermaid, Graphviz DOT, JSON).
 *            Designed for visual sketching; types and contracts are inferred or supplemented via @fsm directives.
 */
enum class FrontendKind : std::uint8_t {
    Formal,  ///< Strict formal metamodel: SysML v2, W3C SCXML, Cameo/MagicDraw XMI, JSON AST
    Diagram  ///< Visual diagram notation: PlantUML, Mermaid, Graphviz DOT
};

/**
 * @brief Converts a FrontendKind enum into its descriptive human-readable label.
 */
[[nodiscard]] inline std::string_view frontend_kind_to_string(FrontendKind kind) noexcept {
    switch (kind) {
        case FrontendKind::Formal:
            return "Formal Model (Deterministic & High-Semantics)";
        case FrontendKind::Diagram:
            return "Visual Diagram (Descriptive / Heuristic)";
    }
    return "Visual Diagram (Descriptive / Heuristic)";
}

/**
 * @brief Abstract interface for all frontends converting text or structured sources into FsmIr.
 */
class IParser {
  public:
    virtual ~IParser() = default;

    /**
     * @brief Returns whether this parser produces formal or descriptive diagram models.
     */
    [[nodiscard]] virtual FrontendKind kind() const noexcept { return FrontendKind::Formal; }

    /**
     * @brief Canonical name of the format ingested by this parser (e.g. "plantuml", "sysml2").
     */
    [[nodiscard]] virtual std::string_view format_name() const noexcept { return "unknown"; }

    /**
     * @brief Ingests source content and populates the output FsmIr metamodel.
     * @param content Raw input model text or document.
     * @param[out] out_ir Destination intermediate representation.
     * @param[out] diag Diagnostic engine receiving structured errors and warnings.
     * @return True if parsing succeeded, false otherwise.
     */
    virtual bool parse(std::string_view content, FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) = 0;
};

/**
 * @brief Reports a parser error to a DiagnosticEngine, extracting line numbers when encoded in the message.
 */
inline void report_parse_error(diagnostic::DiagnosticEngine& diag, std::string_view code, std::string_view message,
                               std::string_view file_path = "") {
    diagnostic::SourceSpan span;
    span.file_path = std::string(file_path);
    // Recognize common "Line N:" or "line N" prefix for structured span reporting.
    if (message.size() > 5 && (message.rfind("Line ", 0) == 0 || message.rfind("line ", 0) == 0)) {
        size_t pos = 5;
        size_t line_val = 0;
        while (pos < message.size() && message[pos] >= '0' && message[pos] <= '9') {
            line_val = line_val * 10 + static_cast<size_t>(message[pos] - '0');
            ++pos;
        }
        if (line_val > 0) {
            span.line = line_val;
        }
    }
    diag.report(diagnostic::Diagnostic::error(
        std::string(code), message.empty() ? "Syntax or semantic parse error" : std::string(message), std::move(span)));
}

/**
 * @brief Checks if a string view starts with a given prefix.
 */
[[nodiscard]] inline bool starts_with(std::string_view str, std::string_view prefix) noexcept {
#if __cplusplus >= 202002L
    return str.starts_with(prefix);
#else
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
#endif
}

/**
 * @brief Checks if a string view ends with a given suffix.
 */
[[nodiscard]] inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
#if __cplusplus >= 202002L
    return str.ends_with(suffix);
#else
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
#endif
}

/**
 * @brief Trims leading and trailing ASCII whitespace from a string view.
 * @param str The string view to trim.
 * @return Trimmed string view.
 */
[[nodiscard]] inline std::string_view trim(std::string_view str) noexcept {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r' || str.front() == '\n')) {
        str.remove_prefix(1);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n')) {
        str.remove_suffix(1);
    }
    return str;
}

/**
 * @brief Checks if a given identifier is a reserved C++ language keyword.
 */
[[nodiscard]] bool is_cpp_keyword(std::string_view token) noexcept;

/**
 * @brief Escapes reserved C++ keywords by appending an underscore.
 */
[[nodiscard]] std::string escape_cpp_keyword(std::string_view token);

/**
 * @brief Converts arbitrary text into a valid C++ alphanumeric identifier.
 */
[[nodiscard]] std::string sanitize_identifier(std::string_view str);

}  // namespace fsm::frontend
