/**
 * @file sysml2_block_scanner.hpp
 * @brief Block-aware hierarchical scanner for OMG SysML v2 / KerML text.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fsm::frontend::formal {

/**
 * @enum SysmlTokenKind
 * @brief Structural token classifications for the SysML v2 block scanner.
 */
enum class SysmlTokenKind : std::uint8_t {
    Statement,    ///< Semicolon-terminated statement
    BlockOpen,    ///< '{' opening a named block
    BlockClose,   ///< '}' closing a block
    ActionBlock,  ///< Balanced action block { ... } (opaque body)
    Directive     ///< @fsm: directive comment
};

/**
 * @struct SysmlScannedToken
 * @brief Token unit produced by lexical scanning of SysML v2 content.
 */
struct SysmlScannedToken {
    SysmlTokenKind kind;     ///< Token classification
    std::string text;        ///< Token content string
    size_t line_number = 0;  ///< 1-indexed source line number
};

/**
 * @class Sysml2BlockScanner
 * @brief Block-Aware Hierarchical Scanner for OMG SysML v2 / KerML.
 *
 * Provides:
 * - Selective filtering of non-FSM structural blocks (part def, part, connect, alloc)
 * - Exact brace balancing for nested actions (entry do action { if (...) { ... } })
 * - Directives and comment preservation
 */
class Sysml2BlockScanner {
  public:
    /**
     * @brief Scans SysML v2 content into a stream of structured block tokens.
     */
    static std::vector<SysmlScannedToken> scan(std::string_view content);

    /**
     * @brief Checks if a statement describes non-FSM structural architecture.
     */
    static bool is_structural_statement(std::string_view stmt);

    /**
     * @brief Checks if a block header introduces non-FSM structural semantics.
     */
    static bool is_structural_block_head(std::string_view head);

    /**
     * @brief Checks if a block header represents a container state/package.
     */
    static bool is_container_block_head(std::string_view head);

    /**
     * @brief Checks if a statement syntax conforms to a transition definition.
     */
    static bool is_transition_like(std::string_view stmt);

  private:
    static std::string trim_str(std::string_view sv);
    static bool ends_with_word(std::string_view text, std::string_view word);
};

}  // namespace fsm::frontend::formal
