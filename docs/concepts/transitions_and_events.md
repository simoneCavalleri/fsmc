# Transitions and Event Triggers

Transitions define the valid paths of state progression in response to incoming events, timeout expirations, or immediate completion triggers.

---

## Trigger Categories

In `fsmc`, transitions are triggered by four distinct classes of triggers:

### 1. Signal Triggers (External Events)
Signal triggers represent discrete external stimuli, sensor interrupts, or software commands passed to the state machine. Signals can carry typed payload parameters (e.g. `TemperatureReading(sensor_id: Integer, val: Real)`).

```sysml
// Signal event definition with typed payload
event def SensorThresholdExceeded {
    attribute sensor_id : Integer;
    attribute reading_celsius : Real;
}
```

### 2. Timed Transitions (`after` and `every`)
Timed triggers model real-time timeouts and periodic ticks:

- **Relative Timeout (`after`)**: Single-shot timer that begins counting when the source state is entered. If an external transition exits the state before the duration expires, the timer is automatically disarmed.
- **Periodic Trigger (`every`)**: Periodic heartbeat trigger that fires repeatedly at fixed intervals as long as the source state remains active.

`fsmc` middle-end passes support formal verification of timed transitions (via Timed SMV / nuXmv). Execution targets can map timed transitions to hardware tick counters (SysTick) or RTOS timer queues.

### 3. Anonymous / Immediate Transitions
Anonymous transitions have no event trigger. They fire automatically when the source state is entered and its entry actions complete (or when an incoming transition targeting the state evaluates to true).

### 4. Deferred Events
When a state machine receives an event that cannot be processed in its current state but must not be dropped, the event can be deferred:

```sysml
state Calibrating {
    @fsm:deferred StartMissionCmd;
    transition on CalibrationFinished to Ready;
}
```

While in `Calibrating`, any `StartMissionCmd` event is saved in a static ring buffer. Upon transitioning into `Ready`, `StartMissionCmd` is automatically recalled and dispatched.

---

## Transition Execution Order (Run-to-Completion)

When a transition fires, `fsmc` executes actions according to UML / SysML run-to-completion semantics:

1. **Guard Evaluation**: All candidate guards matching the current state and trigger are evaluated. If a guard returns `false`, the transition is rejected.
2. **Exit Actions**: The active state's `on_exit` action is executed (ascending from the leaf substate up to the Least Common Ancestor).
3. **Transition Effect**: The transition's action functor is executed (mutating `OutPorts`, updating `Registers`, or invoking `Services`).
4. **Target State Entry**: The target state's `on_entry` action is executed (descending from the Least Common Ancestor down to the target leaf state).
5. **History Update**: Internal history tags are updated with the newly active substate configuration.

---

## Determinism and Collision Resolution

If multiple outgoing transitions from the same state match the same event trigger, a non-deterministic collision occurs unless disambiguated:

- **Priority Rules**: Higher priority values fire first.
- **Guard Exclusivity**: If branches have mutually exclusive guards (e.g. `[x > 0]` and `[x <= 0]`), only the valid branch fires.
- **Strict Mode (`--strict-determinism`)**: When enabled, the compiler flags any overlapping triggers with non-disjoint guards as compilation errors.
