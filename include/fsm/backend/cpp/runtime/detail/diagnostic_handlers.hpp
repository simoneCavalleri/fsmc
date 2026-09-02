#pragma once

#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "fsm/backend/cpp/runtime/async_types.hpp"
#include "fsm/backend/cpp/runtime/detail/notification_dispatcher.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"

namespace fsm::detail {

/**
 * @brief Manages thread-safe registration, storage, and snapshotting of diagnostic callbacks.
 */
class diagnostic_handler_manager {
  public:
    using observer_fn = std::function<void(const transition_info&)>;

    void set_unhandled_handler(unhandled_handler h) { unhandled_handler_ = std::move(h); }
    void set_guard_rejected_handler(guard_rejected_handler h) { guard_rejected_handler_ = std::move(h); }
    void set_deferred_handler(deferred_handler h) { deferred_handler_ = std::move(h); }
    void set_dispatch_failure_handler(dispatch_failure_handler h) { failure_handler_ = std::move(h); }
    void set_exception_handler(exception_handler h) { exception_handler_ = std::move(h); }
    void set_user_observer(observer_fn obs) { user_observer_ = std::move(obs); }
    void clear_user_observer() { user_observer_ = nullptr; }

    [[nodiscard]] exception_handler get_exception_handler_copy() const { return exception_handler_; }

    [[nodiscard]] std::exception_ptr last_exception() const noexcept { return last_exception_; }
    void set_last_exception(std::exception_ptr ep) noexcept { last_exception_ = ep; }
    void clear_last_exception() noexcept { last_exception_ = nullptr; }

    std::vector<transition_info>& notification_buffer() noexcept { return notification_buffer_; }
    const std::vector<transition_info>& notification_buffer() const noexcept { return notification_buffer_; }

    void populate_snapshot_handlers(dispatch_snapshot& snap) const {
        snap.unhandled_h = unhandled_handler_;
        snap.guard_rejected_h = guard_rejected_handler_;
        snap.deferred_h = deferred_handler_;
        snap.failure_h = failure_handler_;
        snap.exception_h = exception_handler_;
        snap.observer_h = user_observer_;
    }

  private:
    observer_fn user_observer_{nullptr};
    std::vector<transition_info> notification_buffer_;
    unhandled_handler unhandled_handler_{nullptr};
    guard_rejected_handler guard_rejected_handler_{nullptr};
    deferred_handler deferred_handler_{nullptr};
    dispatch_failure_handler failure_handler_{nullptr};
    exception_handler exception_handler_{nullptr};
    std::exception_ptr last_exception_{nullptr};
};

}  // namespace fsm::detail
