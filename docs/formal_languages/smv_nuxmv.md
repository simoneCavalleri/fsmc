# nuXmv / SMV Formal Verification Language

`fsmc` compiles statechart models into formal symbolic transition systems formatted for the **nuXmv** and **NuSMV** model checking suites.

---

## 1. SMV Translation Pipeline

During SMV export, `fsmc`:
1. Constructs the discrete finite state domain `state : {StateA, StateB, ...}`.
2. Constructs the input event alphabet `event : {Ev1, Ev2, ..., none}`.
3. Maps discrete timer variables with discrete-time tick counters for `after` / `every` transitions.
4. Generates symbolic transition relation assignments (`next(state) := case ... esac;`).
5. Appends all embedded LTL (`LTLSPEC`) and CTL (`CTLSPEC`) properties.

---

## 2. Example Generated SMV Model

```smv
MODULE main
VAR
    state : {SensorCalib, SystemReady, WaypointNav, ReturnToHome, Landed};
    event : {CalibrationOk, TakeoffCmd, AreaReached, LowBatteryEvent, TouchdownEvent, none};
    batteryLevel : 0..100;

ASSIGN
    init(state) := SensorCalib;
    init(batteryLevel) := 100;

    next(state) := case
        state = SensorCalib & event = CalibrationOk : SystemReady;
        state = SystemReady & event = TakeoffCmd : WaypointNav;
        state = WaypointNav & event = LowBatteryEvent & batteryLevel < 20 : ReturnToHome;
        state = ReturnToHome & event = TouchdownEvent : Landed;
        TRUE : state;
    esac;

-- Formal Verification Specifications
LTLSPEC G (event = LowBatteryEvent & batteryLevel < 20 -> F (state = Landed));
INVARSPEC !(state = SensorCalib & state = WaypointNav);
```

---

## 3. CLI Invocations

```bash
# Export SMV model from SysML v2 or PlantUML
fsmc -i uav_mission.sysml --export smv -o uav_mission.smv

# Execute nuXmv externally (optional)
nuxmv uav_mission.smv
```
