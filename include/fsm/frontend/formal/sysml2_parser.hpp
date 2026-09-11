/**
 * @file sysml2_parser.hpp
 * @brief Formal frontend parser for OMG SysML v2 / KerML state definitions.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/formal/sysml2_block_scanner.hpp"
#include "fsm/frontend/formal/sysml2_symbol_resolver.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

using ir::DataType;

/**
 * @class Sysml2Parser
 * @brief Parser frontend for OMG Systems Modeling Language (SysML) v2 textual notation.
 */
class Sysml2Parser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "sysml2"; }

    /**
     * @enum SysmlBlockKind
     * @brief Lexical scope categories for SysML v2 state and structural definitions.
     */
    enum class SysmlBlockKind : std::uint8_t { Package, StateDef, State, ItemDef, EnumDef, StructDef, ActionBlock };

    /**
     * @brief Ingests SysML v2 text into the destination FsmIr model.
     * @param content Raw SysML v2 specification text.
     * @param model Destination FsmIr to populate.
     * @param error_message Diagnostic description if parsing fails.
     * @return True on success, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override;

  private:
    static std::string strip_comments(const std::string& line);
    static std::string trim(const std::string& str);
    static std::string normalize_whitespace(const std::string& str);
    static DataType map_sysml_to_data_type(std::string_view sysml_type, const Sysml2SymbolResolver* resolver = nullptr);
    static std::string map_sysml_type_to_cpp(std::string_view sysml_type,
                                             const Sysml2SymbolResolver* resolver = nullptr);
    static std::string to_pascal_case(const std::string& str);

    static bool process_statement(const std::string& raw_stmt, FsmIr& model, std::vector<std::string>& state_stack,
                                  std::string& current_item_def, std::string& current_enum_def,
                                  std::string& current_struct_def, std::string& error_message, size_t line_number,
                                  bool is_block_open, SysmlBlockKind& out_kind, Sysml2SymbolResolver& symbol_resolver);

    static bool parse_transition_statement(const std::string& stmt, FsmIr& model,
                                           const std::vector<std::string>& state_stack);
};

}  // namespace fsm::frontend::formal
