#include "stateflow_label_parser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "fsm/frontend/common/lexer_utils.hpp"
#include "fsm/frontend/formal/stateflow/stateflow_parser.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;

StateflowParser::StateflowLabelComponents StateflowParser::parse_stateflow_label(std::string_view raw_label) {
    StateflowLabelComponents res;
    std::string label = std::string(trim(raw_label));
    if (label.empty()) {
        return res;
    }

    // Check for temporal logic after/every/at(N, sec / msec)
    static const std::regex after_re(R"(after\s*\(\s*(\d+(?:\.\d+)?)\s*,\s*(sec|msec|seconds|milliseconds|s|ms)\s*\))",
                                     std::regex::optimize);
    static const std::regex every_re(R"(every\s*\(\s*(\d+(?:\.\d+)?)\s*,\s*(sec|msec|seconds|milliseconds|s|ms)\s*\))",
                                     std::regex::optimize);
    static const std::regex at_re(R"(at\s*\(\s*(\d+(?:\.\d+)?)\s*,\s*(sec|msec|seconds|milliseconds|s|ms)\s*\))",
                                  std::regex::optimize);
    std::smatch match;
    if (std::regex_search(label, match, after_re)) {
        double val = std::stod(match[1].str());
        std::string unit = match[2].str();
        // Apply unit multiplier BEFORE casting to uint64_t to avoid fractional truncation.
        // after(1.5, s) must give 1500 ms, not 1 ms.
        double dur_ms_f = val;
        if (unit == "sec" || unit == "s" || unit == "seconds") {
            dur_ms_f = val * 1000.0;
        }
        uint64_t dur_ms = static_cast<uint64_t>(dur_ms_f + 0.5);  // round to nearest ms
        if (dur_ms == 0)
            dur_ms = 1;
        res.time_trigger = TimeTrigger(TimeTriggerKind::After, dur_ms, TimeUnit::Milliseconds);
        res.event = "after_" + std::to_string(dur_ms) + "ms";
    } else if (std::regex_search(label, match, every_re)) {
        double val = std::stod(match[1].str());
        std::string unit = match[2].str();
        double dur_ms_f = val;
        if (unit == "sec" || unit == "s" || unit == "seconds") {
            dur_ms_f = val * 1000.0;
        }
        uint64_t dur_ms = static_cast<uint64_t>(dur_ms_f + 0.5);
        if (dur_ms == 0)
            dur_ms = 1;
        res.time_trigger = TimeTrigger(TimeTriggerKind::Every, dur_ms, TimeUnit::Milliseconds);
        res.event = "every_" + std::to_string(dur_ms) + "ms";
    } else if (std::regex_search(label, match, at_re)) {
        double val = std::stod(match[1].str());
        std::string unit = match[2].str();
        double dur_ms_f = val;
        if (unit == "sec" || unit == "s" || unit == "seconds") {
            dur_ms_f = val * 1000.0;
        }
        uint64_t dur_ms = static_cast<uint64_t>(dur_ms_f + 0.5);
        if (dur_ms == 0)
            dur_ms = 1;
        res.time_trigger = TimeTrigger(TimeTriggerKind::At, dur_ms, TimeUnit::Milliseconds);
        res.event = "at_" + std::to_string(dur_ms) + "ms";
    }

    size_t idx = 0;
    const size_t len = label.size();

    // 1. Event part: up to '[' or '{' or '/' (if not already matched temporal)
    if (res.event.empty()) {
        size_t ev_end = 0;
        while (ev_end < len && label[ev_end] != '[' && label[ev_end] != '{' && label[ev_end] != '/') {
            ev_end++;
        }
        res.event = std::string(trim(label.substr(0, ev_end)));
        idx = ev_end;
    } else {
        size_t first_delim = label.find_first_of("[{/");
        if (first_delim != std::string::npos) {
            idx = first_delim;
        } else {
            idx = len;
        }
    }

    // 2. Scan brackets and braces with nesting support
    while (idx < len) {
        char c = label[idx];
        if (c == '[') {
            idx++;
            size_t depth = 1;
            size_t g_start = idx;
            while (idx < len && depth > 0) {
                if (label[idx] == '[') {
                    depth++;
                } else if (label[idx] == ']') {
                    depth--;
                }
                if (depth > 0) {
                    idx++;
                }
            }
            res.guard = std::string(trim(label.substr(g_start, idx - g_start)));
            if (idx < len && label[idx] == ']') {
                idx++;
            }
        } else if (c == '{') {
            idx++;
            size_t depth = 1;
            size_t ca_start = idx;
            while (idx < len && depth > 0) {
                if (label[idx] == '{') {
                    depth++;
                } else if (label[idx] == '}') {
                    depth--;
                }
                if (depth > 0) {
                    idx++;
                }
            }
            res.condition_action = std::string(trim(label.substr(ca_start, idx - ca_start)));
            if (idx < len && label[idx] == '}') {
                idx++;
            }
        } else if (c == '/') {
            idx++;
            while (idx < len && (std::isspace(static_cast<unsigned char>(label[idx])) != 0)) {
                idx++;
            }
            if (idx < len && label[idx] == '{') {
                idx++;
                size_t depth = 1;
                size_t ta_start = idx;
                while (idx < len && depth > 0) {
                    if (label[idx] == '{') {
                        depth++;
                    } else if (label[idx] == '}') {
                        depth--;
                    }
                    if (depth > 0) {
                        idx++;
                    }
                }
                res.transition_action = std::string(trim(label.substr(ta_start, idx - ta_start)));
                if (idx < len && label[idx] == '}') {
                    idx++;
                }
            } else {
                res.transition_action = std::string(trim(label.substr(idx)));
                idx = len;
            }
        } else {
            idx++;
        }
    }

    return res;
}

