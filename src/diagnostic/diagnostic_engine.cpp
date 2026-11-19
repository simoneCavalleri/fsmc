#include "fsm/diagnostic/diagnostic_engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fsm::diagnostic {

DiagnosticEngine::DiagnosticEngine() {
    const char* no_color = std::getenv("NO_COLOR");
    color_enabled_ = (no_color == nullptr || no_color[0] == '\0');
}

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
    if (line_num == 0 || text.empty()) {
        return "";
    }
    size_t current_line = 1;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (current_line == line_num) {
            std::string_view line = (nl == std::string_view::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            return std::string(line);
        }
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
        ++current_line;
    }
    return "";
}

std::string DiagnosticEngine::render_to_string(std::string_view source_content, const SourceManager* sources) const {
    const auto ansi = [this](const char* seq) -> const char* {
        return color_enabled_ ? seq : "";
    };
    const auto resolve_source = [source_content, sources](const SourceSpan& span) -> std::string_view {
        if (sources != nullptr && !span.file_path.empty()) {
            if (auto found = sources->get_source(span.file_path)) {
                return *found;
            }
        }
        return source_content;
    };

    const char* c_red = ansi("\033[1;31m");
    const char* c_yellow = ansi("\033[1;33m");
    const char* c_cyan = ansi("\033[1;36m");
    const char* c_blue = ansi("\033[1;34m");
    const char* c_bold = ansi("\033[1m");
    const char* c_reset = ansi("\033[0m");

    std::ostringstream ss;
    for (const auto& diag : diagnostics_) {
        // Severity header
        switch (diag.severity) {
            case DiagnosticSeverity::Fatal:
            case DiagnosticSeverity::Error:
                ss << c_red << "error";
                if (!diag.code.empty())
                    ss << "[" << diag.code << "]";
                ss << c_reset << ": " << diag.message << "\n";
                break;
            case DiagnosticSeverity::Warning:
                ss << c_yellow << "warning";
                if (!diag.code.empty())
                    ss << "[" << diag.code << "]";
                ss << c_reset << ": " << diag.message << "\n";
                break;
            case DiagnosticSeverity::Note:
                ss << c_cyan << "note";
                if (!diag.code.empty())
                    ss << "[" << diag.code << "]";
                ss << c_reset << ": " << diag.message << "\n";
                break;
        }

        const size_t gutter_width = std::to_string(diag.span.is_valid() ? diag.span.line : 1).size() + 2;

        // Location arrow
        if (diag.span.is_valid()) {
            ss << std::string(gutter_width - 1, ' ') << c_blue << "-->" << c_reset << " " << diag.span.file_path << ":"
               << diag.span.line << ":" << diag.span.column << "\n";

            // If source line is available, print context and caret
            std::string_view primary_src = resolve_source(diag.span);
            if (!primary_src.empty()) {
                std::string line_text = extract_line(primary_src, diag.span.line);
                if (!line_text.empty()) {
                    ss << std::string(gutter_width, ' ') << c_blue << "|" << c_reset << "\n";
                    ss << " " << diag.span.line << " " << c_blue << "|" << c_reset << " " << line_text << "\n";
                    ss << std::string(gutter_width, ' ') << c_blue << "|" << c_reset << " ";
                    size_t pad = (diag.span.column > 0) ? (diag.span.column - 1) : 0;
                    for (size_t i = 0; i < pad; ++i)
                        ss << " ";
                    ss << c_red << "^";
                    for (size_t i = 1; i < diag.span.length; ++i)
                        ss << "~";
                    ss << c_reset << "\n";
                }
            }
        }

        // Secondary labels
        for (const auto& [sec_span, sec_label] : diag.secondary_labels) {
            const size_t sec_gutter = std::to_string(sec_span.is_valid() ? sec_span.line : 1).size() + 2;
            if (sec_span.is_valid()) {
                ss << std::string(sec_gutter - 1, ' ') << c_blue << "-->" << c_reset << " " << sec_span.file_path << ":"
                   << sec_span.line << ":" << sec_span.column;
                if (!sec_label.empty()) {
                    ss << " (" << sec_label << ")";
                }
                ss << "\n";
                std::string_view sec_src = resolve_source(sec_span);
                if (!sec_src.empty()) {
                    std::string sec_line_text = extract_line(sec_src, sec_span.line);
                    if (!sec_line_text.empty()) {
                        ss << std::string(sec_gutter, ' ') << c_blue << "|" << c_reset << "\n";
                        ss << " " << sec_span.line << " " << c_blue << "|" << c_reset << " " << sec_line_text << "\n";
                        ss << std::string(sec_gutter, ' ') << c_blue << "|" << c_reset << " ";
                        size_t pad = (sec_span.column > 0) ? (sec_span.column - 1) : 0;
                        for (size_t i = 0; i < pad; ++i)
                            ss << " ";
                        ss << c_cyan << "-";
                        for (size_t i = 1; i < sec_span.length; ++i)
                            ss << "-";
                        if (!sec_label.empty()) {
                            ss << " " << sec_label;
                        }
                        ss << c_reset << "\n";
                    }
                }
            } else if (!sec_label.empty()) {
                ss << std::string(gutter_width, ' ') << c_blue << "=" << c_reset << " " << c_bold << "note" << c_reset
                   << ": " << sec_label << "\n";
            }
        }

        // Help suggestion
        if (!diag.help_suggestion.empty()) {
            ss << std::string(gutter_width, ' ') << c_blue << "=" << c_reset << " " << c_bold << "help" << c_reset
               << ": " << diag.help_suggestion << "\n";
        }
        ss << "\n";
    }
    return ss.str();
}

