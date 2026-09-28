/**
 * @file sysml2_symbol_resolver.hpp
 * @brief Symbol and import resolver for OMG SysML v2 / KerML types and packages.
 */

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsm::frontend::formal {

/**
 * @class Sysml2SymbolResolver
 * @brief Symbol and Import Resolver for OMG SysML v2 / KerML models.
 *
 * Pre-populates standard KerML libraries (ScalarValues, SI, ISQ)
 * and resolves multi-file package imports across include search paths.
 */
class Sysml2SymbolResolver {
  public:
    Sysml2SymbolResolver();

    /**
     * @brief Registers an additional filesystem search directory for package imports.
     */
    void add_search_path(std::string path);

    /**
     * @brief Returns current include search paths.
     */
    [[nodiscard]] const std::vector<std::string>& search_paths() const noexcept;

    /**
     * @brief Registers an import statement (e.g. "import ScalarValues::*").
     */
    void register_import(std::string_view import_stmt);

    /**
     * @brief Checks if a type name corresponds to a standard KerML primitive.
     */
    [[nodiscard]] bool is_standard_type(std::string_view type_name) const noexcept;

    /**
     * @brief Resolves a qualified or unqualified SysML type name into its canonical type.
     */
    [[nodiscard]] std::string resolve_type(std::string_view raw_type) const;

  private:
    std::vector<std::string> search_paths_;
    std::unordered_set<std::string> imported_packages_;
    std::unordered_set<std::string> standard_types_;
    std::unordered_map<std::string, std::string> type_map_;

    void init_standard_library();
};

}  // namespace fsm::frontend::formal
