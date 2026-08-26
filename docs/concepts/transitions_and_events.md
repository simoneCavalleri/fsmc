# Transitions & Event Triggers

Transitions define the valid paths between states in response to event triggers, conditions, and timeouts.

---

## Trigger Kinds

1. **Signal Triggers**:
   Events triggered by explicit asynchronous or synchronous payloads (e.g. `SensorPayload`, `BatteryCritical`).
2. **Timed Transitions**:
   - `after_<N>ms` / `after(duration)`: Single-shot timeout firing relative to the time the source state was entered.
   - `every_<N>ms` / `every(period)`: Periodic heartbeat trigger.
3. **Anonymous / Immediate Triggers**:
   Fires immediately upon state entry without waiting for external signals (e.g. following completion of entry actions).
4. **Deferred Events**:
   Events marked with `@fsm:deferred` are queued and postponed while the FSM is in specific states, firing automatically upon transition to a receiving state.
