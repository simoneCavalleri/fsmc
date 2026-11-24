/**
 * @file target_capabilities.hpp
 * @brief Declarative target capability descriptor driving middle-end lowering selection.
 */

#pragma once

namespace fsm::ir {

/**
 * @struct TargetCapabilities
 * @brief Describes which high-level statechart constructs a codegen backend supports natively.
 *
 * The middle-end PassManager inspects these flags to dynamically schedule only the structural
 * lowering passes required by the target (e.g. OrthogonalProductPass, HistoryLoweringPass,
 * DeferredEventLoweringPass, ForkJoinLoweringPass, ChoiceInliningPass, SampledChangeTriggerPass).
 */
struct TargetCapabilities {
    bool supports_hierarchy = false;         ///< Native composite/nested state LCA semantics
    bool supports_orthogonal = false;        ///< Native parallel orthogonal regions
    bool supports_history = false;           ///< Native shallow/deep history pseudo-states
    bool supports_deferred = false;          ///< Native deferred event queues and recall
    bool supports_choice = false;            ///< Native dynamic choice/junction pseudo-states
    bool supports_fork_join = false;         ///< Native fork/join synchronization bars
    bool supports_sampled_triggers = false;  ///< Native continuous/change triggers (when/after-change)

    /**
     * @brief Capabilities of the built-in C++17 zero-overhead runtime backend.
     */
    [[nodiscard]] static constexpr TargetCapabilities cpp17() noexcept {
        TargetCapabilities caps;
        caps.supports_hierarchy = true;
        caps.supports_orthogonal = false;
        caps.supports_history = true;
        caps.supports_deferred = true;
        caps.supports_choice = true;
        caps.supports_fork_join = false;
        caps.supports_sampled_triggers = false;
        return caps;
    }

    /**
     * @brief Capabilities of the built-in C++20 zero-overhead runtime backend.
     */
    [[nodiscard]] static constexpr TargetCapabilities cpp20() noexcept { return cpp17(); }

    /**
     * @brief Capabilities of a flat C99 bare-metal switch-case backend (all high-level constructs lowered).
     */
    [[nodiscard]] static constexpr TargetCapabilities c99_baremetal() noexcept { return TargetCapabilities{}; }

    /**
     * @brief Capabilities of a Rust no_std enum/match backend (choice supported via match guards).
     */
    [[nodiscard]] static constexpr TargetCapabilities rust_no_std() noexcept {
        TargetCapabilities caps;
        caps.supports_choice = true;
        return caps;
    }

    [[nodiscard]] constexpr bool operator==(const TargetCapabilities& other) const noexcept = default;
};

}  // namespace fsm::ir
