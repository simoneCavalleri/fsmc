#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"

#include <cctype>
#include <functional>
#include <regex>
#include <sstream>
#include <utility>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/frontend/directive/ltl_parser.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using directive::DirectiveParser;
using directive::GuardExpressionParser;

/**
 * @brief Parses SysML v2 State Machine definitions into canonical FsmIr representation.
 *
 * Implements a full token scanning and recursive block processing pass:
 * 1. Scans raw content into lexical tokens and blocks using Sysml2BlockScanner.
 * 2. Processes state machine directives (@fsm:var, @fsm:property, @fsm:signal, @fsm:defer).
 * 3. Tracks lexical block nesting using a block stack and state stack.
 * 4. Resolves state transitions, guards, time triggers, and data assignments.
 * 5. Materializes parallel/orthogonal state regions and resolves initial leaf states.
 *
 * @param content Input SysML v2 textual model specification.
 * @param model Output canonical state machine intermediate representation.
 * @param error_message Diagnostic error message if parsing fails.
 * @return True if parsing and IR construction succeeded, false otherwise.
 */
bool Sysml2Parser::parse(std::string_view content, FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) {
    std::string err;
    if (!parse_impl(content, out_ir, err)) {
        report_parse_error(diag, "E_PARSE_SYSML2", err);
        return false;
    }
    return true;
}

bool Sysml2Parser::parse_impl(std::string_view content, FsmIr& model, std::string& error_message) {
    Sysml2SymbolResolver symbol_resolver;

    // Step 1: Lexical analysis and block token scanning
    auto tokens = Sysml2BlockScanner::scan(content);

    // Stacks to track nested hierarchical states, packages, and definition blocks
    std::vector<std::string> state_stack;
    std::vector<std::string> package_stack;
    std::vector<std::size_t> package_push_counts;
    std::vector<SysmlBlockKind> block_stack;
    std::string current_item_def;
    std::string current_enum_def;
    std::string current_struct_def;
    std::string current_calc_def;

    // Step 2: Iterate through all scanned tokens and process directives or block structures
    for (const auto& token : tokens) {
        // Directives: handles embedded @fsm metadata annotations (variables, ports, properties, signals, services,
        // external types, state flags)
        if (token.kind == SysmlTokenKind::Directive) {
            std::string body = directive::DirectiveParser::extract_directive_body(token.text);
            if (!directive::DirectiveParser::parse_model_directive(body, model) && !state_stack.empty()) {
                auto* st = model.find_state_mut(state_stack.back());
                if (st != nullptr) {
                    if (body.rfind("state", 0) == 0) {
                        directive::DirectiveParser::parse_state_directive(body, *st);
                    } else if (body.rfind("defer", 0) == 0) {
                        directive::DirectiveParser::parse_defer_directive(body, *st);
                    }
                }
            }
            continue;
        }

        // Block open: entering a new composite state, package, item def, enum, or action block
        if (token.kind == SysmlTokenKind::BlockOpen) {
            SysmlBlockKind opened_kind = SysmlBlockKind::ActionBlock;
            if (!process_statement(token.text, model, state_stack, package_stack, package_push_counts, current_item_def,
                                   current_enum_def, current_struct_def, current_calc_def, error_message,
                                   token.line_number, true, opened_kind, symbol_resolver)) {
                return false;
            }
            block_stack.push_back(opened_kind);
        } else if (token.kind == SysmlTokenKind::BlockClose) {
            // Block close: unwind block stack and pop current state/definition/package scope
            if (!block_stack.empty()) {
                const auto popped_kind = block_stack.back();
                block_stack.pop_back();
                if (popped_kind == SysmlBlockKind::State && !state_stack.empty()) {
                    state_stack.pop_back();
                } else if (popped_kind == SysmlBlockKind::Package) {
                    if (!package_push_counts.empty()) {
                        size_t cnt = package_push_counts.back();
                        package_push_counts.pop_back();
                        for (size_t k = 0; k < cnt && !package_stack.empty(); ++k) {
                            package_stack.pop_back();
                        }
                        if (model.name.empty()) {
                            std::string joined;
                            for (size_t k = 0; k < package_stack.size(); ++k) {
                                if (k > 0)
                                    joined += "::";
                                joined += package_stack[k];
                            }
                            model.package = joined;
                        }
                    }
                } else if (popped_kind == SysmlBlockKind::ItemDef) {
                    current_item_def.clear();
                } else if (popped_kind == SysmlBlockKind::EnumDef) {
                    current_enum_def.clear();
                } else if (popped_kind == SysmlBlockKind::StructDef) {
                    current_struct_def.clear();
                } else if (popped_kind == SysmlBlockKind::CalcDef) {
                    current_calc_def.clear();
                }
            }
        } else if (token.kind == SysmlTokenKind::Statement || token.kind == SysmlTokenKind::ActionBlock) {
            // Regular standalone statement (e.g., transition, entry action, attribute assignment)
            SysmlBlockKind unused_kind = SysmlBlockKind::ActionBlock;
            if (!process_statement(token.text, model, state_stack, package_stack, package_push_counts, current_item_def,
                                   current_enum_def, current_struct_def, current_calc_def, error_message,
                                   token.line_number, false, unused_kind, symbol_resolver)) {
                return false;
            }
        }
    }

    // Step 3: Validate that at least one valid state was discovered
    if (model.states.empty()) {
        error_message = "SysML v2 parser: No states found in input.";
        return false;
    }

    // Default initial state to the first registered state if none explicitly set
    if (model.initial_state.empty() && !model.states.empty()) {
        model.initial_state = model.states.front().name;
    }

    // Step 4: Materialize parallel / orthogonal regions in the IR
    // SysML expresses parallel regions as direct composite children. Materialize
    // those regions in the canonical IR before structural lowering.
    for (auto& parallel : model.states) {
        if (parallel.kind != StateKind::Parallel || !parallel.orthogonal_regions.empty()) {
            continue;
        }

        // Identify all direct child states acting as roots of orthogonal regions
        std::vector<std::string> region_roots;
        for (const auto& candidate : model.states) {
            if (candidate.parent_state == parallel.name) {
                region_roots.push_back(candidate.name);
            }
        }

        // Helper lambda to recursively discover the initial leaf state of a composite region
        auto find_initial_leaf = [&](auto&& self, const std::string& state_name) -> std::string {
            const auto* state = model.find_state(state_name);
            if (state != nullptr && state->is_composite && !state->initial_sub_state.empty()) {
                return self(self, state->initial_sub_state);
            }
            return state_name;
        };

        // Construct OrthogonalRegion objects for each region root
        for (const auto& root_name : region_roots) {
            const auto* root = model.find_state(root_name);
            if (root == nullptr) {
                continue;
            }

            OrthogonalRegion region;
            region.id = root->id.empty() ? root->name : root->id;
            region.name = root->name;
            region.initial_state_id = find_initial_leaf(
                find_initial_leaf, root->initial_sub_state.empty() ? root->name : root->initial_sub_state);

            // Collect all descendant atomic leaf states belonging to this region
            std::function<void(const std::string&)> collect_leaves = [&](const std::string& parent_name) {
                bool found_child = false;
                for (const auto& candidate : model.states) {
                    if (candidate.parent_state != parent_name) {
                        continue;
                    }
                    found_child = true;
                    if (candidate.is_composite) {
                        collect_leaves(candidate.name);
                    } else {
                        region.state_ids.push_back(candidate.name);
                    }
                }
                if (!found_child && region.state_ids.empty()) {
                    region.state_ids.push_back(parent_name);
                }
            };
            collect_leaves(root->name);
            parallel.orthogonal_regions.push_back(std::move(region));
        }
    }

    return true;
}

