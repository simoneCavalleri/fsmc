// ============================================================================
// OMG SysML v2 State Machine: High-Throughput Network Protocol Handshake
// Domain: Telecom & Distributed Embedded Systems
// ============================================================================

package NetworkProtocols {
    state def ConnectionFSM {
        // Typed MBSE I/O Ports with Physical Bounds
        in port has_network_interface : Boolean;
        in port has_valid_credentials : Boolean;
        in port rx_bytes_per_sec : Real { assert constraint { self >= 0.0 and self <= 10000000.0; } }
        out port tx_bytes_per_sec : Real { assert constraint { self >= 0.0 and self <= 10000000.0; } }

        entry; then Disconnected;

        state Disconnected;
        state Connecting;
        state Connected;
        state Suspended;

        // Disconnected: Conditional branching with composite boolean guards
        transition from Disconnected accept ConnectCmd 
            if HasNetworkGuard && HasValidCredentialsGuard 
            do InitSocketAction then Connecting;

        transition from Disconnected accept ConnectCmd 
            if !HasNetworkGuard || !HasValidCredentialsGuard 
            do LogErrorAction then Disconnected;

        // Connecting: Handshake verification and timeout handling
        transition from Connecting accept HandshakeOkEvent 
            do SetupSessionAction then Connected;

        transition from Connecting accept HandshakeFailedEvent 
            do CleanupAction then Disconnected;

        transition from Connecting accept TimeoutEvent 
            do CleanupAction then Disconnected;

        // Connected: Session maintenance and degradation
        transition from Connected accept NetworkDegradedEvent 
            do PauseQueueAction then Suspended;

        transition from Connected accept DisconnectCmd 
            do CloseSocketAction then Disconnected;

        transition from Connected accept ConnectionLostEvent 
            do CleanupAction then Disconnected;

        // Suspended: Link recovery and session teardown
        transition from Suspended accept NetworkRestoredEvent 
            do ResumeQueueAction then Connected;

        transition from Suspended accept DisconnectCmd 
            do CloseSocketAction then Disconnected;

        transition from Suspended accept TimeoutEvent 
            do CleanupAction then Disconnected;
    }
}
