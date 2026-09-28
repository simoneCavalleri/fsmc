#include "fsm/frontend/formal/sysml2_parser.hpp"

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
bool Sysml2Parser::parse(std::string_view content, FsmIr& model, std::string& error_message) {
    Sysml2SymbolResolver symbol_resolver;

    // Step 1: Lexical analysis and block token scanning
    auto tokens = Sysml2BlockScanner::scan(content);

    // Stacks to track nested hierarchical states and definition blocks
    std::vector<std::string> state_stack;
    std::vector<SysmlBlockKind> block_stack;
    std::string current_item_def;
    std::string current_enum_def;
    std::string current_struct_def;

    // Step 2: Iterate through all scanned tokens and process directives or block structures
    for (const auto& token : tokens) {
        // Directives: handles embedded @fsm metadata annotations (variables, properties, signals, state flags)
        if (token.kind == SysmlTokenKind::Directive) {
            std::string body = directive::DirectiveParser::extract_directive_body(token.text);
            if (body.rfind("var", 0) == 0) {
                if (auto var = directive::DirectiveParser::parse_variable_directive(body)) {
                    model.add_variable(std::move(*var));
                }
            } else if (body.rfind("property", 0) == 0) {
                if (auto prop = directive::DirectiveParser::parse_property_directive(body)) {
                    model.add_property(std::move(*prop));
                }
            } else if (body.rfind("signal", 0) == 0) {
                if (auto sig = directive::DirectiveParser::parse_signal_directive(body)) {
                    model.add_signal(std::move(*sig));
                }
            } else if (!state_stack.empty()) {
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
            if (!process_statement(token.text, model, state_stack, current_item_def, current_enum_def,
                                   current_struct_def, error_message, token.line_number, true, opened_kind,
                                   symbol_resolver)) {
                return false;
            }
            block_stack.push_back(opened_kind);
        } else if (token.kind == SysmlTokenKind::BlockClose) {
            // Block close: unwind block stack and pop current state/definition scope
            if (!block_stack.empty()) {
                const auto popped_kind = block_stack.back();
                block_stack.pop_back();
                if (popped_kind == SysmlBlockKind::State && !state_stack.empty()) {
                    state_stack.pop_back();
                } else if (popped_kind == SysmlBlockKind::ItemDef) {
                    current_item_def.clear();
                } else if (popped_kind == SysmlBlockKind::EnumDef) {
                    current_enum_def.clear();
                } else if (popped_kind == SysmlBlockKind::StructDef) {
                    current_struct_def.clear();
                }
            }
        } else if (token.kind == SysmlTokenKind::Statement || token.kind == SysmlTokenKind::ActionBlock) {
            // Regular standalone statement (e.g., transition, entry action, attribute assignment)
            SysmlBlockKind unused_kind = SysmlBlockKind::ActionBlock;
            if (!process_statement(token.text, model, state_stack, current_item_def, current_enum_def,
                                   current_struct_def, error_message, token.line_number, false, unused_kind,
                                   symbol_resolver)) {
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
    if (t == "Integer" || t == "Natural" || t == "Positive" || t == "int" || t == "uint32") {
        return "uint32_t";
    }
    if (t == "Int32" || t == "int32") {
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

bool Sysml2Parser::process_statement(const std::string& raw_stmt, FsmIr& model, std::vector<std::string>& state_stack,
                                     std::string& current_item_def, std::string& current_enum_def,
                                     std::string& current_struct_def, std::string& error_message, size_t line_number,
                                     bool is_block_open, SysmlBlockKind& out_kind,
                                     Sysml2SymbolResolver& symbol_resolver) {
    (void)error_message;
    (void)line_number;
    const std::string stmt = normalize_whitespace(raw_stmt);
    out_kind = SysmlBlockKind::ActionBlock;
    if (stmt.empty()) {
        return true;
    }

    // Structural statement filtering (connect, bind, alloc, part usage)
    if (Sysml2BlockScanner::is_structural_statement(stmt)) {
        return true;
    }

    // Import handling (e.g. import ScalarValues::*, import MyPkg::*)
    if (stmt.rfind("import ", 0) == 0) {
        symbol_resolver.register_import(stmt);
        return true;
    }

    // 1. package <Name> / state def <Name>
    static const std::regex package_def_regex(R"(^package\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    std::smatch match;
    if (std::regex_search(stmt, match, package_def_regex)) {
        model.package = sanitize_identifier(match[1].str());

        if (is_block_open) {
            out_kind = SysmlBlockKind::Package;
        }
        return true;
    }

    static const std::regex state_def_regex(R"(^state\s+def\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, state_def_regex)) {
        model.name = sanitize_identifier(match[1].str());
        if (is_block_open) {
            out_kind = SysmlBlockKind::StateDef;
        }
        return true;
    }

    // 2a. Enum Definition: enum def <Name> [ :> <Underlying> ]
    static const std::regex enum_def_regex(R"(^enum\s+def\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*:>\s*([A-Za-z0-9_:]+))?)",
                                           std::regex::optimize);
    if (std::regex_search(stmt, match, enum_def_regex)) {
        const std::string enum_name = sanitize_identifier(match[1].str());
        const std::string underlying =
            match[2].matched ? map_sysml_type_to_cpp(match[2].str(), &symbol_resolver) : "uint8_t";
        EnumDefinition enum_def(enum_name, underlying);
        model.add_enum(std::move(enum_def));
        if (is_block_open) {
            current_enum_def = enum_name;
            out_kind = SysmlBlockKind::EnumDef;
        }
        return true;
    }

    // 2a-2. Enum Literal: enum <LitName> [ = <val> ]
    static const std::regex enum_lit_regex(R"(^enum\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*(-?\d+))?)",
                                           std::regex::optimize);
    if (!current_enum_def.empty() && std::regex_search(stmt, match, enum_lit_regex)) {
        const std::string lit_name = sanitize_identifier(match[1].str());
        std::optional<int64_t> val = std::nullopt;
        if (match[2].matched) {
            try {
                val = std::stoll(match[2].str());
            } catch (...) {
            }
        }
        if (auto* en = model.find_enum_mut(current_enum_def)) {
            en->add_literal(EnumLiteral(lit_name, val));
        }
        return true;
    }

    // 2a-3. Struct / Datatype Definition: (struct def|datatype def) <Name>
    static const std::regex struct_def_regex(R"(^(struct\s+def|datatype\s+def)\s+([A-Za-z_][A-Za-z0-9_]*))",
                                             std::regex::optimize);
    if (std::regex_search(stmt, match, struct_def_regex)) {
        bool is_dt = (match[1].str().find("datatype") != std::string::npos);
        const std::string struct_name = sanitize_identifier(match[2].str());
        StructDefinition struct_def(struct_name, is_dt);
        model.add_struct(std::move(struct_def));
        if (is_block_open) {
            current_struct_def = struct_name;
            out_kind = SysmlBlockKind::StructDef;
        }
        return true;
    }

    // 2b. Port declaration: (in|out|inout) port <name> : <Type> [ { assert constraint { <expr> } } ]
    static const std::regex port_regex(R"(^(in|out|inout)\s+port\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+))",
                                       std::regex::optimize);
    if (std::regex_search(stmt, match, port_regex)) {
        std::string dir_str = match[1].str();
        std::string port_name = sanitize_identifier(match[2].str());
        std::string raw_type = match[3].str();
        std::string cpp_type = map_sysml_type_to_cpp(raw_type, &symbol_resolver);
        std::string constraint_str;
        auto assert_pos = stmt.find("assert");
        if (assert_pos != std::string::npos) {
            auto c_start = stmt.find('{', assert_pos);
            auto c_end = stmt.rfind('}');
            if (c_start != std::string::npos && c_end != std::string::npos && c_end > c_start) {
                constraint_str = trim(stmt.substr(c_start + 1, c_end - c_start - 1));
                while (!constraint_str.empty() && constraint_str.front() == '{')
                    constraint_str = trim(constraint_str.substr(1));
                while (!constraint_str.empty() && constraint_str.back() == '}')
                    constraint_str = trim(constraint_str.substr(0, constraint_str.size() - 1));
            }
        }

        PortDirection dir = string_to_port_direction(dir_str);
        std::optional<double> min_val = std::nullopt;
        std::optional<double> max_val = std::nullopt;

        if (!constraint_str.empty()) {
            static const std::regex min_re(R"(self\s*>=\s*([0-9]+(?:\.[0-9]+)?))");
            static const std::regex max_re(R"(self\s*<=\s*([0-9]+(?:\.[0-9]+)?))");
            std::smatch m_min, m_max;
            if (std::regex_search(constraint_str, m_min, min_re)) {
                try {
                    min_val = std::stod(m_min[1].str());
                } catch (...) {
                }
            }
            if (std::regex_search(constraint_str, m_max, max_re)) {
                try {
                    max_val = std::stod(m_max[1].str());
                } catch (...) {
                }
            }
        }

        PortDefinition port_def(port_name, map_sysml_to_data_type(raw_type, &symbol_resolver), dir, min_val, max_val,
                                constraint_str);
        model.add_port(std::move(port_def));
        return true;
    }

    // 2c. Action definition: action def <Name>; or action def <Name>
    static const std::regex action_def_regex(R"(^action\s+def\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, action_def_regex)) {
        const std::string act_name = sanitize_identifier(match[1].str());
        model.add_action(act_name);
        return true;
    }

    // 2. Item Definition (Signal with Payload): item def / event def / attribute def <Name>
    static const std::regex item_def_regex(
        R"(^(?:item\s+def|event\s+def|attribute\s+def|port\s+def)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, item_def_regex)) {
        const std::string sig_name = sanitize_identifier(match[1].str());
        SignalDefinition sig;
        sig.name = sig_name;
        model.add_signal(std::move(sig));
        if (is_block_open) {
            current_item_def = sig_name;
            out_kind = SysmlBlockKind::ItemDef;
        }
        return true;
    }

    // 3. Attribute / Variable declaration: attribute <name> : <Type> [[unit]] [= <init>] / var <name> : <Type>
    static const std::regex attr_regex(
        R"(^(?:attribute|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+)(?:\s*(\[[^\]]+\]))?(?:\s*=\s*([^;]+))?)",
        std::regex::optimize);
    if (std::regex_search(stmt, match, attr_regex)) {
        const std::string attr_name = sanitize_identifier(match[1].str());
        const std::string raw_type = match[2].str();
        const DataType canonical_dt = map_sysml_to_data_type(raw_type, &symbol_resolver);
        const std::string unit_str = match[3].matched ? trim(match[3].str()) : "";
        const std::string init_val = match[4].matched ? trim(match[4].str()) : "";

        if (!current_struct_def.empty()) {
            // Member field inside a Struct / Datatype Definition
            if (auto* st = model.find_struct_mut(current_struct_def)) {
                std::string clean_unit = unit_str;
                if (clean_unit.size() >= 2 && clean_unit.front() == '[' && clean_unit.back() == ']') {
                    clean_unit = clean_unit.substr(1, clean_unit.size() - 2);
                }
                std::optional<std::string> unit_opt =
                    clean_unit.empty() ? std::nullopt : std::make_optional(clean_unit);
                StructField sf(attr_name, canonical_dt, init_val, unit_opt);
                st->add_field(std::move(sf));
            }
        } else if (!current_item_def.empty()) {
            // Member attribute inside a Signal / Item Definition
            for (auto& sig : model.signals) {
                if (sig.name == current_item_def) {
                    SignalAttribute sa(attr_name, canonical_dt, init_val);
                    sig.attributes.push_back(std::move(sa));
                    break;
                }
            }
        } else {
            // Top-level EFSM state machine variable
            VariableDefinition var;
            var.name = attr_name;
            var.type = canonical_dt;
            std::string clean_unit = unit_str;
            if (clean_unit.size() >= 2 && clean_unit.front() == '[' && clean_unit.back() == ']') {
                clean_unit = clean_unit.substr(1, clean_unit.size() - 2);
            }
            if (!clean_unit.empty()) {
                var.physical_unit = clean_unit;
            }
            var.initial_value = init_val;
            model.add_variable(std::move(var));
        }
        return true;
    }

    // 4. Choice node declaration: state <ChoiceName> <<choice>> or state <ChoiceName> :> Choice
    static const std::regex choice_regex(
        R"(^(?:parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:<<choice>>|<<junction>>|:>\s*Choice|:>\s*Junction))",
        std::regex::optimize);
    if (std::regex_search(stmt, match, choice_regex)) {
        const std::string choice_name = sanitize_identifier(match[1].str());
        model.add_choice_node(choice_name);
        return true;
    }

    // 4c. Decide / Decision pseudostate node: decide <ChoiceName>;
    static const std::regex decide_regex(R"(^decide\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, decide_regex)) {
        const std::string choice_name = sanitize_identifier(match[1].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        model.add_or_get_state(choice_name, parent_name, StateKind::Choice);
        model.add_choice_node(choice_name);
        return true;
    }

    // 4b. Submachine declaration: state <SubName> :> <SubmachineName>
    static const std::regex submachine_regex(
        R"(^(?:parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*)\s*:>\s*([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, submachine_regex)) {
        const std::string state_name = sanitize_identifier(match[1].str());
        const std::string sub_name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        auto& st = model.add_state(state_name, parent_name);
        st.submachine = SubmachineRef(sub_name);
        return true;
    }

    // 5. state <Name> (composite or leaf, optional parallel)
    static const std::regex state_decl_regex(R"(^(parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*))",
                                             std::regex::optimize);
    if (std::regex_search(stmt, match, state_decl_regex)) {
        bool is_parallel = match[1].matched;
        const std::string state_name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        auto* existing = model.find_state_mut(state_name);
        if (existing != nullptr) {
            existing->parent_state = parent_name;
            if (is_parallel) {
                existing->kind = StateKind::Parallel;
            }
        } else {
            auto& st = model.add_state(state_name, parent_name);
            if (is_parallel) {
                st.kind = StateKind::Parallel;
            }
        }

        if (!state_stack.empty()) {
            auto* parent = model.find_state_mut(state_stack.back());
            if (parent != nullptr) {
                parent->is_composite = true;
                if (parent->initial_sub_state.empty()) {
                    parent->initial_sub_state = state_name;
                }
            }
        }
        if (is_block_open) {
            state_stack.push_back(state_name);
            out_kind = SysmlBlockKind::State;
        }
        return true;
    }

    // 6. entry; then <State>; or initial; then <State>; or standalone then <State>;
    static const std::regex initial_regex(R"(^(?:(?:entry|initial)(?:\s*;)?\s*)?then\s+([A-Za-z_][A-Za-z0-9_]*)$)",
                                          std::regex::optimize);
    if (std::regex_search(stmt, match, initial_regex)) {
        const std::string init_target = sanitize_identifier(match[1].str());
        if (state_stack.empty()) {
            model.initial_state = init_target;
        } else {
            auto* parent = model.find_state_mut(state_stack.back());
            if (parent != nullptr) {
                parent->initial_sub_state = init_target;
            }
        }
        return true;
    }

    if (stmt == "entry" || stmt == "initial") {
        return true;
    }

    // 7a. Entry Point / Exit Point pseudostates: entry point <Name>; / exit point <Name>;
    static const std::regex entry_exit_point_regex(R"(^(entry\s+point|exit\s+point)\s+([A-Za-z_][A-Za-z0-9_]*))",
                                                   std::regex::optimize);
    if (std::regex_search(stmt, match, entry_exit_point_regex)) {
        bool is_entry = match[1].str().find("entry") != std::string::npos;
        const std::string name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        model.add_or_get_state(name, parent_name, is_entry ? StateKind::EntryPoint : StateKind::ExitPoint);
        return true;
    }

    // 7a-2. Fork and Join pseudostates: fork <Name>; / join <Name>;
    static const std::regex fork_join_regex(R"(^(fork|join)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, fork_join_regex)) {
        bool is_fork = (match[1].str() == "fork");
        const std::string name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        model.add_or_get_state(name, parent_name, is_fork ? StateKind::Fork : StateKind::Join);
        return true;
    }

    // 7b. State Lifecycle Actions: entry action <Act>; / exit action <Act>;
    static const std::regex entry_block_regex(R"(^entry(?:\s+do)?(?:\s+action)?\s*\{([\s\S]*)\})",
                                              std::regex::optimize);
    if (std::regex_search(stmt, match, entry_block_regex)) {
        std::string act_name = !state_stack.empty() ? (state_stack.back() + "_entry") : "entry_action";
        model.add_action(act_name);
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->entry_actions.emplace_back(act_name);
            }
        }
        return true;
    }

    static const std::regex exit_block_regex(R"(^exit(?:\s+do)?(?:\s+action)?\s*\{([\s\S]*)\})", std::regex::optimize);
    if (std::regex_search(stmt, match, exit_block_regex)) {
        std::string act_name = !state_stack.empty() ? (state_stack.back() + "_exit") : "exit_action";
        model.add_action(act_name);
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->exit_actions.emplace_back(act_name);
            }
        }
        return true;
    }

    static const std::regex do_block_act_regex(R"(^do(?:\s+action)?\s*\{([\s\S]*)\})", std::regex::optimize);
    if (std::regex_search(stmt, match, do_block_act_regex)) {
        std::string act_name = !state_stack.empty() ? (state_stack.back() + "_do") : "do_activity";
        model.add_action(act_name);
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->do_activity = act_name;
            }
        }
        return true;
    }

    static const std::regex entry_act_regex(
        R"(^entry\s+(?:action\s+|do\s+)?(?!point\b)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?)", std::regex::optimize);
    if (std::regex_search(stmt, match, entry_act_regex)) {
        const std::string act_name = sanitize_identifier(match[1].str());
        model.add_action(act_name);
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->entry_actions.emplace_back(act_name);
            }
        }
        return true;
    }

    static const std::regex exit_act_regex(
        R"(^exit\s+(?:action\s+|do\s+)?(?!point\b)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?)", std::regex::optimize);
    if (std::regex_search(stmt, match, exit_act_regex)) {
        const std::string act_name = sanitize_identifier(match[1].str());
        model.add_action(act_name);
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->exit_actions.emplace_back(act_name);
            }
        }
        return true;
    }

    // 8. State Do Activity: do action <Activity>; or do <Activity>;
    static const std::regex do_act_regex(R"(^do\s+(?:action\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?$)",
                                         std::regex::optimize);
    if (std::regex_search(stmt, match, do_act_regex)) {
        const std::string act_name = sanitize_identifier(match[1].str());
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->do_activity = act_name;
            }
        }
        return true;
    }

    // 8d. State Send Action: [do] send <Signal>[(<Payload>)] via <Port>;
    static const std::regex send_act_regex(
        R"(^(?:do\s+)?send\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(([^)]*)\))?\s+via\s+([A-Za-z_][A-Za-z0-9_]*))",
        std::regex::optimize);
    if (std::regex_search(stmt, match, send_act_regex)) {
        const std::string sig_name = sanitize_identifier(match[1].str());
        const std::string args_str = match[2].matched ? match[2].str() : "";
        const std::string port_name = sanitize_identifier(match[3].str());

        if (model.find_port(port_name) == nullptr) {
            PortDefinition p(port_name, DataType{PrimitiveTypeKind::UInt32}, PortDirection::Out);
            model.ports.push_back(std::move(p));
        }
        if (model.find_signal(sig_name) == nullptr) {
            model.signals.emplace_back(sig_name);
        }

        std::string act_name = "send_" + sig_name + "_via_" + port_name;
        ActionSignature sig_action(act_name,
                                   port_name + ".send(" + sig_name + (args_str.empty() ? "" : (", " + args_str)) + ")");
        SignalEmitOp emit_op;
        emit_op.signal_name = sig_name;
        emit_op.target_port = port_name;
        if (!args_str.empty())
            emit_op.arguments.push_back(args_str);
        sig_action.instructions.emplace_back(std::move(emit_op));

        model.add_action(act_name);
        if (!state_stack.empty()) {
            if (auto* st = model.find_state_mut(state_stack.back())) {
                st->entry_actions.push_back(std::move(sig_action));
            }
        }
        return true;
    }

    // 8c. State Stay Duration / Invariant: stay duration <= 500[ms]; or invariant stay <= 500ms;
    static const std::regex invariant_regex(R"(^(?:stay(?:\s+duration)?\s*<=?|invariant)\s*(.+)$)",
                                            std::regex::optimize);
    if (std::regex_search(stmt, match, invariant_regex)) {
        if (!state_stack.empty()) {
            if (auto* st = model.find_state_mut(state_stack.back())) {
                st->time_invariant = trim(match[1].str());
            }
        }
        return true;
    }

    // 8b. Deferred Events: defer <EventName>;
    static const std::regex defer_regex(R"(^defer\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, defer_regex)) {
        const std::string defer_ev = sanitize_identifier(match[1].str());
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->deferred_events.push_back(defer_ev);
            }
        }
        model.add_event(defer_ev);
        return true;
    }

    // 9. Requirement Satisfaction: satisfy [requirement] <ReqId>;
    static const std::regex satisfy_regex(R"(^satisfy\s+(?:requirement\s+)?([A-Za-z0-9_\-]+))", std::regex::optimize);
    if (std::regex_search(stmt, match, satisfy_regex)) {
        const std::string req_id = match[1].str();
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->traceability_reqs.push_back(req_id);
            }
        }
        return true;
    }

    // 10. Temporal Logic Specifications: assert property <Name> : <Formula>
    static const std::regex assert_prop_regex(R"(^(?:assert\s+)?property\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.+)$)",
                                              std::regex::optimize);
    if (std::regex_search(stmt, match, assert_prop_regex)) {
        FormalProperty prop;
        prop.name = sanitize_identifier(match[1].str());
        prop.raw_formula = trim(match[2].str());
        prop.ast = directive::LtlPropertyParser::parse(prop.raw_formula);
        prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
        model.add_property(std::move(prop));
        return true;
    }

    // 11. Transitions: transition [<Name>] [first <Src>] [accept <Evt>] [if <Guard>] [do <Act>] [then <Dst>]
    if (stmt.rfind("transition", 0) == 0 || stmt.find("accept") != std::string::npos ||
        stmt.find("then") != std::string::npos || stmt.find("first") != std::string::npos ||
        stmt.find("after") != std::string::npos) {
        return parse_transition_statement(stmt, model, state_stack);
    }

    return true;
}

