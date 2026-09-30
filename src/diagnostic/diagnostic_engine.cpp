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

std::string DiagnosticEngine::render_to_format(DiagnosticFormat format, std::string_view source_content) const {
    switch (format) {
        case DiagnosticFormat::Json:
            return render_json();
        case DiagnosticFormat::GitHub:
            return render_github_actions();
        case DiagnosticFormat::Text:
        default:
            return render_to_string(source_content);
    }
}

std::string DiagnosticEngine::render_github_actions() const {
    std::ostringstream ss;
    for (const auto& diag : diagnostics_) {
        std::string cmd;
        switch (diag.severity) {
            case DiagnosticSeverity::Fatal:
            case DiagnosticSeverity::Error:
                cmd = "error";
                break;
            case DiagnosticSeverity::Warning:
                cmd = "warning";
                break;
            case DiagnosticSeverity::Note:
            default:
                cmd = "notice";
                break;
        }

        ss << "::" << cmd;
        std::string params;
        if (diag.span.is_valid()) {
            params += "file=" + diag.span.file_path + ",line=" + std::to_string(diag.span.line) +
                      ",col=" + std::to_string(diag.span.column);
        }
        if (!diag.code.empty()) {
            if (!params.empty())
                params += ",";
            params += "title=" + diag.code;
        }
        if (!params.empty()) {
            ss << " " << params;
        }
        ss << "::" << diag.message;
        if (!diag.help_suggestion.empty()) {
            ss << " | Help: " << diag.help_suggestion;
        }
        ss << "\n";
    }
    return ss.str();
}

namespace {

std::string escape_json_str(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

}  // namespace

std::string DiagnosticEngine::render_json() const {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < diagnostics_.size(); ++i) {
        const auto& diag = diagnostics_[i];
        std::string sev_str;
        switch (diag.severity) {
            case DiagnosticSeverity::Fatal:
                sev_str = "fatal";
                break;
            case DiagnosticSeverity::Error:
                sev_str = "error";
                break;
            case DiagnosticSeverity::Warning:
                sev_str = "warning";
                break;
            case DiagnosticSeverity::Note:
                sev_str = "note";
                break;
        }

        ss << "  {\n";
        ss << "    \"severity\": \"" << sev_str << "\",\n";
        ss << "    \"code\": \"" << escape_json_str(diag.code) << "\",\n";
        ss << "    \"message\": \"" << escape_json_str(diag.message) << "\",\n";
        if (diag.span.is_valid()) {
            ss << "    \"file\": \"" << escape_json_str(diag.span.file_path) << "\",\n";
            ss << "    \"line\": " << diag.span.line << ",\n";
            ss << "    \"column\": " << diag.span.column << ",\n";
            ss << "    \"length\": " << diag.span.length << ",\n";
        }
        ss << "    \"help\": \"" << escape_json_str(diag.help_suggestion) << "\"\n";
        ss << "  }";
        if (i + 1 < diagnostics_.size()) {
            ss << ",";
        }
        ss << "\n";
    }
    ss << "]\n";
    return ss.str();
}

}  // namespace fsm::diagnostic
