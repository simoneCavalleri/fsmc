# W3C State Chart XML (SCXML)

`fsmc` provides bidirectional parsing and code generation for the **W3C State Chart XML (SCXML)** standard.

---

## 1. Supported SCXML Elements

- `<scxml>`: Root statechart with `initial` state attribute.
- `<state>`: Atomic and composite states with nested states.
- `<initial>`: Explicit initial transition block.
- `<transition>`: Transitions with `event`, `cond` (guard predicate), and `target` attributes.
- `<onentry>` and `<onexit>`: Lifecycle action blocks.
- `<history>`: Shallow and deep history elements (`type="shallow|deep"`).
- `<final>`: Termination states.

---

## 2. Example SCXML Statechart

```xml
<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Standby">
    <state id="Standby">
        <transition event="StartCmd" target="Active"/>
    </state>

    <state id="Active" initial="Operational">
        <state id="Operational">
            <transition event="PauseCmd" target="Paused"/>
        </state>
        <state id="Paused">
            <transition event="ResumeCmd" target="Operational"/>
        </state>
        <transition event="StopCmd" target="Standby"/>
    </state>
</scxml>
```

Compile with:
```bash
fsmc -i statechart.scxml -o statechart_fsm.hpp --std 20
```
