# `fsm::thread_safe_fsm` (Thread-Safe MPSC Engine)

An asynchronous MPSC wrapper with an internal background worker thread.

---

## API Summary

```cpp
#include "fsm/runtime/cpp/thread_safe_fsm.hpp"

fsm::thread_safe_fsm<TransitionTable, SystemContext> ts_fsm(ctx);

// Asynchronously post event
ts_fsm.post(StartEvent{});

// Wait on asynchronous transition completion
std::future<fsm::dispatch_result> fut = ts_fsm.post_async(RunEvent{});
auto res = fut.get();
```
