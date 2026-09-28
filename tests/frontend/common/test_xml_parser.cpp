/**
 * @file test_xml_parser.cpp
 * @brief Unit test suite for the lightweight XML parser and DOM abstraction.
 */

#include <gtest/gtest.h>

#include "fsm/frontend/common/xml_parser.hpp"

using namespace fsm::frontend;

namespace {

/**
 * @brief Verify basic XML parsing and hierarchical element navigation.
 * @scenario Parse an XML snippet with root node, attributes, and text/empty children.
 * @expected Document parsed into XmlNode tree with matching tags, attributes, and children.
 */
TEST(XmlParser, BasicXmlDocument_ParsedIntoElementHierarchy) {
    const std::string xml = R"(
        <root id="root_1" name="TestRoot">
            <child1 attr="val1">Some text content</child1>
            <child2 attr="val2"/>
        </root>
    )";

    std::string err;
    auto node = SimpleXmlParser::parse(xml, err);
    ASSERT_NE(node, nullptr) << "Parse error: " << err;
    EXPECT_EQ(node->tag, "root");
    EXPECT_EQ(node->get_attr("id"), "root_1");
    EXPECT_EQ(node->get_attr("name"), "TestRoot");
    EXPECT_EQ(node->children.size(), 2u);

    auto child1 = node->find_first_child("child1");
    ASSERT_NE(child1, nullptr);
    EXPECT_EQ(child1->text_content, "Some text content");
    EXPECT_EQ(child1->get_attr("attr"), "val1");

    auto child2 = node->find_first_child("child2");
    ASSERT_NE(child2, nullptr);
    EXPECT_EQ(child2->get_attr("attr"), "val2");
}

/**
 * @brief Verify XML entity decoding in attribute values.
 * @scenario Input XML attribute containing standard XML entities (&gt;, &amp;, &lt;, &quot;).
 * @expected Entities decoded into their literal character equivalents.
 */
TEST(XmlParser, EntityReferences_DecodedCorrectly) {
    const std::string xml = R"(
        <condition expr="a &gt; 0 &amp;&amp; b &lt; 10 &amp;&amp; str != &quot;test&quot;"/>
    )";

    std::string err;
    auto node = SimpleXmlParser::parse(xml, err);
    ASSERT_NE(node, nullptr) << "Parse error: " << err;
    EXPECT_EQ(node->get_attr("expr"), "a > 0 && b < 10 && str != \"test\"");
}

/**
 * @brief Verify CDATA section preservation in script blocks.
 * @scenario Input XML containing <![CDATA[ ... ]]> block with raw operators.
 * @expected Content inside CDATA preserved verbatim without entity interpretation.
 */
TEST(XmlParser, CDataSections_ExtractedWithoutEntityDecoding) {
    const std::string xml = R"(
        <script>
            <![CDATA[
                if (x > 0 && y < 10) {
                    doSomething();
                }
            ]]>
        </script>
    )";

    std::string err;
    auto node = SimpleXmlParser::parse(xml, err);
    ASSERT_NE(node, nullptr) << "Parse error: " << err;
    EXPECT_NE(node->text_content.find("x > 0 && y < 10"), std::string::npos);
    EXPECT_NE(node->text_content.find("doSomething();"), std::string::npos);
}

/**
 * @brief Verify XML namespace prefix-agnostic recursive tag lookup.
 * @scenario Ingest OMG XMI document with XML namespaces (xmi:, uml:).
 * @expected Recursive child lookup resolves tags regardless of namespace prefix presence.
 */
TEST(XmlParser, NamespacePrefixedTags_ResolvedAgnostically) {
    const std::string xml = R"(
        <xmi:XMI xmlns:xmi="http://www.omg.org/XMI" xmlns:uml="http://www.eclipse.org/uml2/5.0.0/UML">
            <uml:Model xmi:id="_model_1" name="MyModel">
                <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm_1" name="StateChart"/>
            </uml:Model>
        </xmi:XMI>
    )";

    std::string err;
    auto node = SimpleXmlParser::parse(xml, err);
    ASSERT_NE(node, nullptr) << "Parse error: " << err;

    auto model = node->find_child_recursive("Model");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->get_attr("id"), "_model_1");
    EXPECT_EQ(model->get_attr("name"), "MyModel");

    auto sm = model->find_child_recursive("packagedElement");
    ASSERT_NE(sm, nullptr);
    EXPECT_EQ(sm->get_attr("id"), "_sm_1");
    EXPECT_EQ(sm->get_attr("type"), "uml:StateMachine");
}

}  // namespace
