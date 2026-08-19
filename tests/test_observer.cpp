#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"

namespace {

// States & Events
struct Idle { static constexpr std::string_view name = "Idle"; };
struct Active { static constexpr std::string_view name = "Active"; };
struct Paused { static constexpr std::string_view name = "Paused"; };

struct StartEvent { static constexpr std::string_view name = "StartEvent"; };
struct PauseEvent { static constexpr std::string_view name = "PauseEvent"; };
struct ResumeEvent { static constexpr std::string_view name = "ResumeEvent"; };
struct PingEvent { static constexpr std::string_view name = "PingEvent"; };
struct StopEvent { static constexpr std::string_view name = "StopEvent"; };

struct PingAction {
    void operator()(const PingEvent& /*evt*/, auto& /*state*/, auto& /*ctx*/) const {
        // internal ping
    }
};

using ObserverTable = fsm::transition_table<
    fsm::row<Idle, StartEvent, Active>,
    fsm::row<Active, PauseEvent, Paused>,
    fsm::row<Paused, ResumeEvent, Active>,
    fsm::internal_row<Active, PingEvent, fsm::no_guard, PingAction>,
    fsm::row<Active, StopEvent, Idle>
>;

void test_sync_fsm_observer() {
    std::cout << "[TEST] Synchronous FSM Observer Hooks...\n";

    fsm::fsm<ObserverTable, fsm::no_context, Idle> machine;

    std::vector<fsm::transition_info> transitions;
    machine.set_observer([&](const fsm::transition_info& info) {
        transitions.push_back(info);
    });

    assert(machine.is_in_state<Idle>());

    // 1. Idle -> Active
    assert(machine.dispatch(StartEvent{}));
    assert(transitions.size() == 1);
    assert(transitions.back().source == "Idle");
    assert(transitions.back().target == "Active");
    assert(transitions.back().is_internal == false);

    // 2. Internal ping in Active
    assert(machine.dispatch(PingEvent{}));
    assert(transitions.size() == 2);
    assert(transitions.back().source == "Active");
    assert(transitions.back().target == "Active");
    assert(transitions.back().is_internal == true);

    // 3. Active -> Paused
    assert(machine.dispatch(PauseEvent{}));
    assert(transitions.size() == 3);
    assert(transitions.back().source == "Active");
    assert(transitions.back().target == "Paused");
    assert(transitions.back().is_internal == false);

    // 4. Paused -> Active
    assert(machine.dispatch(ResumeEvent{}));
    assert(transitions.size() == 4);
    assert(transitions.back().source == "Paused");
    assert(transitions.back().target == "Active");

    // 5. Active -> Idle
    assert(machine.dispatch(StopEvent{}));
    assert(transitions.size() == 5);
    assert(transitions.back().source == "Active");
    assert(transitions.back().target == "Idle");

    std::cout << "  -> Synchronous observer tests passed.\n";
}

void test_thread_safe_fsm_observer() {
    std::cout << "[TEST] Thread-Safe Asynchronous FSM Observer Hooks...\n";

    fsm::thread_safe_fsm<ObserverTable, fsm::no_context, Idle> ts_machine;

    std::vector<fsm::transition_info> async_transitions;
    std::mutex tr_mutex;

    ts_machine.set_observer([&](const fsm::transition_info& info) {
        std::scoped_lock lock(tr_mutex);
        async_transitions.push_back(info);
    });

    ts_machine.start_worker();

    ts_machine.post(StartEvent{});
    ts_machine.post(PingEvent{});
    ts_machine.post(PauseEvent{});
    ts_machine.post(ResumeEvent{});
    ts_machine.post(StopEvent{});

    // Wait for queue drain
    while (!ts_machine.is_queue_empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ts_machine.stop_worker();

    {
        std::scoped_lock lock(tr_mutex);
        assert(async_transitions.size() == 5);
        assert(async_transitions[0].source == "Idle" && async_transitions[0].target == "Active");
        assert(async_transitions[1].source == "Active" && async_transitions[1].is_internal == true);
        assert(async_transitions[2].source == "Active" && async_transitions[2].target == "Paused");
        assert(async_transitions[3].source == "Paused" && async_transitions[3].target == "Active");
        assert(async_transitions[4].source == "Active" && async_transitions[4].target == "Idle");
    }

    assert(ts_machine.is_in_state<Idle>());

    std::cout << "  -> Thread-safe observer tests passed.\n";
}

}  // namespace

int main() {
    std::cout << "=== Running Observer / Tracing Test Suite ===\n";
    test_sync_fsm_observer();
    test_thread_safe_fsm_observer();
    std::cout << "=== All Observer Tests Passed! ===\n";
    return 0;
}
