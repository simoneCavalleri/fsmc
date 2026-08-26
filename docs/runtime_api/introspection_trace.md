# Transition Trace & Live Introspection

Every transition invocation returns rich telemetry without memory allocation.

---

## Inspecting `transition_trace`

```cpp
auto res = fsm.dispatch(EvLaunch{});

if (res.trace.has_value()) {
    std::cout << "Source State: " << res.trace->source << "\n";
    std::cout << "Target State: " << res.trace->target << "\n";
    std::cout << "Trigger Event: " << res.trace->event << "\n";
    std::cout << "Guard: " << res.trace->guard << "\n";
    std::cout << "Action: " << res.trace->action << "\n";
}
```
