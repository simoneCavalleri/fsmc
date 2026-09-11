#include "fsm/frontend/common/companion_manifest_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace fsm::frontend {

namespace {

struct LineInfo {
    size_t indent = 0;
    std::string text;
};

std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        start++;
    }
    if (start == s.size())
        return "";
    size_t end = s.size() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end])) != 0) {
        end--;
    }
    return std::string(s.substr(start, end - start + 1));
}

std::string unquote(std::string_view s) {
    std::string str = trim(s);
    if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') || (str.front() == '\'' && str.back() == '\''))) {
        return str.substr(1, str.size() - 2);
    }
    if (!str.empty() && str.back() == ',') {
        str.pop_back();
        return unquote(str);
    }
    return str;
}

std::vector<LineInfo> preprocess_lines(std::string_view content) {
    std::vector<LineInfo> result;
    std::istringstream stream{std::string(content)};
    std::string raw_line;

    while (std::getline(stream, raw_line)) {
        std::string cleaned;
        bool in_single_quote = false;
        bool in_double_quote = false;
        for (size_t i = 0; i < raw_line.size(); ++i) {
            char c = raw_line[i];
            if (c == '\'' && !in_double_quote)
                in_single_quote = !in_single_quote;
            if (c == '"' && !in_single_quote)
                in_double_quote = !in_double_quote;
            if (c == '#' && !in_single_quote && !in_double_quote) {
                break;
            }
            cleaned += c;
        }

        size_t first_non_space = cleaned.find_first_not_of(" \t");
        if (first_non_space == std::string::npos) {
            continue;
        }

        size_t indent = 0;
        for (size_t i = 0; i < first_non_space; ++i) {
            if (cleaned[i] == '\t')
                indent += 2;
            else
                indent++;
        }

        std::string trimmed = trim(cleaned.substr(first_non_space));
        if (!trimmed.empty()) {
            result.push_back({indent, std::move(trimmed)});
        }
    }
    return result;
}

std::pair<std::string, std::string> split_key_value(std::string_view text) {
    size_t colon_pos = text.find(':');
    if (colon_pos == std::string_view::npos) {
        return {trim(text), ""};
    }
    std::string key = trim(text.substr(0, colon_pos));
    std::string val = trim(text.substr(colon_pos + 1));
    return {key, val};
}

void parse_fsm_section(const std::vector<LineInfo>& lines, size_t& idx, CompanionManifest& manifest) {
    while (idx < lines.size() && lines[idx].indent > 0) {
        std::string text = lines[idx].text;
        if (text == "{" || text == "}" || text == "},") {
            idx++;
            continue;
        }
        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (k == "package") {
            manifest.package_name = unquote(v);
        } else if (k == "name") {
            manifest.fsm_name = unquote(v);
        } else if (k == "initial") {
            manifest.initial_state = unquote(v);
        }
        idx++;
    }
}

void parse_ports_section(const std::vector<LineInfo>& lines, size_t& idx, std::vector<CompanionPort>& ports) {
    CompanionPort current;
    bool has_current = false;

    while (idx < lines.size() && lines[idx].indent > 0) {
        const auto& line = lines[idx];
        std::string text = line.text;

        if (text == "{" || text.rfind("- ", 0) == 0) {
            if (has_current) {
                ports.push_back(std::move(current));
                current = CompanionPort{};
            }
            has_current = true;
            if (text.rfind("- ", 0) == 0) {
                text = trim(text.substr(2));
            } else {
                idx++;
                continue;
            }
        }
        if (text == "}" || text == "}," || text == "]" || text == "],") {
            if (has_current) {
                ports.push_back(std::move(current));
                current = CompanionPort{};
                has_current = false;
            }
            idx++;
            continue;
        }

        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (k == "name") {
            current.name = unquote(v);
        } else if (k == "type") {
            current.type = unquote(v);
        } else if (k == "direction" || k == "dir") {
            current.direction = unquote(v);
        } else if (k == "constraint") {
            current.constraint = unquote(v);
        } else if (k == "min") {
            try {
                current.min_value = std::stod(unquote(v));
            } catch (...) {
            }
        } else if (k == "max") {
            try {
                current.max_value = std::stod(unquote(v));
            } catch (...) {
            }
        }
        idx++;
    }

    if (has_current) {
        ports.push_back(std::move(current));
    }
}

