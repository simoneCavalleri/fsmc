# Policy-Based FSM Configuration

Starting with **`v0.5.0`**, `fsmc` introduces a modern, expressive, and zero-boilerplate **Policy-Based Configuration** system.

In traditional C++ template libraries, configuring complex engines often results in "template parameter explosion", forcing developers to provide long lists of positional template arguments:

```cpp
// [Legacy Positional Syntax] (up to 8 positional parameters)
using MyFSM = fsm::fsm<
    MyTable, 
    fsm::no_ports, 
    fsm::no_ports, 
    MyRegisters, 
    fsm::no_services, 
    typename MyTable::initial_state, 
    fsm::no_observer, 
    16
>;
```

With `fsm::config` and semantic modifiers, you specify **only what you need**, in **any order**, with zero runtime or space overhead:

```cpp
// [Modern Policy-Based Syntax] (v0.5.0+)
using MyFSM = fsm::make_fsm<MyTable, fsm::with_registers<MyRegisters>>;
```

---

## 1. Semantic Policy Modifiers

`fsmc` provides a set of lightweight modifier tags in the `fsm::` namespace:

| Policy Modifier | Purpose | Default if Omitted |
| :--- | :--- | :--- |
| `fsm::with_registers<T>` | Binds internal discrete datapath state variables | `fsm::no_registers` |
| `fsm::with_ports<In, Out>` | Binds continuous input and output hardware port structs | `fsm::no_ports`, `fsm::no_ports` |
| `fsm::with_services<Srv>` | Binds external OS/hardware interface services (drivers, timers) | `fsm::no_services` |
| `fsm::with_observer<Obs>` | Configures compile-time telemetry and transition hooks | `fsm::no_observer` |
| `fsm::with_trace_buffer<N>` | Configures zero-allocation circular flight recorder with capacity $N$ | None (or default observer) |
| `fsm::with_timer_capacity<N>` | Configures capacity $N$ for deterministic synchronous timer manager | `0` (timers disabled) |
| `fsm::with_initial_state<State>` | Overrides the table's default initial state | `Table::initial_state` |
| `fsm::with_deferred_capacity<N>` | Configures static capacity of deferred event queues | `16` |
| `fsm::with_queue_capacity<N>` | Configures ring buffer capacity in SPSC/async engines | `64` |

---

## 2. Order Invariance

Policies are order-independent. The metaprogramming reflection engine extracts and maps each type domain to its respective engine interface regardless of how modifiers are sequenced:

```cpp
// Both configurations produce identical compile-time types:
using ConfigA = fsm::config<
    FlightTable,
    fsm::with_registers<FlightRegisters>,
    fsm::with_ports<SensorIn, ActuatorOut>,
    fsm::with_services<HardwareDrivers>,
    fsm::with_deferred_capacity<32>
>;

using ConfigB = fsm::config<
    FlightTable,
    fsm::with_deferred_capacity<32>,
    fsm::with_services<HardwareDrivers>,
    fsm::with_registers<FlightRegisters>,
    fsm::with_ports<SensorIn, ActuatorOut>
>;

static_assert(std::is_same_v<ConfigA::registers_type, ConfigB::registers_type>);
static_assert(std::is_same_v<ConfigA::services_type, ConfigB::services_type>);
```

---

## 3. Factory Aliases

`fsmc` provides three dedicated factory aliases corresponding to the three execution models:

### A. Synchronous Deterministic Engine (`make_fsm`)
For hard real-time single-threaded control loops:
```cpp
#include "fsm/fsm.hpp"

// Single-parameter instantiation (stateless FSM)
fsm::make_fsm<SimpleTable> machine;

// Configured FSM with internal registers
FlightRegisters regs{100.0, 0.0};
fsm::make_fsm<FlightTable, fsm::with_registers<FlightRegisters>> machine(regs);

// Configured FSM with Blackbox Flight Recorder and Deterministic Timers (v0.6.0+)
using SafeFlightFsm = fsm::make_fsm<
    FlightTable,
    fsm::with_registers<FlightRegisters>,
    fsm::with_trace_buffer<64>,
    fsm::with_timer_capacity<8>
>;
SafeFlightFsm flight_sm(regs);
```

### B. Lock-Free SPSC Ring Buffer Engine (`make_spsc_fsm`)
For zero-overhead interrupt service routines (ISR) and sensor tasks:
```cpp
#include "fsm/spsc_fsm.hpp"

fsm::make_spsc_fsm<
    SensorTable,
    fsm::with_registers<SensorRegisters>,
    fsm::with_queue_capacity<256>
> spsc_machine(regs);
```

### C. Thread-Safe Worker Engine (`make_thread_safe_fsm`)
For multi-threaded event dispatching with background executor loops:
```cpp
#include "fsm/thread_safe_fsm.hpp"

fsm::make_thread_safe_fsm<
    NetworkTable,
    fsm::with_registers<NetworkRegisters>,
    fsm::with_services<SocketManager>
> async_machine(regs, sockets);
```

---

## 4. Migration Guide (from v0.4.x to v0.5.0)

### Synchronous Engine Migration
```diff
- fsm::fsm<MyTable, fsm::no_ports, fsm::no_ports, MyRegs> fsm(regs);
+ fsm::make_fsm<MyTable, fsm::with_registers<MyRegs>> fsm(regs);
```

### SPSC Lock-Free Engine Migration
```diff
- fsm::spsc_fsm<MyTable, fsm::no_ports, fsm::no_ports, MyRegs, fsm::no_services, 128> fsm(regs);
+ fsm::make_spsc_fsm<MyTable, fsm::with_registers<MyRegs>, fsm::with_queue_capacity<128>> fsm(regs);
```

### Thread-Safe Async Engine Migration
```diff
- fsm::thread_safe_fsm<MyTable, fsm::no_ports, fsm::no_ports, MyRegs> fsm(regs);
+ fsm::make_thread_safe_fsm<MyTable, fsm::with_registers<MyRegs>> fsm(regs);
```

> [!TIP]
> **Backward Compatibility**: The legacy positional syntax `fsm<Table, In, Out, Reg, Srv...>` remains fully supported for backward compatibility, ensuring existing codebases continue to compile without disruption.
