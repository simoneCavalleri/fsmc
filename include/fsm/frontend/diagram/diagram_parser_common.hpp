/**
 * @file diagram_parser_common.hpp
 * @brief Shared guard extraction and registration utilities for diagram parsers (Mermaid, PlantUML).
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::diagram::detail {

/**
 * @brief Finds the first '[' and its matching ']' accounting for nested '[...]' brackets and string literals.
 * @param text Input string view to scan.
 * @param[out] out_open Index of the opening '['.
 * @param[out] out_close Index of the matching closing ']'.
 * @return True if a balanced bracket pair was found, false otherwise.
 */
[[nodiscard]] inline bool extract_balanced_square_brackets(std::string_view text, std::size_t& out_open,
                                                           std::size_t& out_close) noexcept {
    const std::size_t open_pos = text.find('[');
    if (open_pos == std::string_view::npos) {
        return false;
    }

    std::size_t depth = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (std::size_t i = open_pos; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && (in_single_quote || in_double_quote) && i + 1 < text.size()) {
            ++i;
            continue;
        }
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }
        if (in_single_quote || in_double_quote) {
            continue;
        }

        if (c == '[') {
            ++depth;
        } else if (c == ']') {
            if (depth > 0) {
                --depth;
                if (depth == 0) {
                    out_open = open_pos;
                    out_close = i;
                    return true;
                }
            }
        }
    }

    return false;
}

/**
 * @brief Parses a raw guard expression, populates the transition guard, and registers atomic guards on the IR model.
 * @param model Target FsmIr model to register guard definitions into.
 * @param trans Target TransitionEdge to populate.
 * @param guard_str Raw guard condition text inside '[...]'.
 * @param fallback_src Source state name for synthetic fallback identifier generation.
 * @param fallback_dst Target state name for synthetic fallback identifier generation.
 */
inline void apply_parsed_guard(ir::FsmIr& model, ir::TransitionEdge& trans, std::string_view guard_str,
                               std::string_view fallback_src, std::string_view fallback_dst) {
    const std::string_view trimmed_guard = trim(guard_str);
    if (trimmed_guard.empty()) {
        return;
    }

    const auto parsed = directive::GuardExpressionParser::parse(trimmed_guard);
    if (!parsed.cpp_type.empty()) {
        trans.guard = parsed.cpp_type;
        for (const auto& detail : parsed.atomic_guard_details) {
            model.add_guard(detail.name, "", detail.expression, detail.expression);
        }
        for (const auto& atomic : parsed.atomic_guards) {
            model.add_guard(atomic);
        }
    } else {
        std::string g = sanitize_identifier(trimmed_guard);
        if (g.empty()) {
            g = "guard_" + sanitize_identifier(fallback_src) + "_to_" + sanitize_identifier(fallback_dst) + "_" +
                std::to_string(model.transitions.size());
        }
        trans.guard = g;
        model.add_guard(g, "", std::string(trimmed_guard), std::string(trimmed_guard));
    }
}

/**
 * @brief Ensures synthesized assignment action names do not collide across distinct actions in a model.
 * @param model Target FsmIr model containing already-registered action names.
 * @param sig ActionSignature whose synthesized name will be disambiguated if already taken.
 */
inline void disambiguate_synthesized_action(const ir::FsmIr& model, ir::ActionSignature& sig) {
    if (sig.assignments.empty() || sig.name.empty()) {
        return;
    }
    auto has_action = [&](const std::string& candidate) {
        for (const auto& act : model.actions) {
            if (act.name == candidate) {
                return true;
            }
        }
        return false;
    };
    if (!has_action(sig.name)) {
        return;
    }
    const std::string base = sig.name;
    std::size_t suffix = 2;
    while (has_action(base + "_" + std::to_string(suffix))) {
        ++suffix;
    }
    sig.name = base + "_" + std::to_string(suffix);
}

}  // namespace fsm::frontend::diagram::detail
