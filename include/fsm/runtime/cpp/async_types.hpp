#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/runtime/cpp/type_traits.hpp"

namespace fsm {

// Handler callback types for lifecycle, failures, and unhandled conditions
using unhandled_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
using guard_rejected_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
using deferred_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
using dispatch_failure_handler =
    std::function<void(std::string_view /*event*/, std::string_view /*state*/, dispatch_status /*status*/)>;
using exception_handler = std::function<void(std::exception_ptr)>;

// Snapshot taken under lock to invoke notifications/observers outside lock
struct dispatch_snapshot {
    dispatch_result result{dispatch_status::unhandled};
    std::string state_name{};
    std::vector<transition_info> notifications{};
    unhandled_handler unhandled_h{};
    guard_rejected_handler guard_rejected_h{};
    deferred_handler deferred_h{};
    dispatch_failure_handler failure_h{};
    exception_handler exception_h{};
    std::function<void(const transition_info&)> observer_h{};
};

// Represents a timed / delayed event task in the priority queue
template <typename HandlerType>
struct timed_event {
    std::chrono::steady_clock::time_point deadline;
    HandlerType task;

    bool operator>(const timed_event& other) const noexcept { return deadline > other.deadline; }
};

}  // namespace fsm
