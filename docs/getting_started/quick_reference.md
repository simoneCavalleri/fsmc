# Developer Quick Reference (Cheat Sheet)

A single-page printable reference card covering the universal CLI commands, formal verification syntax, and target runtime APIs across supported languages.

> [!NOTE]
> **Active Target vs. Roadmap Previews**: The **C++ Backend** (`C++17/C++20`) is currently the sole active production runtime in `v0.5.0`. Rust and C tabs illustrate **preview specifications** currently in active development under the multi-target roadmap.

---

## 1. Command-Line Interface & Formal Verification (Universal)

The `fsmc` CLI works across all model formats and target code generators:

```bash
# Code Generation (C++ Standalone Header)
fsmc -i flight.sysml -o flight_fsm.hpp --target cpp --std 20

# Code Generation (Rust no_std / Embedded C)
fsmc -i flight.sysml -o flight_fsm.rs --target rust
fsmc -i flight.sysml -o flight_fsm.h  --target c

# Formal Verification (Invariants, Deadlocks, Unreachable States)
fsmc verify flight.sysml

# Ad-hoc Temporal Logic Verification (LTL & CTL)
fsmc verify flight.sysml --ltl "G (State == LowBattery -> F State == Landed)"
fsmc verify flight.sysml --ctl "AG (EF State == Standby)"

# Model Transpilation (SysML v2 -> SCXML / Mermaid / PlantUML)
fsmc -i flight.sysml -e scxml -o flight.scxml
fsmc -i flight.sysml -e mermaid -o flight.mmd

# Requirement Traceability Matrix (RTM) Audit Report
fsmc -i flight.sysml --req-audit --rtm-output rtm.md
```

---

## 2. Defining Transition Tables

=== "C++ Target (Production v0.5.0)"
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

=== "Rust Target (Roadmap Preview)"
    ```rust
    // Declarative transition table macro (no_std)
    transition_table! {
        FlightFsm {
            Idle + EvStart => Running,
            Running + EvSpeedUp [SpeedLimitGuard] / AccelerateAction => Running,
            Running + Anonymous [OverheatGuard] => EmergencyStop,
            Fault + EvRecover => History::<Operational, Standby>,
        }
    }
    ```

=== "C Target (Embedded C Roadmap)"
    ```c
    /* Static constant transition table in ROM (zero heap) */
    static const fsm_transition_t FLIGHT_TRANSITIONS[] = {
        { STATE_IDLE,    EV_START,    STATE_RUNNING,   NULL,              NULL },
        { STATE_RUNNING, EV_SPEED_UP, STATE_RUNNING,   guard_speed_limit, action_accelerate },
        { STATE_RUNNING, EV_ANON,     STATE_ESTOP,     guard_overheat,    NULL },
        { STATE_FAULT,   EV_RECOVER,  STATE_OPER_HIST, NULL,              NULL }
    };
    ```

---

## 3. Runtime Engine Instantiation & Policy Configuration

=== "C++ Target (Production v0.5.0)"
    ```cpp
    #include "fsm/fsm.hpp"
    #include "fsm/spsc_fsm.hpp"
    #include "fsm/thread_safe_fsm.hpp"

    // 1. Synchronous Control Loop (Zero-Heap, O(1) WCET)
    using SyncFSM = fsm::make_fsm<
        MyTable,
        fsm::with_ports<InPorts, OutPorts>,
        fsm::with_registers<Registers>,
        fsm::with_services<Services>
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

=== "Rust Target (Roadmap Preview)"
    ```rust
    // 1. Synchronous State Machine (stack-allocated)
    let mut sync_fsm = FlightFsm::new(registers);

    // 2. Lock-Free SPSC Channel (heapless ring buffer)
    let (mut producer, mut consumer_fsm) = SpscFsm::new(registers);

    // 3. Thread-Safe Worker (tokio / embedded-async task)
    let async_fsm = AsyncFsm::spawn(registers, services);
    ```

=== "C Target (Embedded C Roadmap)"
    ```c
    /* 1. Synchronous instance (stack or BSS, zero dynamic allocation) */
    flight_fsm_t sync_fsm;
    flight_fsm_init(&sync_fsm, &registers, &services);

    /* 2. Lock-Free SPSC ring buffer instance */
    flight_spsc_fsm_t spsc_fsm;
    flight_spsc_init(&spsc_fsm, ring_buffer_storage, 128);
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

