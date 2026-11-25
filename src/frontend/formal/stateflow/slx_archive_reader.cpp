#include "slx_archive_reader.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>
#ifdef FSMC_HAS_ZLIB
#include <zlib.h>
#endif

namespace fsm::frontend::formal::stateflow_detail {

uint16_t read_u16_le(const unsigned char* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const unsigned char* p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint32_t MAX_UNCOMP_SIZE = 64u * 1024u * 1024u;  // 64 MB cap against ZIP bombs

bool decompress_entry(uint16_t method, const unsigned char* data_ptr, uint32_t comp_size, uint32_t uncomp_size,
                      const std::string& filename, std::string& out, std::string& err) {
    if (uncomp_size > MAX_UNCOMP_SIZE) {
        err = "Stateflow Parser: Uncompressed size (" + std::to_string(uncomp_size) + ") exceeds maximum limit for '" +
              filename + "'";
        return false;
    }
    if (method == 0) {
        // Stored (uncompressed)
        if (uncomp_size != comp_size) {
            err = "Stateflow Parser: Stored ZIP entry '" + filename +
                  "' has mismatched compressed and uncompressed sizes";
            return false;
        }
        out.assign(reinterpret_cast<const char*>(data_ptr), uncomp_size);
        return true;
    }
    if (method == 8) {
#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
        out.resize(uncomp_size);
        z_stream strm{};
        strm.next_in = const_cast<Bytef*>(data_ptr);
        strm.avail_in = comp_size;
        strm.next_out = reinterpret_cast<Bytef*>(out.data());
        strm.avail_out = uncomp_size;

        if (inflateInit2(&strm, -15) != Z_OK) {
            err = "Stateflow Parser: Failed to initialize zlib inflate for '" + filename + "'";
            return false;
        }
        int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (ret != Z_STREAM_END) {
            err = "Stateflow Parser: Decompression failed for entry '" + filename + "'";
            return false;
        }
        out.resize(strm.total_out);
        return true;
#else
        (void)comp_size;
        (void)uncomp_size;
        (void)data_ptr;
        err = "Stateflow Parser: .slx entry '" + filename +
              "' is compressed with Deflate, but FSMC was compiled without ZLIB support.";
        return false;
#endif
    }
    err = "Stateflow Parser: Unsupported compression method (" + std::to_string(method) + ") in .slx";
    return false;
}

bool extract_stateflow_from_slx(std::string_view zip_content, std::string& extracted_xml, std::string& err) {
    if (zip_content.size() < 22) {
        err = "Stateflow Parser: File size too small for .slx archive.";
        return false;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(zip_content.data());
    const std::size_t total_size = zip_content.size();

    // 1. Try Central Directory method via End of Central Directory (EOCD)
    std::size_t search_start = (total_size > 65557) ? total_size - 65557 : 0;
    std::optional<std::size_t> eocd_pos;
    for (std::size_t i = total_size - 22; i >= search_start; --i) {
        if (read_u32_le(bytes + i) == 0x06054b50) {
            eocd_pos = i;
            break;
        }
        if (i == 0)
            break;
    }

    std::string fallback_blockdiagram_xml;

    if (eocd_pos.has_value()) {
        std::size_t ep = *eocd_pos;
        uint16_t num_entries = read_u16_le(bytes + ep + 10);
        uint32_t cd_offset = read_u32_le(bytes + ep + 16);

        std::size_t cd_pos = cd_offset;
        for (uint16_t e = 0; e < num_entries && cd_pos + 46 <= total_size; ++e) {
            if (read_u32_le(bytes + cd_pos) != 0x02014b50) {
                break;
            }
            uint16_t method = read_u16_le(bytes + cd_pos + 10);
            uint32_t comp_size = read_u32_le(bytes + cd_pos + 20);
            uint32_t uncomp_size = read_u32_le(bytes + cd_pos + 24);
            uint16_t fn_len = read_u16_le(bytes + cd_pos + 28);
            uint16_t extra_len = read_u16_le(bytes + cd_pos + 30);
            uint16_t comment_len = read_u16_le(bytes + cd_pos + 32);
            uint32_t local_hdr_offset = read_u32_le(bytes + cd_pos + 42);

            if (cd_pos + 46 + fn_len > total_size) {
                break;
            }
            std::string filename(reinterpret_cast<const char*>(bytes + cd_pos + 46), fn_len);

            bool is_stateflow = (filename == "simulink/stateflow.xml" || filename == "stateflow.xml" ||
                                 filename.find("stateflow.xml") != std::string::npos);
            bool is_blockdiagram =
                (!is_stateflow && (filename == "simulink/blockdiagram.xml" || filename == "blockdiagram.xml" ||
                                   filename.find("blockdiagram.xml") != std::string::npos));

            if ((is_stateflow || is_blockdiagram) && static_cast<std::size_t>(local_hdr_offset) + 30 <= total_size) {
                uint16_t loc_fn_len = read_u16_le(bytes + local_hdr_offset + 26);
                uint16_t loc_extra_len = read_u16_le(bytes + local_hdr_offset + 28);
                std::size_t data_offset = static_cast<std::size_t>(local_hdr_offset) + 30 + loc_fn_len + loc_extra_len;

                bool bounds_ok = (data_offset <= total_size && comp_size <= total_size - data_offset);
                if (bounds_ok && method == 0) {
                    bounds_ok = (uncomp_size == comp_size && uncomp_size <= total_size - data_offset);
                }
                if (bounds_ok) {
                    std::string decomp;
                    if (decompress_entry(method, bytes + data_offset, comp_size, uncomp_size, filename, decomp, err)) {
                        if (is_stateflow) {
                            extracted_xml = std::move(decomp);
                            return true;
                        }
                        if (is_blockdiagram && fallback_blockdiagram_xml.empty()) {
                            fallback_blockdiagram_xml = std::move(decomp);
                        }
                    } else if (is_stateflow) {
                        return false;
                    }
                } else if (is_stateflow) {
                    err = "Stateflow Parser: Corrupted ZIP entry bounds for '" + filename + "'";
                    return false;
                }
            }

            cd_pos += 46 + fn_len + extra_len + comment_len;
        }

        if (!fallback_blockdiagram_xml.empty()) {
            extracted_xml = std::move(fallback_blockdiagram_xml);
            return true;
        }
    }

    // 2. Fallback to scanning Local File Headers from beginning
    std::size_t offset = 0;
    while (offset + 30 <= total_size) {
        if (read_u32_le(bytes + offset) != 0x04034b50) {
            break;
        }
        uint16_t method = read_u16_le(bytes + offset + 8);
        uint32_t comp_size = read_u32_le(bytes + offset + 18);
        uint32_t uncomp_size = read_u32_le(bytes + offset + 22);
        uint16_t fn_len = read_u16_le(bytes + offset + 26);
        uint16_t extra_len = read_u16_le(bytes + offset + 28);

        std::size_t header_len = static_cast<std::size_t>(30) + fn_len + extra_len;
        if (header_len > total_size - offset || comp_size > total_size - offset - header_len) {
            break;
        }
        std::size_t data_offset = offset + header_len;

        std::string filename(reinterpret_cast<const char*>(bytes + offset + 30), fn_len);
        bool is_stateflow = (filename == "simulink/stateflow.xml" || filename == "stateflow.xml" ||
                             filename.find("stateflow.xml") != std::string::npos);
        bool is_blockdiagram =
            (!is_stateflow && (filename == "simulink/blockdiagram.xml" || filename == "blockdiagram.xml" ||
                               filename.find("blockdiagram.xml") != std::string::npos));

        if (is_stateflow || is_blockdiagram) {
            if (method == 0 && (uncomp_size != comp_size || uncomp_size > total_size - data_offset)) {
                if (is_stateflow) {
                    err = "Stateflow Parser: Stored ZIP entry '" + filename + "' exceeds archive bounds";
                    return false;
                }
            } else {
                std::string decomp;
                if (decompress_entry(method, bytes + data_offset, comp_size, uncomp_size, filename, decomp, err)) {
                    if (is_stateflow) {
                        extracted_xml = std::move(decomp);
                        return true;
                    }
                    if (is_blockdiagram && fallback_blockdiagram_xml.empty()) {
                        fallback_blockdiagram_xml = std::move(decomp);
                    }
                } else if (is_stateflow) {
                    return false;
                }
            }
        }

        offset = data_offset + comp_size;
    }

    if (!fallback_blockdiagram_xml.empty()) {
        extracted_xml = std::move(fallback_blockdiagram_xml);
        return true;
    }

    err = "Stateflow Parser: No 'simulink/stateflow.xml' or 'simulink/blockdiagram.xml' found inside .slx archive.";
    return false;
}

}  // namespace fsm::frontend::formal::stateflow_detail