/**
 * @brief Parses SysML v2 transition statements into TransitionEdge objects in FsmIr.
 *
 * Supports comprehensive SysML v2 state transition syntax variations:
 * - Explicit transition naming: `transition <Name>`
 * - Priority specifications: `priority = <N>`
 * - Source states: `first <Source>` or `from <Source>` (or inferred from state_stack)
 * - Event triggers: `accept <Event>` or `when <Event>`
 * - Timed triggers: `after <Duration>` (with SI units: ms, s, min, h) or `at (<TimeExpr>)`
 * - Guard conditions: `if <GuardExpr>` (composite boolean expressions or custom guards)
 * - Action effects: `do { <Assignments> }`, `do send <Signal> via <Port>`, or `do <Action>`
 * - Target states: `then <Target>` or `to <Target>`
 * - History pseudostates: `then State[H]` (shallow) or `then State[H*]` (deep)
 * - Internal transitions: transitions without target or with identical source and target
 *
 * @param stmt Textual transition statement.
 * @param model Output intermediate representation receiving the transition.
 * @param state_stack Current enclosing hierarchical state stack.
 * @return True if parsing succeeded.
 */
bool Sysml2Parser::parse_transition_statement(const std::string& stmt, FsmIr& model,
                                              const std::vector<std::string>& state_stack) {
    std::string source;
    std::string target;
    std::string event;
    std::string guard;
    std::string action;
    std::uint32_t priority = 0;
    std::optional<TimeTrigger> time_trigger;

    // 1. Optional transition name matching
    static const std::regex trans_name_regex(R"(^transition\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    std::string trans_name;
    std::smatch match;
    if (std::regex_search(stmt, match, trans_name_regex)) {
        std::string name_candidate = sanitize_identifier(match[1].str());
        if (name_candidate != "from" && name_candidate != "first" && name_candidate != "accept" &&
            name_candidate != "if" && name_candidate != "do" && name_candidate != "then") {
            trans_name = name_candidate;
        }
    }

    // 2. Transition priority extraction (e.g., 'priority = 10' or 'prio 1')
    static const std::regex prio_regex(R"(\b(?:priority|prio)\s*=?\s*(\d+))", std::regex::optimize);
    std::smatch prio_match;
    if (std::regex_search(stmt, prio_match, prio_regex)) {
        try {
            priority = static_cast<std::uint32_t>(std::stoul(prio_match[1].str()));
        } catch (...) {
        }
    }

    // Regular expressions for individual transition components
    static const std::regex first_regex(R"(\b(?:first|from)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    static const std::regex after_regex(
        R"(\b(?:accept\s+)?after\s*(?:\(\s*(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?\s*\)|(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?))",
        std::regex::optimize);
    static const std::regex at_regex(R"(\b(?:accept\s+)?at\s*(?:\(\s*([^)]+)\s*\)|([A-Za-z0-9_:]+)))",
                                     std::regex::optimize);
    static const std::regex accept_regex(
        R"(\b(?:accept|when)\s+(?:([A-Za-z_][A-Za-z0-9_]*)\s*:\s*)?([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    static const std::regex if_regex(R"(\bif\s+([^;]+?)(?=\s+(?:do|then|to|;|$)))", std::regex::optimize);
    static const std::regex do_block_regex(R"(\bdo\s*(?:action\s*)?\{([^}]+)\})", std::regex::optimize);
    static const std::regex do_regex(R"(\bdo\s+(?:action\s+)?([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    static const std::regex then_regex(R"(\b(?:then|to)\s+([A-Za-z_][A-Za-z0-9_\[\]\*]*))", std::regex::optimize);

    // 3. Source state detection (explicit 'first/from <State>' or inherited from parent scope)
    if (std::regex_search(stmt, match, first_regex)) {
        source = sanitize_identifier(match[1].str());
    } else if (!state_stack.empty()) {
        source = state_stack.back();
    }

    // 4. Trigger resolution: Time triggers (after, at) or Event triggers (accept, when)
    if (std::regex_search(stmt, match, after_regex)) {
        std::string val_str = match[1].matched ? match[1].str() : match[4].str();
        std::string unit_str = match[2].matched   ? match[2].str()
                               : match[3].matched ? match[3].str()
                               : match[5].matched ? match[5].str()
                               : match[6].matched ? match[6].str()
                                                  : "ms";
        double raw_val = 0.0;
        try {
            raw_val = std::stod(val_str);
        } catch (const std::exception&) {
            raw_val = 1.0;
        }
        uint64_t duration_ms = static_cast<uint64_t>(raw_val);
        if (unit_str == "s" || unit_str == "sec" || unit_str == "seconds") {
            duration_ms = static_cast<uint64_t>(raw_val * 1000.0);
        } else if (unit_str == "min") {
            duration_ms = static_cast<uint64_t>(raw_val * 60000.0);
        } else if (unit_str == "h") {
            duration_ms = static_cast<uint64_t>(raw_val * 3600000.0);
        }
        if (duration_ms == 0)
            duration_ms = 1;
        time_trigger = TimeTrigger(TimeTriggerKind::After, duration_ms, TimeUnit::Milliseconds);
        event = "after_" + std::to_string(duration_ms) + "ms";
    } else if (std::regex_search(stmt, match, at_regex)) {
        std::string at_target = match[1].matched ? match[1].str() : match[2].str();
        time_trigger = TimeTrigger(TimeTriggerKind::At, 0, TimeUnit::Milliseconds);
        time_trigger->dynamic_expression = trim(at_target);
        event = "at_" + sanitize_identifier(at_target);
    } else if (std::regex_search(stmt, match, accept_regex)) {
        event = sanitize_identifier(match[2].str());
    }

    // 5. Guard expression resolution (composite boolean expressions or named guard contracts)
    if (std::regex_search(stmt, match, if_regex)) {
        const std::string raw_guard_expr = trim(match[1].str());
        auto parsed = directive::GuardExpressionParser::parse(raw_guard_expr);
        if (!parsed.cpp_type.empty()) {
            guard = parsed.cpp_type;
            for (const auto& atomic : parsed.atomic_guards) {
                model.add_guard(atomic);
            }
        } else if (!trans_name.empty()) {
            guard = to_pascal_case(trans_name) + "Guard";
            model.add_guard(guard, "", raw_guard_expr);
        } else {
            guard = "TransitionGuard_" + std::to_string(model.transitions.size() + 1);
            model.add_guard(guard, "", raw_guard_expr);
        }
    }

    // 6. Action effects and variable assignments: do { ... }, do send via port, or do action_name
    std::vector<ActionAssignment> assignments;
    std::optional<ActionSignature> trans_send_action;
    static const std::regex trans_send_regex(
        R"(\b(?:do\s+)?send\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(([^)]*)\))?\s+via\s+([A-Za-z_][A-Za-z0-9_]*))",
        std::regex::optimize);
    if (std::regex_search(stmt, match, do_block_regex)) {
        // Parse semicolon-separated action statements within the block
        std::string block_content = trim(match[1].str());
        std::stringstream ss(block_content);
        std::string statement;
        while (std::getline(ss, statement, ';')) {
            statement = trim(statement);
            if (statement.empty())
                continue;

            static const std::regex assign_regex(
                R"(^(?:(?:out|in|reg|service|context)\.)?([A-Za-z_][A-Za-z0-9_]*)\s*(=|\+=|-=|\*=|\/=)\s*(.+)$)",
                std::regex::optimize);
            static const std::regex inc_regex(
                R"(^(?:(?:out|in|reg|service|context)\.)?([A-Za-z_][A-Za-z0-9_]*)\s*(\+\+|--)$)", std::regex::optimize);
            std::smatch assign_match;
            if (std::regex_match(statement, assign_match, assign_regex)) {
                // Variable assignment or compound assignment op
                std::string var = assign_match[1].str();
                std::string op = assign_match[2].str();
                std::string rhs = trim(assign_match[3].str());
                if (op == "=") {
                    assignments.push_back({var, rhs});
                } else if (op == "+=") {
                    assignments.push_back({var, var + " + " + rhs});
                } else if (op == "-=") {
                    assignments.push_back({var, var + " - " + rhs});
                } else if (op == "*=") {
                    assignments.push_back({var, var + " * (" + rhs + ")"});
                } else if (op == "/=") {
                    assignments.push_back({var, var + " / (" + rhs + ")"});
                }
            } else if (std::regex_match(statement, assign_match, inc_regex)) {
                // Increment or decrement shorthand (++ or --)
                std::string var = assign_match[1].str();
                std::string op = assign_match[2].str();
                if (op == "++") {
                    assignments.push_back({var, var + " + 1"});
                } else {
                    assignments.push_back({var, var + " - 1"});
                }
            } else {
                // Standalone action method call
                std::string act_call = sanitize_identifier(statement);
                if (!act_call.empty()) {
                    action = act_call;
                    model.add_action(act_call);
                }
            }
        }

        // Materialize synthesized action signatures from variable assignments
        if (!assignments.empty()) {
            if (assignments.size() == 1) {
                const auto& a = assignments[0];
                if (a.expression == a.target.name + " + 1" || a.expression == a.target.name + " + 1.0") {
                    action = "increment_" + a.target.name;
                } else if (a.expression == a.target.name + " - 1" || a.expression == a.target.name + " - 1.0") {
                    action = "decrement_" + a.target.name;
                } else if (!trans_name.empty()) {
                    action = to_pascal_case(trans_name) + "Action_" + a.target.name;
                } else {
                    action = "assign_" + a.target.name;
                }
            } else if (!trans_name.empty()) {
                action = to_pascal_case(trans_name) + "Action";
            } else {
                action = "update_state_vars";
            }
            model.add_action(action);
        }
    } else if (std::regex_search(stmt, match, trans_send_regex)) {
        // Asynchronous message/signal sending via port definition
        const std::string sig_name = sanitize_identifier(match[1].str());
        const std::string args_str = match[2].matched ? match[2].str() : "";
        const std::string port_name = sanitize_identifier(match[3].str());

        if (model.find_port(port_name) == nullptr) {
            PortDefinition p(port_name, DataType{PrimitiveTypeKind::UInt32}, PortDirection::Out);
            model.ports.push_back(std::move(p));
        }
        if (model.find_signal(sig_name) == nullptr) {
            model.signals.emplace_back(sig_name);
        }

        action = "send_" + sig_name + "_via_" + port_name;
        ActionSignature sig_action(action,
                                   port_name + ".send(" + sig_name + (args_str.empty() ? "" : (", " + args_str)) + ")");
        SignalEmitOp emit_op;
        emit_op.signal_name = sig_name;
        emit_op.target_port = port_name;
        if (!args_str.empty())
            emit_op.arguments.push_back(args_str);
        sig_action.instructions.emplace_back(std::move(emit_op));
        trans_send_action = std::move(sig_action);
    } else if (std::regex_search(stmt, match, do_regex)) {
        // Direct action invocation
        action = sanitize_identifier(match[1].str());
    }

    // 7. Target state and History pseudostate detection ([H] or [H*])
    bool target_is_history = false;
    bool target_is_deep_history = false;

    if (std::regex_search(stmt, match, then_regex)) {
        const std::string raw_target = match[1].str();
        if (raw_target.find("[H*]") != std::string::npos || raw_target.find("[deep_history]") != std::string::npos) {
            target_is_deep_history = true;
            target_is_history = true;
            target = sanitize_identifier(raw_target.substr(0, raw_target.find('[')));
        } else if (raw_target.find("[H]") != std::string::npos || raw_target.find("[history]") != std::string::npos) {
            target_is_history = true;
            target = sanitize_identifier(raw_target.substr(0, raw_target.find('[')));
        } else {
            target = sanitize_identifier(raw_target);
        }
    }

    // Normalize implicit self-transitions and source/target defaults
    if (source.empty() && !target.empty()) {
        source = target;
    }
    if (target.empty() && !source.empty()) {
        target = source;  // internal transition
    }

    if (source.empty() || target.empty()) {
        return true;
    }

    // 8. Construct the canonical TransitionEdge representation
    TransitionEdge trans;
    trans.source = source;
    trans.target = target;
    trans.event = event;
    if (time_trigger.has_value()) {
        trans.trigger = *time_trigger;
    }
    if (!guard.empty()) {
        trans.guard = guard;
    }
    if (trans_send_action.has_value()) {
        trans.transition_action = std::move(trans_send_action);
        model.add_action(action);
    } else if (!action.empty() || !assignments.empty()) {
        ActionSignature sig;
        sig.name = action;
        sig.assignments = assignments;
        trans.transition_action = std::move(sig);
        model.add_action(action);
    }
    trans.target_is_history = target_is_history;
    trans.target_is_deep_history = target_is_deep_history;
    trans.priority = priority;
    trans.parent_scope = state_stack.empty() ? "" : state_stack.back();

    // Distinguish internal from external transitions
    if (source == target && !event.empty() && stmt.find("then") == std::string::npos &&
        stmt.find("to") == std::string::npos) {
        trans.kind = TransitionEdgeKind::Internal;
    } else {
        trans.kind = TransitionEdgeKind::External;
    }

    // 9. Ensure states, choice nodes, and signals are synchronized in the model
    if (!model.is_choice_node(source)) {
        model.add_or_get_state(source, "");
    }
    if (!model.is_choice_node(target)) {
        model.add_or_get_state(target, "");
        if (target_is_history) {
            auto* target_state = model.find_state_mut(target);
            if (target_state != nullptr) {
                target_state->has_history = true;
                if (target_is_deep_history) {
                    target_state->has_deep_history = true;
                }
            }
        }
    }

    if (!event.empty()) {
        model.add_event(event);
    }
    if (!action.empty()) {
        model.add_action(action);
    }

    // 10. Register completed transition edge into the model
    model.add_transition(std::move(trans));
    return true;
}

}  // namespace fsm::frontend::formal
