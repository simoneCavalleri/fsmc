/**
 * @file test_stateflow_slx_ingestion.cpp
 * @brief Unit test suite for direct ingestion of MathWorks Stateflow .slx container archives.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
#include <zlib.h>
#endif

#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/formal/stateflow_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend;
using namespace fsm::frontend::formal;
using namespace fsm::ir;

namespace {

void write_u16_le(std::string& s, uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void write_u32_le(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::string build_mock_slx(const std::string& xml_content, const std::string& entry_name = "simulink/stateflow.xml",
                           bool use_deflate = false) {
    std::string data = xml_content;
    uint16_t method = 0;
    uint32_t uncomp_size = static_cast<uint32_t>(xml_content.size());
    uint32_t comp_size = uncomp_size;

#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
    if (use_deflate) {
        method = 8;
        std::string deflated;
        deflated.resize(xml_content.size() * 2 + 64);
        z_stream strm{};
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(xml_content.data()));
        strm.avail_in = uncomp_size;
        strm.next_out = reinterpret_cast<Bytef*>(deflated.data());
        strm.avail_out = static_cast<uInt>(deflated.size());

        deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        deflate(&strm, Z_FINISH);
        deflateEnd(&strm);

        deflated.resize(strm.total_out);
        data = std::move(deflated);
        comp_size = static_cast<uint32_t>(data.size());
    }
#else
    (void)use_deflate;
#endif

    std::string zip;
    uint32_t local_header_offset = 0;

    // Local File Header (30 bytes)
    write_u32_le(zip, 0x04034b50);  // Local header signature
    write_u16_le(zip, 20);          // Version needed
    write_u16_le(zip, 0);           // Flags
    write_u16_le(zip, method);      // Compression method
    write_u16_le(zip, 0);           // Time
    write_u16_le(zip, 0);           // Date
    write_u32_le(zip, 0);           // CRC-32
    write_u32_le(zip, comp_size);
    write_u32_le(zip, uncomp_size);
    write_u16_le(zip, static_cast<uint16_t>(entry_name.size()));
    write_u16_le(zip, 0);  // Extra field length
    zip += entry_name;
    zip += data;

    // Central Directory Header (46 bytes)
    uint32_t cd_offset = static_cast<uint32_t>(zip.size());
    write_u32_le(zip, 0x02014b50);  // Central directory signature
    write_u16_le(zip, 20);          // Version made by
    write_u16_le(zip, 20);          // Version needed
    write_u16_le(zip, 0);           // Flags
    write_u16_le(zip, method);      // Method
    write_u16_le(zip, 0);           // Time
    write_u16_le(zip, 0);           // Date
    write_u32_le(zip, 0);           // CRC-32
    write_u32_le(zip, comp_size);
    write_u32_le(zip, uncomp_size);
    write_u16_le(zip, static_cast<uint16_t>(entry_name.size()));
    write_u16_le(zip, 0);  // Extra field length
    write_u16_le(zip, 0);  // Comment length
    write_u16_le(zip, 0);  // Disk number start
    write_u16_le(zip, 0);  // Internal attributes
    write_u32_le(zip, 0);  // External attributes
    write_u32_le(zip, local_header_offset);
    zip += entry_name;

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;

    // End of Central Directory (EOCD - 22 bytes)
    write_u32_le(zip, 0x06054b50);  // EOCD signature
    write_u16_le(zip, 0);           // Disk number
    write_u16_le(zip, 0);           // CD disk
    write_u16_le(zip, 1);           // Entries on disk
    write_u16_le(zip, 1);           // Total entries
    write_u32_le(zip, cd_size);     // CD size
    write_u32_le(zip, cd_offset);   // CD offset
    write_u16_le(zip, 0);           // Comment length

    return zip;
}

const char* kSampleStateflowXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Stateflow>
    <machine name="PowertrainSystem">
        <chart id="100" name="TransmissionController" initial="Park">
            <state id="1" name="Park"/>
            <state id="2" name="Drive"/>
            <state id="3" name="Reverse"/>
            <transition src="Park" dst="Drive" labelString="ShiftDrive [brakePressed &gt; 0] / { engageClutch(); }"/>
            <transition src="Drive" dst="Park" labelString="ShiftPark [speed == 0]"/>
        </chart>
    </machine>
</Stateflow>
)";

/**
 * @brief Verify direct ingestion of uncompressed (stored) Stateflow .slx container.
 * @scenario Create in-memory .slx zip container containing 'simulink/stateflow.xml'.
 * @expected Parser unpacks container transparently and constructs valid FsmIr.
 */
