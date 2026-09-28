/**
 * @file deterministic_id.hpp
 * @brief Canonical 64-bit FNV-1a deterministic hash generator for model element identifiers.
 */

#pragma once

#include <string>
#include <string_view>

namespace fsm::ir {

/**
 * @brief Computes a deterministic, order-invariant 64-bit hex hash identifier from canonical strings.
 *
 * Employs the FNV-1a (Fowler-Noll-Vo) algorithm to yield reproducible 16-character hex identifiers
 * across compilation runs and target platforms.
 *
 * @param canonical_str Canonical string representing the model element (e.g. FQN, transition signature).
 * @return 16-character lowercase hexadecimal hash string.
 */
[[nodiscard]] std::string compute_deterministic_id(std::string_view canonical_str);

}  // namespace fsm::ir
