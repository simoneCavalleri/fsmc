/**
 * @file diagnostic_engine.hpp
 * @brief Structured compiler diagnostics, source spans, and terminal rendering engine.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsm::diagnostic {

/**
 * @enum DiagnosticSeverity
 * @brief Severity level for compiler and verification diagnostics.
 */
enum class DiagnosticSeverity : std::uint8_t { Note, Info = Note, Warning, Error, Fatal, SafetyCritical = Fatal };

/**
 * @struct SourceSpan
 * @brief Precise source span locating a token or AST construct in an input file.
 */
struct SourceSpan {
    std::string file_path;  ///< Path to source document
    size_t line{1};         ///< 1-indexed start line number
    size_t column{1};       ///< 1-indexed start column number
    size_t length{1};       ///< Span character length

    [[nodiscard]] bool is_valid() const noexcept { return !file_path.empty() && line > 0; }
};

/**
 * @struct Diagnostic
 * @brief Diagnostic report containing severity, error code, location, and actionable suggestions.
 */
struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};  ///< Diagnostic severity level
    std::string code;                                        ///< Canonical error/warning code (e.g., "E0101", "W0103")
    std::string message;                                     ///< Primary diagnostic text
    SourceSpan span;                                         ///< Primary source location span
    std::string help_suggestion;                             ///< Actionable fix advice
    std::vector<std::pair<SourceSpan, std::string>> secondary_labels;  ///< Additional reference locations

    static Diagnostic error(std::string code, std::string message, SourceSpan span = {}) {
        return Diagnostic{DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(span), "", {}};
    }

    static Diagnostic safety_critical(std::string code, std::string message, SourceSpan span = {}) {
        return Diagnostic{DiagnosticSeverity::Fatal, std::move(code), std::move(message), std::move(span), "", {}};
    }

    static Diagnostic warning(std::string code, std::string message, SourceSpan span = {}) {
        return Diagnostic{DiagnosticSeverity::Warning, std::move(code), std::move(message), std::move(span), "", {}};
    }

    static Diagnostic info(std::string code, std::string message, SourceSpan span = {}) {
        return Diagnostic{DiagnosticSeverity::Note, std::move(code), std::move(message), std::move(span), "", {}};
    }

    static Diagnostic note(std::string message, SourceSpan span = {}) {
        return Diagnostic{DiagnosticSeverity::Note, "", std::move(message), std::move(span), "", {}};
    }
};

/**
 * @class DiagnosticEngine
 * @brief Rich Diagnostic Engine providing colored terminal output with Rust/Clang-style carets.
 */
class DiagnosticEngine {
  public:
    /**
     * @brief Records a diagnostic into the collection.
     */
    void report(Diagnostic diag);

    /**
     * @brief Checks whether any Error or Fatal diagnostics have been reported.
     */
    [[nodiscard]] bool has_errors() const noexcept;

    /**
     * @brief Checks whether any Warning diagnostics have been reported.
     */
    [[nodiscard]] bool has_warnings() const noexcept;

    /**
     * @brief Returns immutable reference to all collected diagnostics.
     */
    [[nodiscard]] const std::vector<Diagnostic>& get_diagnostics() const noexcept;

    /**
     * @brief Clears all reported diagnostics.
     */
    void clear() noexcept;

    /**
     * @brief Renders all collected diagnostics into formatted ANSI color strings with visual carets.
     */
    [[nodiscard]] std::string render_to_string(std::string_view source_content = "") const;

  private:
    static std::string extract_line(std::string_view text, size_t line_num);

    std::vector<Diagnostic> diagnostics_;
    bool has_errors_{false};
};

}  // namespace fsm::diagnostic
