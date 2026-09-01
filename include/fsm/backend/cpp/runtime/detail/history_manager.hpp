#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>

#include "fsm/backend/cpp/runtime/static_vector.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/type_list.hpp"

namespace fsm {

struct history_entry {
    std::string_view parent{};   ///< Parent composite state name
    std::string_view substate{}; ///< Recorded active substate name

    constexpr bool operator==(const history_entry& other) const noexcept {
        return parent == other.parent && substate == other.substate;
    }
};

namespace detail {

template <typename Table, bool HasHistory>
class history_manager;

// Specialization for state machines WITH history states
template <typename Table>
class history_manager<Table, true> {
  public:
    static constexpr std::size_t raw_capacity = count_parent_states_v<typename Table::states>;
    static constexpr std::size_t max_history_capacity = (raw_capacity > 0 ? raw_capacity : 1);

    void record_history(std::string_view parent, std::string_view substate) {
        if (parent.empty() || substate.empty()) {
            return;
        }
        for (auto& entry : history_records_) {
            if (entry.parent == parent) {
                entry.substate = substate;
                return;
            }
        }
        history_records_.push_back({parent, substate});
    }

    [[nodiscard]] std::string_view get_history(std::string_view parent) const noexcept {
        for (const auto& entry : history_records_) {
            if (entry.parent == parent) {
                return entry.substate;
            }
        }
        return "";
    }

    void clear_history() noexcept {
        history_records_.clear();
    }

  private:
    static_vector<history_entry, max_history_capacity> history_records_{};
};

// Specialization for state machines WITHOUT history states (Zero-overhead, 0 bytes)
template <typename Table>
class history_manager<Table, false> {
  public:
    static constexpr std::size_t max_history_capacity = 0;

    void record_history(std::string_view /*parent*/, std::string_view /*substate*/) noexcept {}
    [[nodiscard]] std::string_view get_history(std::string_view /*parent*/) const noexcept { return ""; }
    void clear_history() noexcept {}
};

}  // namespace detail
}  // namespace fsm
