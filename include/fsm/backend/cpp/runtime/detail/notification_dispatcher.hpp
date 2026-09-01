#pragma once

#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/cpp/runtime/async_types.hpp"
#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"

namespace fsm::detail {

struct dispatch_snapshot {
    dispatch_result result;
    std::string state_name;
    std::vector<transition_info> notifications;
    unhandled_handler unhandled_h;
    guard_rejected_handler guard_rejected_h;
    deferred_handler deferred_h;
    dispatch_failure_handler failure_h;
    exception_handler exception_h;
    std::function<void(const transition_info&)> observer_h;
};

inline void handle_exception_outside_lock(std::exception_ptr ex, const exception_handler& handler,
                                          std::exception_ptr& last_exception, std::mutex& mutex) {
    {
        std::scoped_lock lock(mutex);
        last_exception = ex;
    }
    if (handler) {
        try {
            handler(ex);
        } catch (...) {
            // Suppress secondary exceptions thrown from handler
        }
    }
}

template <typename Event>
inline void invoke_notifications_outside_lock(const Event& evt, const dispatch_snapshot& snap,
                                             std::exception_ptr& last_exception, std::mutex& mutex) {
    if (snap.observer_h) {
        for (const auto& info : snap.notifications) {
            try {
                snap.observer_h(info);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), snap.exception_h, last_exception, mutex);
            }
        }
    }
    if (snap.result.is_unhandled() && snap.unhandled_h) {
        try {
            snap.unhandled_h(get_event_name(evt), snap.state_name);
        } catch (...) {
            handle_exception_outside_lock(std::current_exception(), snap.exception_h, last_exception, mutex);
        }
    } else if (snap.result.is_guard_rejected() && snap.guard_rejected_h) {
        try {
            snap.guard_rejected_h(get_event_name(evt), snap.state_name);
        } catch (...) {
            handle_exception_outside_lock(std::current_exception(), snap.exception_h, last_exception, mutex);
        }
    } else if (snap.result.is_deferred() && snap.deferred_h) {
        try {
            snap.deferred_h(get_event_name(evt), snap.state_name);
        } catch (...) {
            handle_exception_outside_lock(std::current_exception(), snap.exception_h, last_exception, mutex);
        }
    }
    if (!snap.result.is_ok() && snap.failure_h) {
        try {
            snap.failure_h(get_event_name(evt), snap.state_name, snap.result.status);
        } catch (...) {
            handle_exception_outside_lock(std::current_exception(), snap.exception_h, last_exception, mutex);
        }
    }
}

}  // namespace fsm::detail