=== "C++ Target (Production v0.5.0)"
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

=== "Rust Target (Roadmap Preview)"
    ```rust
    // Guard: Immutable borrows (&InPorts, &Registers, &Event)
    fn target_reachable_guard(in_ports: &InPorts, reg: &Registers, ev: &MoveCmd) -> bool {
        in_ports.gps_lock && ev.altitude <= reg.max_altitude
    }

    // Action: Mutable borrows (&mut OutPorts, &mut Registers, &Services)
    fn fire_motors_action(out: &mut OutPorts, reg: &mut Registers, srv: &Services) {
        out.pwm_duty = 0.85;
        reg.ignition_count += 1;
        srv.hardware_timer.start();
    }
    ```

=== "C Target (Embedded C Roadmap)"
    ```c
    /* Guard: Const-qualified pointer inspection */
    bool guard_target_reachable(const in_ports_t* const in, const registers_t* const reg, const move_cmd_t* const ev) {
        return (in->gps_lock && (ev->altitude <= reg->max_altitude));
    }

    /* Action: Output & register mutation with service driver injection */
    void action_fire_motors(out_ports_t* const out, registers_t* const reg, const services_t* const srv) {
        out->pwm_duty = 0.85f;
        reg->ignition_count++;
        srv->timer_start();
    }
    ```

---

## 5. Execution API Cheat Sheet

=== "C++ Target (Production v0.5.0)"
    ```cpp
    SyncFSM fsm(initial_regs, srv);

    // 1. Reactive event dispatch
    fsm::dispatch_result res = fsm.dispatch(EvStart{}, in, out);
    if (res.is_success()) { /* Transition succeeded */ }

    // 2. Sampled periodic control loop step (IEC 61131-3)
    fsm::step_result step_res = fsm.step(in, out);
    if (step_res.has_transitioned()) { /* Sampled threshold fired */ }

    // 3. Introspection & Safe Datapath Access
    assert(fsm.is_in<Running>());
    std::string_view current = fsm.current_state_name();
    uint32_t count = fsm.registers().ignition_count;

    // 4. Lock-Free SPSC / Thread-Safe Engines
    spsc.post(SensorDataEvent{raw_adc});
    Registers snap = spsc.snapshot_registers(); // Seqlock atomic copy
    ```

=== "Rust Target (Roadmap Preview)"
    ```rust
    // 1. Reactive event dispatch
    let res = fsm.dispatch(&Event::Start, &in_ports, &mut out_ports);
    if res.is_transitioned() { /* Transition succeeded */ }

    // 2. Sampled periodic control loop step
    let step_res = fsm.step(&in_ports, &mut out_ports);

    // 3. Introspection
    assert_eq!(fsm.state(), State::Running);
    let count = fsm.registers().ignition_count;

    // 4. Lock-Free SPSC post
    producer.post(SensorDataEvent { raw_adc });
    let snap = consumer.snapshot_registers();
    ```

=== "C Target (Embedded C Roadmap)"
    ```c
    /* 1. Reactive event dispatch */
    fsm_result_t res = flight_fsm_dispatch(&fsm, EV_START, &in_ports, &out_ports);

    /* 2. Sampled periodic control loop step */
    fsm_result_t step_res = flight_fsm_step(&fsm, &in_ports, &out_ports);

    /* 3. Introspection */
    bool is_running = (fsm.current_state == STATE_RUNNING);
    uint32_t count = fsm.registers.ignition_count;

    /* 4. Lock-Free SPSC post */
    flight_spsc_post(&spsc_fsm, &sensor_event);
    ```
