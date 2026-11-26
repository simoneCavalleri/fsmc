/**
 * @file c99_generator.hpp
 * @brief Native Pure-C99 bare-metal code generator (*.h + *.c) requiring zero C++ compiler on target.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::c99 {

using ir::FsmIr;

/**
 * @struct C99GeneratorOptions
 * @brief Configuration options for native Pure-C99 code generation.
 */
struct C99GeneratorOptions {
    std::string header_include;        ///< Include path for the generated .h inside .c (default: "<prefix>.h")
    bool omit_header_include = false;  ///< Omit `#include "<header>"` in .c (for single-TU stdout emission)
};

/**
 * @struct GeneratedC99Bundle
 * @brief Pair of pure C99 files: a strict C99 header (*.h) and a zero-heap C99 implementation (*.c).
 */
struct GeneratedC99Bundle {
    std::string header;  ///< Pure C99 header (*.h) with state/status enums, ports, registers, services, and API
    std::string source;  ///< Pure C99 implementation (*.c) with switch-case dispatch, timers, and snapshots
};

/**
 * @class C99Generator
 * @brief Synthesizes standalone, zero-heap, MISRA-friendly Pure-C99 code from a lowered canonical FsmIr.
 *
 * Unlike CAbiGenerator (which wraps the C++17 runtime behind an extern "C" bridge), C99Generator emits
 * 100% native C99 with zero C++ dependencies, suitable for bare-metal MCUs with C-only toolchains.
 */
class C99Generator {
  public:
    /**
     * @brief Generates the pure C99 header and source pair for a lowered FsmIr model.
     * @param model Canonical FsmIr model (should be lowered via TargetCapabilities::c99_baremetal()).
     * @param options Optional C99 codegen configuration.
     * @return Pair of generated C99 header and source strings.
     */
    [[nodiscard]] static GeneratedC99Bundle generate(const FsmIr& model, const C99GeneratorOptions& options = {});
};

}  // namespace fsm::backend::c99