void parse_variables_section(const std::vector<LineInfo>& lines, size_t& idx, std::vector<CompanionVariable>& vars) {
    CompanionVariable current;
    bool has_current = false;

    while (idx < lines.size() && lines[idx].indent > 0) {
        const auto& line = lines[idx];
        std::string text = line.text;

        if (text == "{" || text.rfind("- ", 0) == 0) {
            if (has_current) {
                vars.push_back(std::move(current));
                current = CompanionVariable{};
            }
            has_current = true;
            if (text.rfind("- ", 0) == 0) {
                text = trim(text.substr(2));
            } else {
                idx++;
                continue;
            }
        }
        if (text == "}" || text == "}," || text == "]" || text == "],") {
            if (has_current) {
                vars.push_back(std::move(current));
                current = CompanionVariable{};
                has_current = false;
            }
            idx++;
            continue;
        }

        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (k == "name") {
            current.name = unquote(v);
        } else if (k == "type") {
            current.type = unquote(v);
        } else if (k == "initial" || k == "init" || k == "default") {
            current.initial_value = unquote(v);
        } else if (k == "unit") {
            current.unit = unquote(v);
        } else if (k == "min") {
            try {
                current.min_value = std::stod(unquote(v));
            } catch (...) {
            }
        } else if (k == "max") {
            try {
                current.max_value = std::stod(unquote(v));
            } catch (...) {
            }
        }
        idx++;
    }

    if (has_current) {
        vars.push_back(std::move(current));
    }
}

void parse_signals_section(const std::vector<LineInfo>& lines, size_t& idx, std::vector<CompanionSignal>& signals) {
    CompanionSignal current;
    bool has_current = false;
    bool in_attributes = false;
    CompanionSignalAttr current_attr;
    bool has_current_attr = false;

    while (idx < lines.size() && lines[idx].indent > 0) {
        const auto& line = lines[idx];
        std::string text = line.text;

        if (text == "]" || text == "],") {
            if (in_attributes) {
                if (has_current_attr) {
                    current.attributes.push_back(std::move(current_attr));
                    current_attr = CompanionSignalAttr{};
                    has_current_attr = false;
                }
                in_attributes = false;
                idx++;
                continue;
            }
        }

        if (text == "}" || text == "},") {
            if (has_current_attr) {
                current.attributes.push_back(std::move(current_attr));
                current_attr = CompanionSignalAttr{};
                has_current_attr = false;
                idx++;
                continue;
            }
            if (has_current) {
                signals.push_back(std::move(current));
                current = CompanionSignal{};
                has_current = false;
                idx++;
                continue;
            }
        }

        if (text.rfind("- ", 0) == 0 && line.indent <= 4) {
            if (has_current_attr) {
                current.attributes.push_back(std::move(current_attr));
                current_attr = CompanionSignalAttr{};
                has_current_attr = false;
            }
            if (has_current) {
                signals.push_back(std::move(current));
                current = CompanionSignal{};
            }
            has_current = true;
            in_attributes = false;
            text = trim(text.substr(2));
        } else if (text == "{" && !in_attributes) {
            if (has_current) {
                signals.push_back(std::move(current));
                current = CompanionSignal{};
            }
            has_current = true;
            idx++;
            continue;
        }

        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (k == "attributes" || k == "attrs") {
            in_attributes = true;
        } else if (in_attributes) {
            if (text == "{") {
                if (has_current_attr) {
                    current.attributes.push_back(std::move(current_attr));
                    current_attr = CompanionSignalAttr{};
                }
                has_current_attr = true;
                idx++;
                continue;
            }
            if (text.rfind("- ", 0) == 0) {
                if (has_current_attr) {
                    current.attributes.push_back(std::move(current_attr));
                    current_attr = CompanionSignalAttr{};
                }
                has_current_attr = true;
                text = trim(text.substr(2));
                auto [sub_k, sub_v] = split_key_value(text);
                sub_k = unquote(sub_k);
                if (sub_k == "name") {
                    current_attr.name = unquote(sub_v);
                } else if (sub_k == "type") {
                    current_attr.type = unquote(sub_v);
                } else if (sub_k == "default") {
                    current_attr.default_value = unquote(sub_v);
                }
            } else if (k == "name") {
                current_attr.name = unquote(v);
            } else if (k == "type") {
                current_attr.type = unquote(v);
            } else if (k == "default") {
                current_attr.default_value = unquote(v);
            }
        } else {
            if (k == "name") {
                current.name = unquote(v);
            }
        }
        idx++;
    }

    if (has_current_attr) {
        current.attributes.push_back(std::move(current_attr));
    }
    if (has_current) {
        signals.push_back(std::move(current));
    }
}

void parse_invariants_section(const std::vector<LineInfo>& lines, size_t& idx,
                              std::unordered_map<std::string, std::string>& invariants) {
    while (idx < lines.size() && lines[idx].indent > 0) {
        std::string text = lines[idx].text;
        if (text == "{" || text == "}" || text == "},") {
            idx++;
            continue;
        }
        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (!k.empty() && !v.empty()) {
            invariants[k] = unquote(v);
        }
        idx++;
    }
}

