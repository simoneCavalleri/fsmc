# `fsm::fsm` (Synchronous Core Engine)

The foundational zero-overhead compile-time FSM engine.

---

## API Summary

```cpp
#include "fsm/runtime/cpp/fsm.hpp"

// Instantiate with context reference
MyFSMContext ctx;
MyFSM fsm(ctx);

// Dispatch typed event synchronously
auto result = fsm.dispatch(EvStart{});

if (result.is_success()) {
    std::cout << "Current State: " << fsm.current_state_name() << "\n";
}
```
