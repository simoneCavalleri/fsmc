/**
 * @file manifest_emitter.hpp
 * @brief Machine-readable system manifest emitter (*.manifest.json) for orchestrators, build systems, and AI agents.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend {

/**
 * @struct ManifestArtifacts
 * @brief Generated code artifact paths recorded in *.manifest.json.
 */
struct ManifestArtifacts {
    std::string ports_header;
    std::string fsm_header;
    std::string c_header;
    std::string c_source;
    bool standalone = false;
};

/**
 * @struct ManifestVerificationSummary
 * @brief Summary of middle-end / formal verification and real-time scheduling bounds recorded in *.manifest.json.
 */
struct ManifestVerificationSummary {
    bool verified = false;
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t properties_checked = 0;
    std::size_t wcet_max_microsteps = 0;
    std::size_t max_event_queue_bound = 0;
    bool zeno_free = true;
};

/**
 * @class ManifestEmitter
 * @brief Emits canonical, deterministic *.manifest.json describing an FSM's external contract and artifacts.
 */
class ManifestEmitter {
  public:
    static constexpr std::string_view kManifestSchemaVersion = "1.0";

    [[nodiscard]] static std::string emit_json(const ir::FsmIr& model, const ManifestArtifacts& artifacts = {},
                                               const ManifestVerificationSummary& verification = {});
};

}  // namespace fsm::backend