void parse_properties_section(const std::vector<LineInfo>& lines, size_t& idx, std::vector<CompanionProperty>& props) {
    CompanionProperty current;
    bool has_current = false;

    while (idx < lines.size() && lines[idx].indent > 0) {
        const auto& line = lines[idx];
        std::string text = line.text;

        if (text == "{" || text.rfind("- ", 0) == 0) {
            if (has_current) {
                props.push_back(std::move(current));
                current = CompanionProperty{};
            }
            has_current = true;
            if (text.rfind("- ", 0) == 0) {
                text = trim(text.substr(2));
            } else {
                idx++;
                continue;
            }
        }
        if (text == "}" || text == "}," || text == "]" || text == "],") {
            if (has_current) {
                props.push_back(std::move(current));
                current = CompanionProperty{};
                has_current = false;
            }
            idx++;
            continue;
        }

        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (k == "name") {
            current.name = unquote(v);
        } else if (k == "formula" || k == "ltl" || k == "ctl") {
            current.formula = unquote(v);
        }
        idx++;
    }

    if (has_current) {
        props.push_back(std::move(current));
    }
}

void parse_actions_section(const std::vector<LineInfo>& lines, size_t& idx, std::vector<CompanionAction>& actions) {
    CompanionAction current;
    bool has_current = false;

    while (idx < lines.size() && lines[idx].indent > 0) {
        const auto& line = lines[idx];
        std::string text = line.text;

        if (text == "{" || text.rfind("- ", 0) == 0) {
            if (has_current) {
                actions.push_back(std::move(current));
                current = CompanionAction{};
            }
            has_current = true;
            if (text.rfind("- ", 0) == 0) {
                text = trim(text.substr(2));
            } else {
                idx++;
                continue;
            }
        }
        if (text == "}" || text == "}," || text == "]" || text == "],") {
            if (has_current) {
                actions.push_back(std::move(current));
                current = CompanionAction{};
                has_current = false;
            }
            idx++;
            continue;
        }

        auto [k, v] = split_key_value(text);
        k = unquote(k);
        if (k == "name") {
            current.name = unquote(v);
        } else if (k == "signature") {
            current.signature = unquote(v);
        } else if (k == "inv") {
            current.inv = unquote(v);
        }
        idx++;
    }

    if (has_current) {
        actions.push_back(std::move(current));
    }
}

void parse_requirements_section(const std::vector<LineInfo>& lines, size_t& idx, std::vector<std::string>& reqs) {
    while (idx < lines.size() && lines[idx].indent > 0) {
        std::string text = lines[idx].text;
        if (text == "[" || text == "]" || text == "],") {
            idx++;
            continue;
        }
        if (text.rfind("- ", 0) == 0) {
            text = trim(text.substr(2));
        }
        if (!text.empty()) {
            reqs.push_back(unquote(text));
        }
        idx++;
    }
}

}  // namespace

bool CompanionManifestParser::parse(std::string_view content, CompanionManifest& manifest, std::string& error_message) {
    (void)error_message;
    std::vector<LineInfo> lines = preprocess_lines(content);
    if (lines.empty()) {
        return true;
    }

    // If wrapped in root JSON object { ... }, strip outer { and } and shift indentation
    if (!lines.empty() && lines.front().text == "{" && (lines.back().text == "}" || lines.back().text == "};")) {
        lines.erase(lines.begin());
        if (!lines.empty())
            lines.pop_back();
        size_t min_indent = 1000;
        for (const auto& l : lines) {
            if (l.indent < min_indent)
                min_indent = l.indent;
        }
        for (auto& l : lines) {
            if (l.indent >= min_indent)
                l.indent -= min_indent;
        }
    }

    size_t idx = 0;
    while (idx < lines.size()) {
        const auto& line = lines[idx];
        if (line.indent != 0) {
            idx++;
            continue;
        }

        auto [key, val] = split_key_value(line.text);
        key = unquote(key);
        if (key == "fsm" || key == "machine" || key == "contract") {
            idx++;
            parse_fsm_section(lines, idx, manifest);
        } else if (key == "ports") {
            idx++;
            parse_ports_section(lines, idx, manifest.ports);
        } else if (key == "variables" || key == "vars") {
            idx++;
            parse_variables_section(lines, idx, manifest.variables);
        } else if (key == "signals" || key == "events") {
            idx++;
            parse_signals_section(lines, idx, manifest.signals);
        } else if (key == "invariants" || key == "durations") {
            idx++;
            parse_invariants_section(lines, idx, manifest.invariants);
        } else if (key == "properties" || key == "specs") {
            idx++;
            parse_properties_section(lines, idx, manifest.properties);
        } else if (key == "actions") {
            idx++;
            parse_actions_section(lines, idx, manifest.actions);
        } else if (key == "requirements" || key == "reqs") {
            idx++;
            parse_requirements_section(lines, idx, manifest.requirements);
        } else if (key == "package") {
            manifest.package_name = unquote(val);
            idx++;
        } else if (key == "name") {
            manifest.fsm_name = unquote(val);
            idx++;
        } else if (key == "initial") {
            manifest.initial_state = unquote(val);
            idx++;
        } else {
            idx++;
        }
    }

    return true;
}

}  // namespace fsm::frontend
