#include "fsm/diagnostic/diagnostic_engine.hpp"

#include <sstream>

namespace fsm::diagnostic {

void DiagnosticEngine::report(Diagnostic diag) {
    if (diag.severity == DiagnosticSeverity::Error || diag.severity == DiagnosticSeverity::Fatal) {
        has_errors_ = true;
    }
    diagnostics_.push_back(std::move(diag));
}

bool DiagnosticEngine::has_errors() const noexcept {
    return has_errors_;
}

bool DiagnosticEngine::has_warnings() const noexcept {
    for (const auto& diag : diagnostics_) {
        if (diag.severity == DiagnosticSeverity::Warning) {
            return true;
        }
    }
    return false;
}

const std::vector<Diagnostic>& DiagnosticEngine::get_diagnostics() const noexcept {
    return diagnostics_;
}

void DiagnosticEngine::clear() noexcept {
    diagnostics_.clear();
    has_errors_ = false;
}

std::string DiagnosticEngine::extract_line(std::string_view text, size_t line_num) {
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

std::string DiagnosticEngine::render_to_string(std::string_view source_content) const {
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
            ss << "  \033[1;34m-->\033[0m " << diag.span.file_path << ":" << diag.span.line << ":" << diag.span.column
               << "\n";

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

}  // namespace fsm::diagnostic
