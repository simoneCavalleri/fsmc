// ============================================================================
// OMG SysML v2 State Machine: High-Throughput Network Protocol Handshake
// Domain: Telecom & Distributed Embedded Systems
// ============================================================================

package NetworkProtocols {
    private import ScalarValues::*;

    state def ConnectionFSM {
        // Typed MBSE I/O Parameters with Physical Bounds
        in has_network_interface : Boolean;
        in has_valid_credentials : Boolean;
        in rx_bytes_per_sec : Real { assert constraint { self >= 0.0 and self <= 10000000.0; } }
        out tx_bytes_per_sec : Real { assert constraint { self >= 0.0 and self <= 10000000.0; } }

        entry; then Disconnected;

        state Disconnected;
        state Connecting;
        state Connected;
        state Suspended;

        // Disconnected: Conditional branching with composite boolean guards
        transition first Disconnected accept ConnectCmd
            if HasNetworkGuard and HasValidCredentialsGuard
            do InitSocketAction then Connecting;

        transition first Disconnected accept ConnectCmd
            if not HasNetworkGuard or not HasValidCredentialsGuard
            do LogErrorAction then Disconnected;

        // Connecting: Handshake verification and timeout handling
        transition first Connecting accept HandshakeOkEvent
            do SetupSessionAction then Connected;

        transition first Connecting accept HandshakeFailedEvent
            do CleanupAction then Disconnected;

        transition first Connecting accept TimeoutEvent
            do CleanupAction then Disconnected;

        // Connected: Session maintenance and degradation
        transition first Connected accept NetworkDegradedEvent
            do PauseQueueAction then Suspended;

        transition first Connected accept DisconnectCmd
            do CloseSocketAction then Disconnected;

        transition first Connected accept ConnectionLostEvent
            do CleanupAction then Disconnected;

        // Suspended: Link recovery and session teardown
        transition first Suspended accept NetworkRestoredEvent
            do ResumeQueueAction then Connected;

        transition first Suspended accept DisconnectCmd
            do CloseSocketAction then Disconnected;

        transition first Suspended accept TimeoutEvent
            do CleanupAction then Disconnected;
    }
}
