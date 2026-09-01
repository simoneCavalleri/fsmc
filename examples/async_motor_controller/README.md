# Asynchronous Industrial Motor Controller Showcase

This showcase demonstrates a high-performance **3-Phase BLDC / PMSM Motor Controller & Inverter State Machine** modeled and compiled with **`fsmc`**.

It highlights **Active Object multi-threading (`thread_safe_fsm`)**, asynchronous transition futures (`post_async`), timed ramp-up profiles (`post_delayed`), and hardware thermal/current safety interlocks.

---

## What This Example Demonstrates

1. **Active Object Architecture (`fsm::thread_safe_fsm`)**:
   - The state machine runs inside a dedicated background worker thread with a thread-safe priority event queue.
   - External producer threads push command events without blocking.
2. **Asynchronous Dispatch with `std::future<dispatch_result>` (`post_async`)**:
   - Caller threads can await transition results asynchronously (`future.get()`) to verify that hardware relays closed successfully.
3. **Timed Transition Scheduling (`post_delayed`)**:
   - Models physical motor dynamics (e.g. 50ms acceleration ramp-up, braking deceleration delays) without blocking worker threads.
4. **Hardware Safety Guards & Thermal Interlocks**:
   - `HasValidRpmGuard`: Restricts target speed commands to the safe range ($0 < \text{RPM} \le 8000$).
   - `IsThermalSafeGuard`: Rejects energizing gate drivers if inverter temperature exceeds $75.0^\circ\text{C}$.
5. **Emergency Fault & Electronic Braking Recovery**:
   - Immediate transition to `Fault` upon overcurrent or overtemperature trips.
   - Controlled reset sequence via `FaultCleared` and `BrakeDisengaged`.

---

## Execution Phases in `main.cpp`

```
[Phase 1] Standby State & Initial Hardware Telemetry Check (Off / Standby)
    │
[Phase 2] Power-On Gate Drivers & Inverter Stage (StartCmd -> EnergizingInverter)
    │
[Phase 3] Closed-Loop Speed Ramp-Up via Scheduled Timers (RampingSpeed -> TargetRpmReached -> RunningAtSpeed)
    │
[Phase 4] Dynamic Speed Setpoint Adjustment During Active Rotation (TargetRpmUpdateCmd)
    │
[Phase 5] Electronic Braking & Controlled Deceleration (BrakeCmd -> ActiveBraking -> MotorStopped -> Standby)
    │
[Phase 6] Overcurrent Trip & Hardware Fault Interlock (OvercurrentFault -> Fault -> FaultReset)
```

---

## Running the Example

```bash
# Run via CMake build binary
./build/bin/async_motor_controller_example
```
