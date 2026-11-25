/**
 * @file sysml2_parser.hpp
 * @brief Formal frontend parser for OMG SysML v2 / KerML state definitions.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_block_scanner.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_symbol_resolver.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal {

using ir::DataType;

/**
 * @struct Sysml2ParseOptions
 * @brief Workspace and multi-machine selection options for OMG SysML v2.0 parsing.
 */
struct Sysml2ParseOptions {
    std::vector<std::string> import_dirs;  ///< Search directories for multi-file `import Pkg::*;` resolution
    std::string target_state_def;          ///< Optional `state def` name to compile when a model defines multiple
    std::string source_file;               ///< Optional path of the primary `.sysml` file (parent dir auto-searched)
};

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
    enum class SysmlBlockKind : std::uint8_t {
        Package,
        StateDef,
        State,
        ItemDef,
        EnumDef,
        StructDef,
        CalcDef,
        PortDef,
        ActionBlock
    };

    void set_options(Sysml2ParseOptions opts) { options_ = std::move(opts); }
    [[nodiscard]] const Sysml2ParseOptions& options() const noexcept { return options_; }

    /**
     * @brief Scans SysML v2 source text and returns all declared `state def` names in declaration order.
     */
    [[nodiscard]] static std::vector<std::string> list_state_defs(std::string_view content);

    /**
     * @brief Ingests SysML v2 text into the destination FsmIr model.
     * @param content Raw SysML v2 specification text.
     * @param model Destination FsmIr to populate.
     * @param diag Diagnostic engine for errors and warnings.
     * @return True on success, false otherwise.
     */
    bool parse(std::string_view content, FsmIr& model, diagnostic::DiagnosticEngine& diag) override;

  private:
    Sysml2ParseOptions options_;
    bool parse_impl(std::string_view content, FsmIr& out_ir, std::string& out_error);
    static std::string strip_comments(const std::string& line);
    static std::string trim(const std::string& str);
    static std::string normalize_whitespace(const std::string& str);
    static DataType map_sysml_to_data_type(std::string_view sysml_type, const Sysml2SymbolResolver* resolver = nullptr);
    static std::string map_sysml_type_to_cpp(std::string_view sysml_type,
                                             const Sysml2SymbolResolver* resolver = nullptr);
    static std::string to_pascal_case(const std::string& str);

    static bool process_statement(const std::string& raw_stmt, FsmIr& model, std::vector<std::string>& state_stack,
                                  std::vector<std::string>& package_stack,
                                  std::vector<std::size_t>& package_push_counts, std::string& current_item_def,
                                  std::string& current_enum_def, std::string& current_struct_def,
                                  std::string& current_calc_def, std::string& error_message, size_t line_number,
                                  bool is_block_open, SysmlBlockKind& out_kind, Sysml2SymbolResolver& symbol_resolver);

    static bool parse_transition_statement(const std::string& stmt, FsmIr& model,
                                           const std::vector<std::string>& state_stack,
                                           const Sysml2SymbolResolver* symbol_resolver = nullptr);
};

}  // namespace fsm::frontend::formal
