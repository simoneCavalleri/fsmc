#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace fsm::codegen {

// ============================================================================
// Deterministic Hash Generator for IDs (Order-Invariant / Canonical)
// ============================================================================

inline std::string compute_deterministic_id(std::string_view canonical_str) {
    // 64-bit FNV-1a hash
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : canonical_str) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << "id_" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

}  // namespace fsm::codegen
