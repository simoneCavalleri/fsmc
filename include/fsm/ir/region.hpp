#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fsm::codegen {

// ============================================================================
// Orthogonal Region & Submachine States
// ============================================================================

struct OrthogonalRegion {
    std::string id;
    std::string name;
    std::string initial_state_id;
    std::vector<std::string> state_ids;

    bool operator==(const OrthogonalRegion& other) const noexcept {
        return id == other.id && name == other.name && initial_state_id == other.initial_state_id &&
               state_ids == other.state_ids;
    }
};

struct PortMapping {
    std::string entry_point;
    std::string exit_point;

    PortMapping() = default;
    PortMapping(std::string entry, std::string exit) : entry_point(std::move(entry)), exit_point(std::move(exit)) {}

    bool operator==(const PortMapping& other) const noexcept {
        return entry_point == other.entry_point && exit_point == other.exit_point;
    }
};

struct SubmachineRef {
    std::string fsm_name;
    std::string source_uri;
    std::vector<PortMapping> port_mappings;

    SubmachineRef() = default;
    explicit SubmachineRef(std::string target_name, std::string uri = "")
        : fsm_name(std::move(target_name)), source_uri(std::move(uri)) {}

    bool operator==(const SubmachineRef& other) const noexcept {
        return fsm_name == other.fsm_name && source_uri == other.source_uri && port_mappings == other.port_mappings;
    }
};

}  // namespace fsm::codegen
