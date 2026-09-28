/**
 * @file region.hpp
 * @brief Orthogonal Concurrent Regions, Port Mappings, and Submachine References.
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fsm::ir {

/**
 * @brief Represents an orthogonal concurrent region executing in parallel within a composite state.
 */
struct OrthogonalRegion {
    std::string id;                      ///< Unique region identifier
    std::string name;                    ///< Local region name
    std::string initial_state_id;        ///< Initial state ID for this specific region
    std::vector<std::string> state_ids;  ///< IDs of all member states encapsulated by this region
    std::uint32_t priority = 0;          ///< Precedence order for PriorityOrdered dispatch (lower = higher priority)

    bool operator==(const OrthogonalRegion& other) const noexcept {
        return id == other.id && name == other.name && initial_state_id == other.initial_state_id &&
               state_ids == other.state_ids && priority == other.priority;
    }
};

/**
 * @brief Port binding mapping an outer submachine call to an inner encapsulated statechart endpoint.
 */
struct PortMapping {
    std::string entry_point;  ///< Outer entry point port / signal
    std::string exit_point;   ///< Inner exit point port / signal

    PortMapping() = default;
    PortMapping(std::string entry, std::string exit) : entry_point(std::move(entry)), exit_point(std::move(exit)) {}

    bool operator==(const PortMapping& other) const noexcept {
        return entry_point == other.entry_point && exit_point == other.exit_point;
    }
};

/**
 * @brief Reference invocation of an external modular submachine statechart.
 */
struct SubmachineRef {
    std::string fsm_name;                    ///< Referenced target state machine name
    std::string source_uri;                  ///< URI or relative file path to the target model
    std::vector<PortMapping> port_mappings;  ///< Interface port and event mappings

    SubmachineRef() = default;
    explicit SubmachineRef(std::string target_name, std::string uri = "")
        : fsm_name(std::move(target_name)), source_uri(std::move(uri)) {}

    bool operator==(const SubmachineRef& other) const noexcept {
        return fsm_name == other.fsm_name && source_uri == other.source_uri && port_mappings == other.port_mappings;
    }
};

}  // namespace fsm::ir
