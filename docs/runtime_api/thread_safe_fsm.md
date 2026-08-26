# `fsm::thread_safe_fsm` (Thread-Safe MPSC Engine)

`fsm::thread_safe_fsm<TransitionTable, Context, InitialState>` is an asynchronous Multi-Producer Single-Consumer (MPSC) wrapper designed for general-purpose C++ multi-threaded applications, services, and game engines.

---

## Class Template Synopsis

```cpp
namespace fsm {

template <
    typename Table,
    typename Context = no_context,
    typename InitialState = typename Table::initial_state
>
class thread_safe_fsm {
public:
    explicit thread_safe_fsm(Context ctx = Context{});
    ~thread_safe_fsm();

    // Asynchronous Event Enqueue (Fire and Forget)
    template <typename Event>
    void post(Event&& event);

    // Asynchronous Event Dispatch with std::future Result
    template <typename Event>
    std::future<dispatch_result> post_async(Event&& event);

    // Synchronous Event Dispatch (Blocks calling thread)
    template <typename Event>
    dispatch_result dispatch_sync(const Event& event);

    // State Inspection (Thread-Safe)
    [[nodiscard]] std::string_view current_state_name() const;

    // Context Access
    template <typename Func>
    void with_context(Func&& fn);
};

} // namespace fsm
```

---

## Asynchronous Execution Architecture

```mermaid
flowchart LR
    Thread1["Producer Thread 1"] -->|post()| Queue["MPSC Event Queue (std::mutex + cv)"]
    Thread2["Producer Thread 2"] -->|post_async()| Queue
    Queue --> Worker["Background Worker Thread"]
    Worker --> FSM["fsm::fsm Core Instance"]
```

1. **Multiple Producers**: Any number of threads can safely call `post()` or `post_async()`.
2. **Background Execution**: A dedicated background thread dequeues events and executes transitions sequentially.
3. **`std::future` Integration**: Callers can await transition results (e.g. to inspect whether the guard rejected the transition or which state was reached).

---

## Example Usage

```cpp
#include "uav_mission_fsm.hpp"
#include <iostream>

int main() {
    avionics::UavMissionFSMContext ctx;
    avionics::ThreadSafeUavMissionFSM fsm(ctx);

    // Fire and forget from worker thread 1
    fsm.post(avionics::CalibrationOk{});

    // Post and await result from thread 2
    std::future<fsm::dispatch_result> future_res = fsm.post_async(avionics::TakeoffCmd{});
    fsm::dispatch_result res = future_res.get();

    if (res.is_success()) {
        std::cout << "Takeoff completed asynchronously. State: " 
                  << fsm.current_state_name() << "\n";
    }

    return 0;
}
```
