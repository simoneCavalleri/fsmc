/**
 * @file companion_manifest_emitter.hpp
 * @brief Companion Manifest Serializer (.fsm.yaml, .fsm.json) preserving formal contracts during diagram export.
 */

#pragma once

#include <string>

#include "fsm/frontend/common/companion_manifest_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::diagram {

/**
 * @class CompanionManifestEmitter
 * @brief Serializes state machine data-path contracts, formal properties, and I/O ports
 * into companion manifest sidecars (.fsm.yaml, .fsm.json).
 */
class CompanionManifestEmitter {
  public:
    /**
     * @brief Extracts formal contracts and datapath metadata from FsmIr into a CompanionManifest.
     */
    [[nodiscard]] static frontend::CompanionManifest build_manifest(const ir::FsmIr& ir);

    /**
     * @brief Serializes a CompanionManifest to YAML format (.fsm.yaml).
     */
    [[nodiscard]] static std::string emit_yaml(const frontend::CompanionManifest& manifest);

    /**
     * @brief Serializes a CompanionManifest to JSON format (.fsm.json).
     */
    [[nodiscard]] static std::string emit_json(const frontend::CompanionManifest& manifest);

    /**
     * @brief Convenience method extracting and serializing FsmIr directly to YAML.
     */
    [[nodiscard]] static std::string emit_yaml(const ir::FsmIr& ir) { return emit_yaml(build_manifest(ir)); }

    /**
     * @brief Convenience method extracting and serializing FsmIr directly to JSON.
     */
    [[nodiscard]] static std::string emit_json(const ir::FsmIr& ir) { return emit_json(build_manifest(ir)); }
};

}  // namespace fsm::backend::diagram
