#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace fsm {

// Diagnostic & Failure Handling Callbacks
using unhandled_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
using guard_rejected_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
using deferred_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
using dispatch_failure_handler =
    std::function<void(std::string_view /*event*/, std::string_view /*state*/, dispatch_status /*status*/)>;
using exception_handler = std::function<void(std::exception_ptr)>;

// Represents a timed / delayed event task in the priority queue
template <typename HandlerType>
struct timed_event {
    std::chrono::steady_clock::time_point deadline;
    HandlerType task;

    bool operator>(const timed_event& other) const noexcept { return deadline > other.deadline; }
};

}  // namespace fsm