namespace stateflow_detail {

ActionSignature build_stateflow_action(std::string_view raw_action, const std::string& fallback_prefix) {
    static const std::regex assign_regex(
        R"(^((?:(?:out|in|reg|local)\.)?)?(?:(?:service|context)\.)?([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(=|\+=|-=|\*=|\/=)\s*(.+)$)",
        std::regex::optimize);
    static const std::regex inc_regex(
        R"(^((?:(?:out|in|reg|local)\.)?)?(?:(?:service|context)\.)?([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*(\+\+|--)$)",
        std::regex::optimize);

    std::vector<ActionAssignment> assignments;
    std::string raw_str(raw_action);
    std::stringstream ss(raw_str);
    std::string stmt;
    while (std::getline(ss, stmt, ';')) {
        size_t first = stmt.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        size_t last = stmt.find_last_not_of(" \t\r\n");
        std::string trimmed = stmt.substr(first, last - first + 1);
        std::smatch m;
        if (std::regex_match(trimmed, m, assign_regex)) {
            std::string scope_prefix = m[1].str();
            std::string var_path = m[2].str();
            std::string op = m[3].str();
            std::string rhs = m[4].str();
            size_t rfirst = rhs.find_first_not_of(" \t\r\n");
            size_t rlast = rhs.find_last_not_of(" \t\r\n");
            if (rfirst != std::string::npos) {
                rhs = rhs.substr(rfirst, rlast - rfirst + 1);
            }
            LValueTarget tgt(scope_prefix + var_path);
            if (op == "=") {
                assignments.push_back({tgt, rhs});
            } else if (op == "+=") {
                assignments.push_back({tgt, var_path + " + " + rhs});
            } else if (op == "-=") {
                assignments.push_back({tgt, var_path + " - " + rhs});
            } else if (op == "*=") {
                assignments.push_back({tgt, var_path + " * (" + rhs + ")"});
            } else if (op == "/=") {
                assignments.push_back({tgt, var_path + " / (" + rhs + ")"});
            }
        } else if (std::regex_match(trimmed, m, inc_regex)) {
            std::string scope_prefix = m[1].str();
            std::string var_path = m[2].str();
            std::string op = m[3].str();
            LValueTarget tgt(scope_prefix + var_path);
            assignments.push_back({tgt, op == "++" ? (var_path + " + 1") : (var_path + " - 1")});
        }
    }

    if (!assignments.empty()) {
        std::string name = fallback_prefix + "_" + sanitize_identifier(assignments.front().target.name);
        ActionSignature sig(std::move(name));
        sig.assignments = std::move(assignments);
        return sig;
    }
    return ActionSignature(sanitize_identifier(raw_action));
}

}  // namespace stateflow_detail

}  // namespace fsm::frontend::formal
