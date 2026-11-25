// ============================================================================
// OMG SysML v2.0 Shared Protocol Package
// Imported by thermal_subsystem.sysml via `import SharedProtocol::*;`
// ============================================================================

package SharedProtocol {
    import ScalarValues::*;

    item def EvSample;
    item def EvCoolComplete;
    item def EvOverTemp {
        attribute peak_temp : Int32;
    }

    port def ThermalLinkPort {
        in attribute temp_c : Int32;
        in item alert : EvOverTemp;
    }
}
