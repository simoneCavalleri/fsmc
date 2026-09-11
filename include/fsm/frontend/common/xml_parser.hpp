/**
 * @file xml_parser.hpp
 * @brief Lightweight, zero-dependency XML DOM Node and Tokenizer for SCXML and Cameo XMI.
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::frontend {

/**
 * @brief Represents an XML DOM element with attributes, text content, and child elements.
 */
struct XmlNode {
    std::string tag;                                 ///< XML element tag name (e.g. "scxml", "state", "transition")
    std::map<std::string, std::string> attributes;   ///< Map of XML attribute key-value pairs
    std::string text_content;                        ///< Text or CDATA enclosed within the tag
    std::vector<std::shared_ptr<XmlNode>> children;  ///< Ordered child XML nodes

    /**
     * @brief Looks up an attribute value by name with a default fallback.
     */
    [[nodiscard]] std::string get_attr(const std::string& name, const std::string& default_val = "") const;

    /**
     * @brief Finds all immediate child nodes matching a given tag name.
     */
    [[nodiscard]] std::vector<std::shared_ptr<XmlNode>> find_children(const std::string& tag_name) const;

    /**
     * @brief Finds the first immediate child node matching a given tag name.
     */
    [[nodiscard]] std::shared_ptr<XmlNode> find_first_child(const std::string& tag_name) const;

    /**
     * @brief Recursively searches descendants for the first node matching a tag name.
     */
    [[nodiscard]] std::shared_ptr<XmlNode> find_child_recursive(std::string_view tag_name) const;
};

/**
 * @brief Robust, lightweight XML tokenizer and parser supporting standard XML elements and entities.
 */
class SimpleXmlParser {
  public:
    /**
     * @brief Parses an XML document string into an XmlNode DOM tree.
     * @param xml_text Raw XML text.
     * @param[out] err Error description if malformed.
     * @return Root XmlNode shared pointer, or nullptr on failure.
     */
    static std::shared_ptr<XmlNode> parse(std::string_view xml_text, std::string& err);

    /**
     * @brief Unescapes standard XML entity references (&amp;, &lt;, &gt;, &quot;, &apos;).
     */
    static std::string unescape_xml(std::string str);

  private:
    static std::string trim_sv(std::string_view input_sv);
    static void parse_tag_body(std::string_view body, std::string& out_tag,
                               std::map<std::string, std::string>& out_attrs);
};

}  // namespace fsm::frontend
