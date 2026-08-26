#pragma once

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsm::codegen {

/**
 * @brief Severity level for compiler and verification diagnostics.
 */
enum class DiagnosticSeverity : std::uint8_t { Note, Info = Note, Warning, Error, Fatal, SafetyCritical = Fatal };

/**
 * @brief Precise source span locating a token or AST construct in an input file.
 */
struct SourceSpan {
    std::string file_path;
    size_t line{1};
    size_t column{1};
    size_t length{1};

    [[nodiscard]] bool is_valid() const noexcept { return !file_path.empty() && line > 0; }
};

/**
 * @brief Diagnostic report containing severity, error code, location, and actionable suggestions.
 */
struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string code;  ///< Canonical error/warning code (e.g., "E0101", "W0103")
    std::string message;
    SourceSpan span;
    std::string help_suggestion;
    std::vector<std::pair<SourceSpan, std::string>> secondary_labels;

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
 * @brief Rich Diagnostic Engine providing colored terminal output with Rust/Clang-style carets.
 */
class DiagnosticEngine {
  public:
    void report(Diagnostic diag) {
        if (diag.severity == DiagnosticSeverity::Error || diag.severity == DiagnosticSeverity::Fatal) {
            has_errors_ = true;
        }
        diagnostics_.push_back(std::move(diag));
    }

    [[nodiscard]] bool has_errors() const noexcept { return has_errors_; }
    [[nodiscard]] bool has_warnings() const noexcept {
        for (const auto& diag : diagnostics_) {
            if (diag.severity == DiagnosticSeverity::Warning) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] const std::vector<Diagnostic>& get_diagnostics() const noexcept { return diagnostics_; }

    void clear() noexcept {
        diagnostics_.clear();
        has_errors_ = false;
    }

    /**
     * @brief Renders all collected diagnostics into formatted ANSI color strings with visual carets.
     */
    [[nodiscard]] std::string render_to_string(std::string_view source_content = "") const {
        std::ostringstream ss;
        for (const auto& diag : diagnostics_) {
            // Severity header
            switch (diag.severity) {
                case DiagnosticSeverity::Fatal:
                case DiagnosticSeverity::Error:
                    ss << "\033[1;31merror";
                    if (!diag.code.empty())
                        ss << "[" << diag.code << "]";
                    ss << "\033[0m: " << diag.message << "\n";
                    break;
                case DiagnosticSeverity::Warning:
                    ss << "\033[1;33mwarning";
                    if (!diag.code.empty())
                        ss << "[" << diag.code << "]";
                    ss << "\033[0m: " << diag.message << "\n";
                    break;
                case DiagnosticSeverity::Note:
                    ss << "\033[1;36mnote\033[0m: " << diag.message << "\n";
                    break;
            }

            // Location arrow
            if (diag.span.is_valid()) {
                ss << "  \033[1;34m-->\033[0m " << diag.span.file_path << ":" << diag.span.line << ":"
                   << diag.span.column << "\n";

                // If source line is available, print context and caret
                if (!source_content.empty()) {
                    std::string line_text = extract_line(source_content, diag.span.line);
                    if (!line_text.empty()) {
                        ss << "   \033[1;34m|\033[0m\n";
                        ss << " " << diag.span.line << " \033[1;34m|\033[0m " << line_text << "\n";
                        ss << "   \033[1;34m|\033[0m ";
                        size_t pad = (diag.span.column > 0) ? (diag.span.column - 1) : 0;
                        for (size_t i = 0; i < pad; ++i)
                            ss << " ";
                        ss << "\033[1;31m^";
                        for (size_t i = 1; i < diag.span.length; ++i)
                            ss << "~";
                        ss << "\033[0m\n";
                    }
                }
            }

            // Help suggestion
            if (!diag.help_suggestion.empty()) {
                ss << "   \033[1;34m=\033[0m \033[1mhelp\033[0m: " << diag.help_suggestion << "\n";
            }
            ss << "\n";
        }
        return ss.str();
    }

  private:
    static std::string extract_line(std::string_view text, size_t line_num) {
        std::istringstream stream{std::string(text)};
        std::string line;
        size_t current_line = 1;
        while (std::getline(stream, line)) {
            if (current_line == line_num) {
                return line;
            }
            ++current_line;
        }
        return "";
    }

    std::vector<Diagnostic> diagnostics_;
    bool has_errors_{false};
};

}  // namespace fsm::codegen

namespace fsm {
using DiagnosticEngine = ::fsm::codegen::DiagnosticEngine;
using Diagnostic = ::fsm::codegen::Diagnostic;
using DiagnosticSeverity = ::fsm::codegen::DiagnosticSeverity;
using SourceSpan = ::fsm::codegen::SourceSpan;
}  // namespace fsm
