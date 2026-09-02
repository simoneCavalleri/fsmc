# W3C State Chart XML (SCXML)

`fsmc` provides bidirectional parsing, validation, and C++ code generation for the **W3C State Chart XML (SCXML)** standard.

---

## 1. Supported W3C SCXML Elements

`fsmc` maps the core elements of the W3C SCXML specification into its strongly typed `FsmIr` AST:

| SCXML Element | Attributes / Children | `fsmc` Semantic Mapping |
| :--- | :--- | :--- |
| `<scxml>` | `name`, `initial`, `version="1.0"` | Root state machine model |
| `<datamodel>` | `<data id="..." expr="..."/>` | Internal memory registers (`Registers`) |
| `<state>` | `id`, `initial` | Simple atomic state or composite parent state |
| `<parallel>` | `id` | Orthogonal parallel execution regions |
| `<history>` | `id`, `type="shallow\|deep"` | History pseudostate with $z^{-1}$ memory |
| `<final>` | `id` | Final termination state |
| `<transition>` | `event`, `cond`, `target`, `delay` | Transition with event trigger, guard, target, and timed dwell |
| `<onentry>` / `<onexit>` | `<script>`, executable content | State entry and exit lifecycle actions |

---

## 2. Embedding Lossless Directives in SCXML

Because standard SCXML lacks native elements for hardware I/O port contracts or temporal LTL model checking formulas, `fsmc` allows embedding **`@fsm:*` directives** inside standard XML comments:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" name="MotorController" initial="Idle">

    <!-- @fsm:port name=bus_voltage type=float dir=in min=0.0 max=48.0 unit="[V]" -->
    <!-- @fsm:port name=pwm_duty type=float dir=out min=0.0 max=100.0 unit="[%]" -->
    <!-- @fsm:property name=SafeStop kind=Safety formula="G (EvFault -> F SafeIdle)" -->

    <datamodel>
        <data id="retry_count" expr="0"/>
    </datamodel>

    <state id="Idle">
        <transition event="EvStart" cond="in.bus_voltage > 12.0" target="Running"/>
    </state>

    <state id="Running">
        <onentry>
            <!-- Executed upon entering Running -->
            <script>out.pwm_duty = 50.0;</script>
        </onentry>
        <transition event="EvStop" target="Idle"/>
        <transition event="EvFault" target="SafeIdle"/>
    </state>

    <state id="SafeIdle"/>
</scxml>
```

---

## 3. End-to-End Example: SCXML to C++20 Header

=== "W3C SCXML Source (`controller.scxml`)"
    ```xml
    <?xml version="1.0" encoding="UTF-8"?>
    <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" name="Thermostat" initial="Off">
        <!-- @fsm:port name=current_temp type=float dir=in min=-20.0 max=60.0 -->
        <!-- @fsm:port name=heater_relay type=bool dir=out -->

        <state id="Off">
            <transition event="EvEnable" target="Heating"/>
        </state>

        <state id="Heating">
            <onentry><script>out.heater_relay = true;</script></onentry>
            <transition event="EvTargetReached" cond="in.current_temp >= 22.0" target="Standby"/>
            <transition event="EvDisable" target="Off"/>
        </state>

        <state id="Standby">
            <onentry><script>out.heater_relay = false;</script></onentry>
            <transition event="EvTempDrop" cond="in.current_temp < 19.0" target="Heating"/>
            <transition event="EvDisable" target="Off"/>
        </state>
    </scxml>
    ```

=== "Generated C++20 Header (`thermostat_fsm.hpp`)"
    ```cpp
    #pragma once
    #include <fsm/backend/cpp/runtime/fsm.hpp>

    namespace Thermostat {

    struct InPorts {
        float current_temp{0.0f};
    };

    struct OutPorts {
        bool heater_relay{false};
    };

    struct Off {};
    struct Heating {};
    struct Standby {};

    struct EvEnable {};
    struct EvDisable {};
    struct EvTargetReached {};
    struct EvTempDrop {};

    } // namespace Thermostat
    ```

---

## 4. CLI Invocations

```bash
# Formally verify SCXML model
fsmc -i controller.scxml --verify

# Generate standalone C++20 header
fsmc -i controller.scxml -o controller_fsm.hpp --std 20

# Transpile SCXML into SysML v2 syntax
fsmc -i controller.scxml --export sysml2 -o controller.sysml

# Transpile SCXML into Mermaid or PlantUML visual diagram
fsmc -i controller.scxml --export mermaid -o diagram.mmd
```
