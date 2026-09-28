/**
 * @file concurrency_semantics.hpp
 * @brief Mathematical / Formal Concurrency Semantics Configuration for FSM execution.
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace fsm::ir {

/**
 * @brief Formal model of event consumption and step execution.
 */
enum class EventDispatchSemantics : std::uint8_t {
    SingleEventRunToCompletion,  ///< Standard UML/SysML atomic step to state quiescence
    SynchronousReactive,         ///< Esterel/Lustre zero-time synchronous clock tick
    ActiveObjectAsynchronous     ///< Actor mailbox with sequential deferred event consumption
};

/**
 * @brief Converts EventDispatchSemantics to its canonical string representation.
 */
[[nodiscard]] constexpr std::string_view event_dispatch_semantics_to_string(EventDispatchSemantics sem) noexcept {
    switch (sem) {
        case EventDispatchSemantics::SingleEventRunToCompletion:
            return "SingleEventRunToCompletion";
        case EventDispatchSemantics::SynchronousReactive:
            return "SynchronousReactive";
        case EventDispatchSemantics::ActiveObjectAsynchronous:
            return "ActiveObjectAsynchronous";
    }
    return "SingleEventRunToCompletion";
}

/**
 * @brief Parses an EventDispatchSemantics string label into an enum value.
 */
[[nodiscard]] constexpr EventDispatchSemantics string_to_event_dispatch_semantics(std::string_view str) noexcept {
    if (str == "SynchronousReactive")
        return EventDispatchSemantics::SynchronousReactive;
    if (str == "ActiveObjectAsynchronous")
        return EventDispatchSemantics::ActiveObjectAsynchronous;
    return EventDispatchSemantics::SingleEventRunToCompletion;
}

/**
 * @brief Semantic conflict resolution across orthogonal concurrent regions.
 */
enum class OrthogonalConflictResolution : std::uint8_t {
    Interleaved,              ///< Asynchronous non-deterministic interleaving (nuXmv async)
    SimultaneousSynchronous,  ///< True synchronous lockstep firing with static race check
    PriorityOrdered,          ///< Explicit priority ordering of orthogonal micro-steps
    DocumentOrder             ///< W3C SCXML normative traversal order
};

/**
 * @brief Converts OrthogonalConflictResolution to its canonical string representation.
 */
[[nodiscard]] constexpr std::string_view orthogonal_conflict_resolution_to_string(
    OrthogonalConflictResolution res) noexcept {
    switch (res) {
        case OrthogonalConflictResolution::Interleaved:
            return "Interleaved";
        case OrthogonalConflictResolution::SimultaneousSynchronous:
            return "SimultaneousSynchronous";
        case OrthogonalConflictResolution::PriorityOrdered:
            return "PriorityOrdered";
        case OrthogonalConflictResolution::DocumentOrder:
            return "DocumentOrder";
    }
    return "SimultaneousSynchronous";
}

/**
 * @brief Parses an OrthogonalConflictResolution string label into an enum value.
 */
[[nodiscard]] constexpr OrthogonalConflictResolution string_to_orthogonal_conflict_resolution(
    std::string_view str) noexcept {
    if (str == "Interleaved")
        return OrthogonalConflictResolution::Interleaved;
    if (str == "PriorityOrdered")
        return OrthogonalConflictResolution::PriorityOrdered;
    if (str == "DocumentOrder")
        return OrthogonalConflictResolution::DocumentOrder;
    return OrthogonalConflictResolution::SimultaneousSynchronous;
}

/**
 * @brief Memory and datapath isolation model across concurrent regions.
 */
enum class DatapathIsolation : std::uint8_t {
    SharedDatapath,      ///< Shared state variables across parallel regions (requires race-checking)
    PartitionedDatapath  ///< Shared-nothing isolation: regions communicate via internal signals
};

/**
 * @brief Converts DatapathIsolation to its canonical string representation.
 */