std::string Sysml2Parser::strip_comments(const std::string& line) {
    const auto line_comment_pos = line.find("//");
    std::string result = (line_comment_pos != std::string::npos) ? line.substr(0, line_comment_pos) : line;

    const auto block_comment_start = result.find("/*");
    if (block_comment_start != std::string::npos) {
        const auto block_comment_end = result.find("*/", block_comment_start + 2);
        if (block_comment_end != std::string::npos) {
            result.erase(block_comment_start, block_comment_end - block_comment_start + 2);
        } else {
            result.erase(block_comment_start);
        }
    }
    return result;
}

std::string Sysml2Parser::trim(const std::string& str) {
    const auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string Sysml2Parser::normalize_whitespace(const std::string& str) {
    std::string result;
    bool in_space = false;
    for (const char character : str) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (!in_space) {
                result += ' ';
                in_space = true;
            }
        } else {
            result += character;
            in_space = false;
        }
    }
    return trim(result);
}

DataType Sysml2Parser::map_sysml_to_data_type(std::string_view sysml_type, const Sysml2SymbolResolver* resolver) {
    std::string t = trim(std::string{sysml_type});
    if (resolver != nullptr) {
        t = resolver->resolve_type(t);
    }
    auto colon_pos = t.rfind("::");
    if (colon_pos != std::string::npos) {
        t = t.substr(colon_pos + 2);
    }
    return DataType::from_string(t);
}

std::string Sysml2Parser::map_sysml_type_to_cpp(std::string_view sysml_type, const Sysml2SymbolResolver* resolver) {
    std::string t = trim(std::string{sysml_type});
    if (resolver != nullptr) {
        t = resolver->resolve_type(t);
    }
    auto colon_pos = t.rfind("::");
    if (colon_pos != std::string::npos) {
        t = t.substr(colon_pos + 2);
    }
    if (t == "Integer" || t == "Natural" || t == "Positive" || t == "uint32") {
        return "uint32_t";
    }
    if (t == "int" || t == "Int32" || t == "int32") {
        return "int32_t";
    }
    if (t == "Real" || t == "Float" || t == "float") {
        return "float";
    }
    if (t == "Double" || t == "double") {
        return "double";
    }
    if (t == "Boolean" || t == "bool") {
        return "bool";
    }
    if (t == "String" || t == "string") {
        return "std::string";
    }
    return t;
}

std::string Sysml2Parser::to_pascal_case(const std::string& str) {
    std::string res;
    bool cap = true;
    for (char c : str) {
        if (c == '_' || c == '-' || c == ' ') {
            cap = true;
        } else if (cap) {
            res += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap = false;
        } else {
            res += c;
        }
    }
    return res.empty() ? "Custom" : res;
}

}  // namespace fsm::frontend::formal
