#pragma once

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codegen/fsm_model.hpp"
#include "codegen/parser_interface.hpp"

namespace fsm::codegen {

class DotParser : public IParser {
  public:
    [[nodiscard]] static std::string format_name() { return "dot"; }

    bool parse(std::string_view content, FsmModel& model, std::string& error_message) override {
        std::istringstream stream{std::string(content)};
        std::string line;
        std::string current_parent_state;
        std::vector<std::string> parent_stack;
        std::map<std::string, bool> point_nodes;

        // Regex helpers
        const std::regex digraph_regex(R"(^\s*(?:digraph|graph)\s+([A-Za-z0-9_]+))", std::regex::icase);
        const std::regex subgraph_regex(R"(^\s*subgraph\s+(?:cluster_)?([A-Za-z0-9_]+)\s*\{?)", std::regex::icase);
        const std::regex node_point_regex(R"(^\s*([A-Za-z0-9_]+)\s*\[.*shape\s*=\s*(?:point|circle|none).*\].*)",
                                          std::regex::icase);
        const std::regex edge_regex(
            R"(^\s*([A-Za-z0-9_\[\]\*]+)\s*(?:->|--)\s*([A-Za-z0-9_\[\]\*]+)(?:\s*\[(.*)\])?.*)");
        const std::regex label_attr_regex(R"raw(label\s*=\s*"([^"]*)")raw", std::regex::icase);

        while (std::getline(stream, line)) {
            std::string trimmed = trim_line(line);
            if (trimmed.empty() || starts_with(trimmed, "//") || starts_with(trimmed, "#") ||
                starts_with(trimmed, "/*")) {
                continue;
            }

            // Check digraph Name {
            std::smatch match;
            if (std::regex_search(trimmed, match, digraph_regex)) {
                model.name = sanitize_identifier(match[1].str());
                continue;
            }

            // Check subgraph cluster_CompositeState {
            if (std::regex_search(trimmed, match, subgraph_regex)) {
                std::string comp_name = sanitize_identifier(match[1].str());
                model.add_state(comp_name, current_parent_state);
                auto* parent_state = model.find_state_mut(comp_name);
                if (parent_state != nullptr) {
                    parent_state->is_composite = true;
                }
                parent_stack.push_back(current_parent_state);
                current_parent_state = comp_name;
                continue;
            }

            // Closing brace }
            if (trimmed == "}" || trimmed == "};") {
                if (!parent_stack.empty()) {
                    current_parent_state = parent_stack.back();
                    parent_stack.pop_back();
                }
                continue;
            }

            // Check point / initial node declaration (e.g. init [shape=point];)
            if (std::regex_search(trimmed, match, node_point_regex)) {
                std::string node_name = match[1].str();
                point_nodes[node_name] = true;
                continue;
            }

            // Check edge transition S1 -> S2 [label="..."]
            if (std::regex_search(trimmed, match, edge_regex)) {
                std::string src_raw = match[1].str();
                std::string dst_raw = match[2].str();
                std::string attrs = match[3].matched ? match[3].str() : "";

                std::string label_str;
                std::smatch label_match;
                if (std::regex_search(attrs, label_match, label_attr_regex)) {
                    label_str = label_match[1].str();
                } else if (!attrs.empty() && attrs.find('=') == std::string::npos) {
                    label_str = attrs;
                }

                // Is initial transition?
                if (point_nodes.count(src_raw) != 0 || src_raw == "[*]" || src_raw == "__start__" ||
                    src_raw == "start" || src_raw == "init" || src_raw == "initial") {
                    std::string init_target = sanitize_identifier(dst_raw);
                    if (current_parent_state.empty()) {
                        model.initial_state = init_target;
                    } else {
                        auto* parent = model.find_state_mut(current_parent_state);
                        if (parent != nullptr) {
                            parent->initial_sub_state = init_target;
                        }
                    }
                    model.add_state(init_target, current_parent_state);
                    continue;
                }

                std::string src = sanitize_identifier(src_raw);
                std::string dst = sanitize_identifier(dst_raw);

                // Parse label: Event [Guard] / Action
                std::string event_name;
                std::string guard_name;
                std::string action_name;
                parse_label(label_str, event_name, guard_name, action_name);

                TransitionModel trans;
                trans.source = src;
                trans.target = dst;
                trans.event = sanitize_identifier(event_name);
                if (!guard_name.empty()) {
                    trans.guard = sanitize_identifier(guard_name);
                    model.add_guard(*trans.guard);
                }
                if (!action_name.empty()) {
                    trans.action = sanitize_identifier(action_name);
                    model.add_action(*trans.action);
                }
                trans.kind = (src == dst && attrs.find("internal") != std::string::npos) ? TransitionKind::Internal
                                                                                         : TransitionKind::External;

                model.add_state(src, current_parent_state);
                model.add_state(dst, current_parent_state);
                if (!trans.event.empty()) {
                    model.add_event(trans.event);
                }

                model.add_transition(std::move(trans));
            }
        }

        if (model.states.empty()) {
            error_message = "DOT Parser: No valid states or transitions extracted.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
    static std::string trim_line(std::string_view line_sv) {
        size_t start = 0;
        while (start < line_sv.size() && (std::isspace(static_cast<unsigned char>(line_sv[start])) != 0)) {
            start++;
        }
        if (start == line_sv.size()) {
            return "";
        }
        size_t end = line_sv.size() - 1;
        while (end > start && (std::isspace(static_cast<unsigned char>(line_sv[end])) != 0 || line_sv[end] == ';')) {
            end--;
        }
        return std::string(line_sv.substr(start, end - start + 1));
    }

    static void parse_label(const std::string& label, std::string& out_event, std::string& out_guard,
                            std::string& out_action) {
        if (label.empty()) {
            return;
        }

        std::string work = label;
        // Check Action / ActionName
        size_t slash_pos = work.find('/');
        if (slash_pos != std::string::npos) {
            out_action = trim_line(work.substr(slash_pos + 1));
            work = work.substr(0, slash_pos);
        }

        // Check Guard [GuardName]
        size_t lbracket = work.find('[');
        size_t rbracket = work.find(']', lbracket);
        if (lbracket != std::string::npos && rbracket != std::string::npos) {
            out_guard = trim_line(work.substr(lbracket + 1, rbracket - lbracket - 1));
            work = work.substr(0, lbracket);
        }

        // Remainder is Event
        out_event = trim_line(work);
    }
};

}  // namespace fsm::codegen
