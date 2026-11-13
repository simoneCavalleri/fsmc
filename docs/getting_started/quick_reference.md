# Developer Quick Reference (Cheat Sheet)

A single-page reference covering the universal `fsmc` / `fsm-opt` CLI workflow, verification commands, C++ transition table DSL, and runtime execution engines.

!!! note "Workflow Modes"
    You can either generate code directly from SysML v2 / Stateflow / SCXML / diagram models via **`fsmc`**, or write transition tables directly in C++ using the header-only **`<fsm/fsm.hpp>`** runtime library. For planned Rust and C backends, see the [Multi-Target Roadmap](../runtime_api/multi_target_roadmap.md).

---

## 1. Command-Line Interface & Formal Verification

```bash
# Code Generation (C++20 Standalone Header)
fsmc -i flight.sysml -o flight_fsm.hpp --target cpp --std 20 --standalone

# Formal Verification (Invariants, Deadlocks, Unreachable States)
fsmc -i flight.sysml --verify

# Ad-hoc Temporal Logic Verification (LTL & CTL)
fsmc -i flight.sysml --verify --ltl "G (LowBattery -> F Landed)"
fsmc -i flight.sysml --verify --ctl "AG (EF Standby)"

# Model Transpilation (SysML v2 -> Stateflow / SCXML / Mermaid / PlantUML / SMV)
fsmc -i flight.sysml -e stateflow -o flight_sf.xml
fsmc -i flight.sysml -e scxml -o flight.scxml
fsmc -i flight.sysml -e mermaid -o flight.mmd
fsmc -i flight.sysml -e smv -o flight.smv

# Requirement Traceability Matrix (RTM) & MC/DC Test Harness
fsmc -i flight.sysml --req-audit --rtm rtm.md --harness flight_test.cpp
```

---

## 2. Defining Transition Tables (C++ Runtime DSL)

```cpp
// Method A: fsm::row Type Declarations
using MyTable = fsm::transition_table<
    fsm::row<Idle, EvStart, Running>,
    fsm::row<Running, EvSpeedUp, Running>::when<SpeedLimitGuard>::then<AccelerateAction>,
    fsm::row<Running, fsm::anonymous_event, EmergencyStop>::when<OverheatGuard>,
    fsm::row<Fault, EvRecover, fsm::history<Operational, Standby>>
>;

// Method B: fsm::on Fluent Builder
using MyTable = fsm::transition_table<
    decltype(fsm::on<EvStart>().from<Idle>().to<Running>()),
    decltype(fsm::on<EvStop>().from<Running>().to<Idle>().action<StopMotors>())
>;

// Guard Combinators
using SafeToArm = fsm::and_<BatteryOkGuard, fsm::not_<CoverOpenGuard>>;
using DwellGuard = fsm::in_state_for<10>; // Active for 10 periodic step() ticks
```

---

## 3. Runtime Engine Instantiation & Policy Configuration

```cpp
#include <fsm/fsm.hpp> // Unified runtime umbrella (or "fsm.hpp" via --export-runtime)

// 1. Synchronous Control Loop (Zero-Heap, O(1) WCET, Flight Recorder & Timers)
using SyncFSM = fsm::make_fsm<
    MyTable,
    fsm::with_ports<InPorts, OutPorts>,
    fsm::with_registers<Registers>,
    fsm::with_services<Services>,
    fsm::with_trace_buffer<64>,
    fsm::with_timer_capacity<8>
>;

// 2. Lock-Free SPSC Engine (Wait-Free ISR Ingress)
using SpscFSM = fsm::make_spsc_fsm<
    MyTable,
    fsm::with_registers<Registers>,
    fsm::with_queue_capacity<128>
>;

// 3. Multi-Threaded Active Object (std::future & Chrono Timers)
using AsyncFSM = fsm::make_thread_safe_fsm<
    MyTable,
    fsm::with_registers<Registers>,
    fsm::with_services<Services>,
    fsm::with_queue_capacity<256>
>;
```

---

## 4. The 4-Domain Datapath Signatures

`fsmc` strictly segregates memory into 4 orthogonal domains across all backends:

| Domain | Mutability in Guards | Mutability in Actions | Bound At |
| :--- | :--- | :--- | :--- |
| **`InPorts`** | Read-Only | Read-Only | Call-site per cycle (`dispatch(ev, in, out)`) |
| **`OutPorts`** | Inaccessible | Read-Write | Call-site per cycle (`dispatch(ev, in, out)`) |
| **`Registers`** | Read-Only | Read-Write | Machine construction |
| **`Services`** | Inaccessible | Injected Reference | Machine construction |

```cpp
// Guard: Read-only access to InPorts, Registers, and Event payload
struct TargetReachableGuard {
    bool operator()(const InPorts& in, const Registers& reg, const MoveCmd& ev) const noexcept {
        return (in.gps_lock && ev.altitude <= reg.max_altitude);
    }
};

// Action: Read-write OutPorts and Registers, injected Services
struct FireMotorsAction {
    void operator()(OutPorts& out, Registers& reg, Services& srv) const {
        out.pwm_duty = 0.85f;
        reg.ignition_count++;
        srv.hardware_timer.start();
    }
};
```

---

## 5. Execution API Cheat Sheet

```cpp
SyncFSM fsm(initial_regs, srv);

// 1. Reactive event dispatch
fsm::dispatch_result res = fsm.dispatch(EvStart{}, in, out);
if (res.is_success()) { /* Transition succeeded */ }

// 2. Sampled periodic control loop step (IEC 61131-3)
fsm::step_result step_res = fsm.step(in, out);
if (step_res.has_transitioned()) { /* Sampled threshold fired */ }

// 3. Deterministic Real-Time Timer Tick
std::size_t expired = fsm.tick(std::chrono::milliseconds(10));

// 4. Introspection & Blackbox Flight Recorder
assert(fsm.is_in<Running>());
std::string_view current = fsm.current_state_name();
uint32_t count = fsm.registers().ignition_count;
std::size_t trace_count = fsm.observer().recorder().size();

// 5. Lock-Free SPSC / Thread-Safe Engines
spsc.post(SensorDataEvent{raw_adc});
Registers snap = spsc.snapshot_registers(); // Seqlock atomic copy

// 6. Zero-Heap Binary Snapshot Serialization
std::array<std::uint8_t, 256> snapshot_buf{};
std::size_t written = 0;
fsm.serialize(snapshot_buf, written);
fsm.deserialize(snapshot_buf);
```
