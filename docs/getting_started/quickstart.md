# Quickstart: Your First State Machine

This guide demonstrates defining, compiling, and running a state machine in C++ in under 5 minutes.

---

## 1. Define the Statechart Model

Create a state machine in any supported format (e.g. SysML v2 or PlantUML):

=== "battery.sysml (SysML v2)"
    ```sysml
    package BatteryManagement {
        state def BatteryStatechart {
            attribute batterySoC : Real = 100.0;
            attribute isCharging : Boolean = false;

            entry; then state Discharging;

            state Discharging {
                transition on StartCharging do isCharging = true; to Charging;
                transition on BatteryCritical if batterySoC < 15.0 to LowPower;
            }

            state Charging {
                transition on StopCharging do isCharging = false; to Discharging;
            }

            state LowPower;
        }
    }
    ```

=== "battery.puml (PlantUML)"
    ```plantuml
    @startuml
    [*] --> Discharging
    Discharging --> Charging : StartCharging
    Charging --> Discharging : StopCharging
    Discharging --> LowPower : BatteryCritical [batterySoC < 15.0]
    @enduml
    ```

---

## 2. Compile to Modern C++20 Header

Run the compiler driver `fsmc`:

```bash
fsmc -i battery.sysml -o battery_fsm.hpp --std 20 --namespace bms --name BatteryFSM
```

---

## 3. Instantiate and Dispatch Events in Application

=== "Synchronous (Zero-Heap)"
    ```cpp
    #include "battery_fsm.hpp"
    #include <iostream>

    int main() {
        bms::BatteryFSMContext ctx;
        ctx.batterySoC = 12.0;

        bms::BatteryFSM fsm(ctx);

        std::cout << "Initial State: " << fsm.current_state_name() << "\n";

        // Dispatch transition with guard evaluation
        auto result = fsm.dispatch(bms::BatteryCritical{});
        if (result.is_success()) {
            std::cout << "Transitioned to: " << fsm.current_state_name() << "\n";
        }

        return 0;
    }
    ```

=== "Lock-Free SPSC (ISR-Safe)"
    ```cpp
    #include "battery_fsm.hpp"

    bms::BatteryFSMContext ctx;
    bms::SpscBatteryFSM spsc_fsm(ctx);

    // Producer (e.g. Interrupt Service Routine)
    void EXTI0_IRQHandler() {
        spsc_fsm.enqueue(bms::BatteryCritical{}); // Wait-Free O(1)
    }

    // Consumer (e.g. Control Task)
    void ControlTask() {
        spsc_fsm.run_until_empty();
    }
    ```
