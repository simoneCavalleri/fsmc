// ============================================================================
// OMG SysML v2.0 Shared Protocol Package
// Imported by thermal_subsystem.sysml via `private import SharedProtocol::*;`
// ============================================================================

package SharedProtocol {
    private import ScalarValues::*;

    item def EvSample;
    item def EvCoolComplete;
    item def EvOverTemp {
        attribute peak_temp : Integer;
    }

    port def ThermalLinkPort {
        in attribute temp_c : Integer;
        in item alert : EvOverTemp;
    }
}
