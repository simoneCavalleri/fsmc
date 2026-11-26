# W3C State Chart XML (SCXML)

`fsmc` provides bidirectional parsing, validation, and C++ code generation for the **W3C State Chart XML (SCXML)** standard.

---

## 1. Supported W3C SCXML Elements

`fsmc` maps the core elements of the W3C SCXML specification into its strongly typed `FsmIr` AST:

| SCXML Element | Attributes / Children | `fsmc` Semantic Mapping |
| :--- | :--- | :--- |
| `<scxml>` | `name`, `initial`, `version="1.0"` | Root state machine model |
| `<datamodel>` | `<data id="..." expr="..."/>` | Internal memory registers (`Registers`), inferring `bool`, `double`, `int32_t`, `uint32_t`, or `std::string` from `expr` when `type` is omitted |
| `<state>` | `id`, `initial` | Simple atomic state or composite parent state |
| `<parallel>` | `id` | Orthogonal concurrent execution regions (flattened via `OrthogonalProductPass`) |
| `<history>` | `id`, `type="shallow\|deep"` | History pseudostate with $z^{-1}$ memory |
| `<final>` | `id` (`<onentry>`, `<onexit>`, `<donedata>`) | Final termination state emitting completion event `done.state.<parent_id>` |
| `<transition>` | `event`, `cond`, `target`, `type="external\|internal"` | Transition with event trigger, guard, target, and external/internal/local semantics |
| `<send>` / `<cancel>` | `id`, `event`, `delay`, `sendid` | Internal event dispatch and W3C delayed-send timed transitions |
| `<onentry>` / `<onexit>` | `<assign>`, `<send>`, `<cancel>`, `<script>` | State entry and exit lifecycle actions |

---

## 2. Embedding Lossless Directives in SCXML

Because standard SCXML lacks native elements for hardware I/O port range contracts or temporal LTL model checking formulas, `fsmc` supports W3C-valid `xmlns:fsm` extension attributes on `<data>`/`<state>`/`<transition>` as well as `@fsm:*` directives inside standard XML comments:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml"
       xmlns:fsm="https://github.com/simoneCavalleri/fsmc/scxml"
       version="1.0" name="MotorController" initial="Idle">

    <!-- @fsm:property name=SafeStop kind=Safety ltl="G (EvFault -> F SafeIdle)" -->

    <datamodel>
        <data id="bus_voltage" expr="0.0" fsm:port="true" fsm:type="float" fsm:dir="in" fsm:min="0.0" fsm:max="48.0"/>
        <data id="pwm_duty" expr="0.0" fsm:port="true" fsm:type="float" fsm:dir="out" fsm:min="0.0" fsm:max="100.0"/>
        <data id="retry_count" expr="0" fsm:type="uint32"/>
    </datamodel>

    <state id="Idle">
        <transition event="EvStart" cond="bus_voltage &gt; 12.0" target="Running"/>
    </state>

    <state id="Running">
        <onentry>
            <assign location="pwm_duty" expr="50.0"/>
            <send id="_timer_Running_1" event="after_500ms" delay="500ms"/>
        </onentry>
        <onexit>
            <cancel sendid="_timer_Running_1"/>
        </onexit>
        <transition event="EvStop" target="Idle"/>
        <transition event="EvFault" target="SafeIdle"/>
        <transition event="after_500ms" target="SafeIdle"/>
    </state>

    <state id="SafeIdle"/>
</scxml>
```

---

## 3. End-to-End Example: SCXML to C++20 Header

=== "W3C SCXML Source (`controller.scxml`)"
    ```xml
    <?xml version="1.0" encoding="UTF-8"?>
    <scxml xmlns="http://www.w3.org/2005/07/scxml"
           xmlns:fsm="https://github.com/simoneCavalleri/fsmc/scxml"
           version="1.0" name="Thermostat" initial="Off">
        <datamodel>
            <data id="current_temp" expr="20.0" fsm:port="true" fsm:type="float" fsm:dir="in" fsm:min="-20.0" fsm:max="60.0"/>
            <data id="heater_relay" expr="false" fsm:port="true" fsm:type="bool" fsm:dir="out"/>
        </datamodel>

        <state id="Off">
            <transition event="EvEnable" target="Heating"/>
        </state>

        <state id="Heating">
            <onentry><assign location="heater_relay" expr="true"/></onentry>
            <transition event="EvTargetReached" cond="current_temp &gt;= 22.0" target="Standby"/>
            <transition event="EvDisable" target="Off"/>
        </state>

        <state id="Standby">
            <onentry><assign location="heater_relay" expr="false"/></onentry>
            <transition event="EvTempDrop" cond="current_temp &lt; 19.0" target="Heating"/>
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
fsmc -i controller.scxml -o controller_fsm.hpp

# Transpile SCXML into SysML v2 syntax
fsmc -i controller.scxml --export sysml2 -o controller.sysml

# Transpile SCXML into Mermaid or PlantUML visual diagram
fsmc -i controller.scxml --export mermaid -o diagram.mmd
```