std::string DiagnosticEngine::render_to_format(DiagnosticFormat format, std::string_view source_content,
                                               const SourceManager* sources) const {
    switch (format) {
        case DiagnosticFormat::Json:
            return render_json();
        case DiagnosticFormat::GitHub:
            return render_github_actions();
        case DiagnosticFormat::Text:
        default:
            return render_to_string(source_content, sources);
    }
}

std::string DiagnosticEngine::render_github_actions() const {
    auto escape_gh_data = [](std::string_view str) {
        std::string out;
        out.reserve(str.size());
        for (char c : str) {
            if (c == '%') {
                out += "%25";
            } else if (c == '\r') {
                out += "%0D";
            } else if (c == '\n') {
                out += "%0A";
            } else {
                out += c;
            }
        }
        return out;
    };

    auto escape_gh_property = [](std::string_view str) {
        std::string out;
        out.reserve(str.size());
        for (char c : str) {
            if (c == '%') {
                out += "%25";
            } else if (c == '\r') {
                out += "%0D";
            } else if (c == '\n') {
                out += "%0A";
            } else if (c == ':') {
                out += "%3A";
            } else if (c == ',') {
                out += "%2C";
            } else {
                out += c;
            }
        }
        return out;
    };

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
            params += "file=" + escape_gh_property(diag.span.file_path) + ",line=" + std::to_string(diag.span.line) +
                      ",col=" + std::to_string(diag.span.column);
        }
        if (!diag.code.empty()) {
            if (!params.empty())
                params += ",";
            params += "title=" + escape_gh_property(diag.code);
        }
        if (!params.empty()) {
            ss << " " << params;
        }
        ss << "::" << escape_gh_data(diag.message);
        if (!diag.help_suggestion.empty()) {
            ss << " | Help: " << escape_gh_data(diag.help_suggestion);
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
        if (!diag.secondary_labels.empty()) {
            ss << "    \"secondary_labels\": [\n";
            for (size_t j = 0; j < diag.secondary_labels.size(); ++j) {
                const auto& [sec_span, sec_msg] = diag.secondary_labels[j];
                ss << "      {\"file\": \"" << escape_json_str(sec_span.file_path) << "\", \"line\": " << sec_span.line
                   << ", \"column\": " << sec_span.column << ", \"length\": " << sec_span.length << ", \"label\": \""
                   << escape_json_str(sec_msg) << "\"}";
                if (j + 1 < diag.secondary_labels.size()) {
                    ss << ",";
                }
                ss << "\n";
            }
            ss << "    ],\n";
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