[[nodiscard]] constexpr std::string_view datapath_isolation_to_string(DatapathIsolation iso) noexcept {
    switch (iso) {
        case DatapathIsolation::SharedDatapath:
            return "SharedDatapath";
        case DatapathIsolation::PartitionedDatapath:
            return "PartitionedDatapath";
    }
    return "SharedDatapath";
}

/**
 * @brief Parses a DatapathIsolation string label into an enum value.
 */
[[nodiscard]] constexpr DatapathIsolation string_to_datapath_isolation(std::string_view str) noexcept {
    if (str == "PartitionedDatapath")
        return DatapathIsolation::PartitionedDatapath;
    return DatapathIsolation::SharedDatapath;
}

/**
 * @brief Canonical 3-dimensional concurrency semantics configuration.
 *
 * Defines mathematical execution guarantees:
 * - Dimension A: Event dispatch model (RTC, Synchronous Reactive, Async Active Object)
 * - Dimension B: Orthogonal conflict resolution (Interleaved, Synchronous, Priority, DocumentOrder)
 * - Dimension C: Datapath isolation (Shared vs Partitioned)
 */
struct ConcurrencySemantics {
    EventDispatchSemantics dispatch{EventDispatchSemantics::SingleEventRunToCompletion};
    OrthogonalConflictResolution orthogonal_conflict{OrthogonalConflictResolution::SimultaneousSynchronous};
    DatapathIsolation datapath_isolation{DatapathIsolation::SharedDatapath};

    /**
     * @brief Validates semantic consistency across the concurrency dimensions.
     *
     * Invariants:
     * - SynchronousReactive requires simultaneous lockstep and cannot be combined with Interleaved.
     */
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        if (dispatch == EventDispatchSemantics::SynchronousReactive &&
            orthogonal_conflict == OrthogonalConflictResolution::Interleaved) {
            return false;  // Semantic contradiction: synchronous reactive clock prohibits asynchronous interleaving
        }
        return true;
    }

    bool operator==(const ConcurrencySemantics& other) const noexcept = default;
};

/**
 * @brief Preemption precedence between ancestor (outer) and child (inner) transitions in hierarchy.
 */
enum class HierarchicalPriority : std::uint8_t {
    OuterFirst,  ///< UML 2.5, SysML v2, Stateflow: Enclosing composite states preempt substates
    InnerFirst   ///< W3C SCXML, Rhapsody: Inner substates preempt enclosing composite transitions
};

[[nodiscard]] constexpr std::string_view hierarchical_priority_to_string(HierarchicalPriority prio) noexcept {
    switch (prio) {
        case HierarchicalPriority::OuterFirst:
            return "OuterFirst";
        case HierarchicalPriority::InnerFirst:
            return "InnerFirst";
    }
    return "OuterFirst";
}

[[nodiscard]] constexpr HierarchicalPriority string_to_hierarchical_priority(std::string_view str) noexcept {
    if (str == "InnerFirst")
        return HierarchicalPriority::InnerFirst;
    return HierarchicalPriority::OuterFirst;
}

/**
 * @brief Unified formal execution semantics specification across concurrency, hierarchy, and dispatch.
 */
struct ExecutionSemantics {
    EventDispatchSemantics dispatch_model{EventDispatchSemantics::SingleEventRunToCompletion};
    HierarchicalPriority preemption_priority{HierarchicalPriority::OuterFirst};
    OrthogonalConflictResolution orthogonal_scheduling{OrthogonalConflictResolution::SimultaneousSynchronous};
    DatapathIsolation datapath_isolation{DatapathIsolation::SharedDatapath};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        if (dispatch_model == EventDispatchSemantics::SynchronousReactive &&
            orthogonal_scheduling == OrthogonalConflictResolution::Interleaved) {
            return false;
        }
        return true;
    }

    bool operator==(const ExecutionSemantics& other) const noexcept = default;
};

}  // namespace fsm::ir
