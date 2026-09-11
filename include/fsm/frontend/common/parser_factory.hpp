/**
 * @file parser_factory.hpp
 * @brief Factory for discovering, instantiating, and content-detecting state machine parsers.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::frontend {

/**
 * @brief Factory class for dynamically constructing frontend parsers based on format, extension, or content heuristics.
 */
class ParserFactory {
  public:
    /**
     * @brief Instantiates a parser corresponding to an explicit format identifier.
     * @param format_name Format name string (e.g. "plantuml", "mermaid", "sysml2", "scxml", "dot", "stateflow",
     * "cameo").
     * @return Unique pointer to the allocated IParser implementation, or nullptr if unknown.
     */
    static std::unique_ptr<IParser> create_by_format(std::string_view format_name);

    /**
     * @brief Instantiates a parser deduced from the file path extension.
     * @param file_path Path to the state machine file (e.g. "model.puml", "system.sysml", "graph.mmd").
     * @return Unique pointer to the allocated IParser implementation, or nullptr if unrecognized.
     */
    static std::unique_ptr<IParser> create_by_extension(std::string_view file_path);

    /**
     * @brief Resolves and instantiates a parser, with optional explicit format override.
     * @param file_path File path to examine if format_override is empty.
     * @param format_override Explicit format string if forced by user/caller.
     * @return Unique pointer to the allocated IParser implementation.
     */
    static std::unique_ptr<IParser> create(std::string_view file_path, std::string_view format_override = "");

    /**
     * @brief Analyzes source content using heuristic signatures to detect its format.
     * @param source Raw source document content.
     * @return Detected format string (e.g. "plantuml", "mermaid", "scxml", "sysml2", "dot", "json").
     */
    static std::string detect_format_from_content(std::string_view source);

    /**
     * @brief Retrieves the semantic classification (Formal vs Diagram) for a format name.
     * @param format_name The format name string.
     * @return FrontendKind classification.
     */
    static FrontendKind get_kind_for_format(std::string_view format_name);

    /**
     * @brief Returns the complete list of supported format identifiers.
     */
    static std::vector<std::string> supported_formats();
};

}  // namespace fsm::frontend
