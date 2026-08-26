# EFSM Data Path Interval Analysis

The `EFSMIntervalAnalyzer` performs forward abstract interpretation over numerical variable ranges across the state machine.

---

## Capabilities

- **Dead Branch Detection**: If a guard condition like `batterySoC > 100.0` is unreachable given the current variable interval `[0.0, 80.0]`, the compiler emits `W_EFSM_UNSATISFIABLE_GUARD`.
- **Assignment Propagation**: Tracks updates (`v += k`, `v = clamp(v, min, max)`) through transitions and joins intervals at target states until reaching a fixed point.
