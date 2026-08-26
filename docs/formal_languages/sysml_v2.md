# OMG SysML v2 Statecharts

`fsmc` features native parsing and emission for the **OMG Systems Modeling Language (SysML) v2** textual state definition notation.

---

## Example SysML v2 Statechart

```sysml
package SatelliteOperations {
    state def SatelliteStatechart {
        attribute batteryCharge : Real = 95.0;
        attribute orbitCount : Integer = 0;

        entry; then state Initialization;

        state Initialization {
            transition on SystemOk to Detumbling;
        }

        state Detumbling {
            transition on RatesNominal to SunAcquisition;
        }

        state SunAcquisition {
            transition on SunLocked do batteryCharge += 5.0; to NominalOps;
        }

        state NominalOps {
            transition on EclipseEntry to EclipseOps;
        }

        state EclipseOps {
            transition on EclipseExit to SunAcquisition;
        }
    }
}
```

Compile with:
```bash
fsmc -i satellite.sysml -o satellite_fsm.hpp --std 20 --name SatelliteFSM
```
