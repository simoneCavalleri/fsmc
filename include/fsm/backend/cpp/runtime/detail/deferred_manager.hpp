#pragma once

#include <cstddef>
#include <type_traits>
#include <variant>

#include "fsm/backend/cpp/runtime/static_vector.hpp"
#include "fsm/backend/cpp/runtime/traits/type_list.hpp"

namespace fsm::detail {

template <typename Table, std::size_t DeferredCapacity, bool HasDeferred>
class deferred_manager;

// Specialization for state machines WITH deferred events
template <typename Table, std::size_t DeferredCapacity>
class deferred_manager<Table, DeferredCapacity, true> {
  public:
    static constexpr std::size_t max_deferred_capacity = DeferredCapacity;
    using event_variant = typename Table::event_variant;

    template <typename Event>
    void defer_event(const Event& event) {
        deferred_queue_.push_back(event_variant{event});
    }

    [[nodiscard]] std::size_t deferred_count() const noexcept {
        return deferred_queue_.size();
    }

    void clear_deferred_events() noexcept {
        deferred_queue_.clear();
    }

    template <typename DispatchDirectFn>
    void process_deferred_queue(DispatchDirectFn&& dispatch_direct_fn) {
        if (deferred_queue_.empty() || is_replaying_deferred_) {
            return;
        }
        is_replaying_deferred_ = true;

        bool any_handled = true;
        while (any_handled && !deferred_queue_.empty()) {
            any_handled = false;
            for (auto it = deferred_queue_.begin(); it != deferred_queue_.end();) {
                bool handled = std::visit(
                    [&dispatch_direct_fn](const auto& ev) -> bool {
                        return dispatch_direct_fn(ev).is_success();
                    },
                    *it);
                if (handled) {
                    it = deferred_queue_.erase(it);
                    any_handled = true;
                    break;
                }
                ++it;
            }
        }

        is_replaying_deferred_ = false;
    }

  private:
    static_vector<event_variant, max_deferred_capacity> deferred_queue_{};
    bool is_replaying_deferred_{false};
};

// Specialization for state machines WITHOUT deferred events (Zero-overhead, 0 bytes)
template <typename Table, std::size_t DeferredCapacity>
class deferred_manager<Table, DeferredCapacity, false> {
  public:
    static constexpr std::size_t max_deferred_capacity = 0;

    template <typename Event>
    void defer_event(const Event& /*event*/) noexcept {}
    [[nodiscard]] std::size_t deferred_count() const noexcept { return 0; }
    void clear_deferred_events() noexcept {}

    template <typename DispatchDirectFn>
    void process_deferred_queue(DispatchDirectFn&& /*dispatch_direct_fn*/) noexcept {}
};

}  // namespace fsm::detail