TEST(StateflowSlxIngestion, IngestStoredSlxContainer_ParsesModelDirectly) {
    std::string slx_bytes = build_mock_slx(kSampleStateflowXml, "simulink/stateflow.xml", false);

    StateflowParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(slx_bytes, model, err);

    ASSERT_TRUE(ok) << "Parse error: " << err;
    EXPECT_EQ(model.name, "TransmissionController");
    EXPECT_EQ(model.initial_state, "Park");
    EXPECT_EQ(model.states.size(), 3);
    EXPECT_NE(model.find_state("Park"), nullptr);
    EXPECT_NE(model.find_state("Drive"), nullptr);
    EXPECT_NE(model.find_state("Reverse"), nullptr);
    EXPECT_EQ(model.transitions.size(), 2);
}

/**
 * @brief Verify direct ingestion of Deflate-compressed Stateflow .slx container.
 * @scenario Create in-memory .slx zip container compressed using Deflate.
 * @expected Parser decompresses stream and successfully populates FsmIr.
 */
TEST(StateflowSlxIngestion, IngestDeflatedSlxContainer_ParsesModelDirectly) {
#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
    std::string slx_bytes = build_mock_slx(kSampleStateflowXml, "simulink/stateflow.xml", true);

    StateflowParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(slx_bytes, model, err);

    ASSERT_TRUE(ok) << "Parse error: " << err;
    EXPECT_EQ(model.name, "TransmissionController");
    EXPECT_EQ(model.states.size(), 3);
#else
    GTEST_SKIP() << "Compiled without ZLIB; skipping deflate decompression test";
#endif
}

/**
 * @brief Verify fallback ingestion from 'simulink/blockdiagram.xml' when stateflow.xml is absent.
 * @scenario .slx container with chart stored in blockdiagram.xml.
 * @expected Parser detects fallback entry and parses chart.
 */
TEST(StateflowSlxIngestion, IngestBlockdiagramXmlFallback_ParsesModelDirectly) {
    std::string slx_bytes = build_mock_slx(kSampleStateflowXml, "simulink/blockdiagram.xml", false);

    StateflowParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(slx_bytes, model, err);

    ASSERT_TRUE(ok) << "Parse error: " << err;
    EXPECT_EQ(model.name, "TransmissionController");
    EXPECT_EQ(model.states.size(), 3);
}

/**
 * @brief Verify ParserFactory deduction and content detection for .slx files.
 * @scenario Query ParserFactory by extension '.slx', format name 'slx', and magic bytes PK\x03\x04.
 * @expected All inquiries instantiate StateflowParser and classify format as Formal.
 */
TEST(StateflowSlxIngestion, ParserFactory_DetectsSlxAndInstantiatesStateflowParser) {
    auto p1 = ParserFactory::create_by_extension("controller.slx");
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->format_name(), "stateflow");

    auto p2 = ParserFactory::create_by_format("slx");
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2->format_name(), "stateflow");

    std::string slx_bytes = build_mock_slx(kSampleStateflowXml, "simulink/stateflow.xml", false);
    std::string fmt = ParserFactory::detect_format_from_content(slx_bytes);
    EXPECT_EQ(fmt, "stateflow");

    EXPECT_EQ(ParserFactory::get_kind_for_format("slx"), FrontendKind::Formal);
}

/**
 * @brief Verify rejection of corrupted or empty ZIP containers.
 * @scenario Pass truncated PK magic header without actual entries.
 * @expected Parser returns false with clear diagnostic error message.
 */
TEST(StateflowSlxIngestion, CorruptedSlxContainer_FailsGracefully) {
    std::string corrupted_zip = "PK\x03\x04_truncated_content";
    StateflowParser parser;
    FsmIr model;
    std::string err;
    bool ok = parser.parse(corrupted_zip, model, err);

    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("Stateflow Parser:"), std::string::npos);
}

}  // namespace
