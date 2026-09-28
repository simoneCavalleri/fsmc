/**
 * @file fsm_graph_ops.hpp
 * @brief Graph operations, structural hierarchy normalization, and AST canonicalization algorithms for FsmIr.
 */

#pragma once

#include <string>

namespace fsm::ir {

struct FsmIr;

/**
 * @brief Graph operations, structural hierarchy normalization, and AST canonicalization algorithms for FsmIr.
 */
class FsmGraphOps {
  public:
    /**
     * @brief Normalizes the state hierarchy, resolving root states, parent-child links,
     * and computing composite state classifications.
     * @param ir The state machine IR to normalize in-place.
     */
    static void normalize_hierarchy(FsmIr& ir);

    /**
     * @brief Automatically synthesizes and synchronizes canonical Guard and Action interfaces
     * from all transition edges and state entry/exit actions across the graph.
     * @param ir The state machine IR to synchronize in-place.
     */
    static void sync_interfaces(FsmIr& ir);

    /**
     * @brief Rebuilds contiguous transition adjacency lists on each StateNode for O(1) graph traversal.
     * @param ir The state machine IR to index in-place.
     */
    static void rebuild_adjacency_indices(FsmIr& ir);

    /**
     * @brief Sorts transition edges by priority within each source state (lower non-zero number = higher precedence).
     * @param ir The state machine IR whose transitions are sorted in-place.
     */
    static void sort_transitions_by_priority(FsmIr& ir);

    /**
     * @brief Canonicalizes the FSM model: generates deterministic IDs, sorts all containers,
     * synchronizes guards and actions, and rebuilds graph adjacency indices.
     * @param ir The state machine IR to canonicalize in-place.
     */
    static void canonicalize(FsmIr& ir);

    /**
     * @brief Performs structural well-formedness verification on the IR graph.
     * @param ir The state machine IR to validate.
     * @param[out] error Output error message if malformed.
     * @return True if structurally consistent, false otherwise.
     */
    [[nodiscard]] static bool is_well_formed(const FsmIr& ir, std::string& error) noexcept;
};

}  // namespace fsm::ir
