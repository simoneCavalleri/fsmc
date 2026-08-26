# OMG Systems Modeling Language (SysML) v2

`fsmc` features native parsing and emission for the **OMG SysML v2** textual state definition grammar.

---

## 1. Supported SysML v2 State Syntax

`fsmc` parses the full declarative state machine syntax defined in the OMG SysML v2 standard:

```sysml
package SatelliteOperations {
    state def SatelliteStatechart {
        // Variable attributes with initial values
        attribute batteryCharge : Real = 95.0;
        attribute orbitCount : Integer = 0;
        attribute eclipseDetected : Boolean = false;

        // Entry into initial state
        entry; then state Initialization;

        state Initialization {
            entry action doInitializeHardware();
            exit action logInitComplete();
            transition on SystemOk to Detumbling;
        }

        state Detumbling {
            transition on RatesNominal to SunAcquisition;
            transition on Timeout if batteryCharge < 20.0 to SafeMode;
        }

        state SunAcquisition {
            transition on SunLocked do batteryCharge += 5.0; to NominalOps;
        }

        // Composite state with nested substates
        state NominalOps {
            entry; then state PayloadActive;

            state PayloadActive {
                transition on OrbitCompleted do orbitCount += 1; to PayloadActive;
                transition on EclipseEntry to EclipsePassive;
            }

            state EclipsePassive {
                transition on EclipseExit to PayloadActive;
            }
        }

        state SafeMode;
    }
}
```

---

## 2. Compilation and Code Generation

```bash
# Compile SysML v2 into standalone C++20 header
fsmc -i satellite.sysml -o satellite_fsm.hpp --std 20 --namespace space --name SatelliteFSM
```

---

## 3. Supported Directives and Extensions

SysML v2 comments can contain compiler directives:

- `@fsm:req "<id>"`: Links state or transition to a system requirement for RTM reporting.
- `@fsm:property <name> = "<LTL/CTL formula>"`: Defines a formal verification property.
- `@fsm:deferred <EventName>`: Marks an event as deferred in this state.
