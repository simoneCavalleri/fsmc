#include "fsm/ir/deterministic_id.hpp"

#include <cstdio>

namespace fsm::ir {

std::string compute_deterministic_id(std::string_view canonical_str) {
    // 64-bit FNV-1a hash
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : canonical_str) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "id_%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

}  // namespace fsm::ir
