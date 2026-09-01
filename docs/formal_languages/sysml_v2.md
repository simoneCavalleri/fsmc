# OMG Systems Modeling Language (SysML) v2

`fsmc` features native parsing and emission for the **OMG SysML v2** textual state definition grammar, including typed ports with range contracts, internal attribute registers, and action behaviors.

---

## 1. Supported SysML v2 State Syntax

`fsmc` parses the full declarative state machine syntax defined in the OMG SysML v2 standard:

```sysml
package SatelliteOperations {
    state def SatelliteStatechart {
        // Typed Ports with formal range contracts
        in port batteryCharge : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        in port eclipseDetected : Boolean;
        out port heaterPower : Real { assert constraint { self >= 0.0 and self <= 100.0; } }

        // Internal Attribute Registers (z^-1 Memory)
        attribute orbitCount : Integer = 0;

        // Entry into initial state
        entry; then state Initialization;

        state Initialization {
            entry action doInitializeHardware();
            exit action logInitComplete();
            transition on SystemOk to Detumbling;
        }

        state Detumbling {
            transition on RatesNominal to SunAcquisition;
            transition on Timeout if in.batteryCharge < 20.0 to SafeMode;
        }

        state SunAcquisition {
            transition on SunLocked do action { out.heaterPower = 50.0; } then NominalOps;
        }

        // Composite state with nested substates
        state NominalOps {
            entry; then state PayloadActive;

            state PayloadActive {
                transition on OrbitCompleted do action { reg.orbitCount = reg.orbitCount + 1; } then PayloadActive;
                transition on EclipseEntry if in.eclipseDetected then EclipsePassive;
            }

            state EclipsePassive {
                transition on EclipseExit if not in.eclipseDetected then PayloadActive;
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

SysML v2 comments and docstrings can contain compiler directives:

- `@fsm:req "<id>"`: Links state or transition to a system requirement for RTM reporting.
- `@fsm:property <name> = "<LTL/CTL formula>"`: Defines a formal verification property.
- `@fsm:deferred <EventName>`: Marks an event as deferred in this state.
