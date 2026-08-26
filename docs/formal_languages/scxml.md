# W3C State Chart XML (SCXML)

`fsmc` provides bidirectional conversion for the **W3C SCXML** standard state machine format.

---

## Example SCXML

```xml
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle">
    <state id="Idle">
        <transition event="Start" target="Active"/>
    </state>
    <state id="Active">
        <transition event="Stop" target="Idle"/>
    </state>
</scxml>
```

Compile with:
```bash
fsmc -i statechart.scxml -o statechart_fsm.hpp --std 20
```
