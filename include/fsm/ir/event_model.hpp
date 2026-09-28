/**
 * @file event_model.hpp
 * @brief Event Metadata and Choice Pseudostate Branching Models in FSM IR.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "fsm/ir/transition_edge.hpp"

namespace fsm::ir {

/**
 * @brief Metadata model representing an event trigger registered within the FSM.
 */
struct EventModel {
    std::string name;         ///< Unique event name identifier
    std::string description;  ///< Optional human-readable documentation comment

    explicit EventModel(std::string event_name = "", std::string event_desc = "")
        : name(std::move(event_name)), description(std::move(event_desc)) {}

    bool operator<(const EventModel& other) const noexcept { return name < other.name; }
};

/**
 * @brief Model representing a dynamic Choice pseudostate and its outgoing evaluated branches.
 */
struct ChoiceNodeModel {
    std::string name;                               ///< Pseudostate identifier (e.g. "CheckAuth")
    std::vector<TransitionEdge> outgoing_branches;  ///< Evaluated conditional branch edges

    explicit ChoiceNodeModel(std::string choice_name = "") : name(std::move(choice_name)) {}
};

}  // namespace fsm::ir
