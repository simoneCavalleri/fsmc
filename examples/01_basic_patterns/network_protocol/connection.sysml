// ============================================================================
// OMG SysML v2.0 State Machine: High-Throughput Network Protocol Handshake
// Domain: Telecom & Distributed Embedded Systems
// ============================================================================

package NetworkProtocols {
    private import ScalarValues::*;

    item def ConnectCmd;
    item def DisconnectCmd;
    item def HandshakeOkEvent;
    item def HandshakeFailedEvent;
    item def TimeoutEvent;
    item def NetworkDegradedEvent;
    item def NetworkRestoredEvent;
    item def ConnectionLostEvent;

    state def ConnectionFSM {
        // Typed MBSE I/O Parameters with Physical Bounds
        in has_network_interface : Boolean;
        in has_valid_credentials : Boolean;
        in rx_bytes_per_sec : Real {
            assert constraint { rx_bytes_per_sec >= 0.0 and rx_bytes_per_sec <= 10000000.0 }
        }
        out tx_bytes_per_sec : Real {
            assert constraint { tx_bytes_per_sec >= 0.0 and tx_bytes_per_sec <= 10000000.0 }
        }

        // Boolean guard evaluations
        calc HasNetworkGuard { return : Boolean; }
        calc HasValidCredentialsGuard { return : Boolean; }

        entry; then Disconnected;

        state Disconnected;
        state Connecting;
        state Connected;
        state Suspended;

        // Disconnected: Conditional branching with composite boolean guards
        transition first Disconnected accept ConnectCmd
            if HasNetworkGuard and HasValidCredentialsGuard
            do action InitSocketAction then Connecting;

        transition first Disconnected accept ConnectCmd
            if not HasNetworkGuard or not HasValidCredentialsGuard
            do action LogErrorAction then Disconnected;

        // Connecting: Handshake verification and timeout handling
        transition first Connecting accept HandshakeOkEvent
            do action SetupSessionAction then Connected;

        transition first Connecting accept HandshakeFailedEvent
            do action CleanupAction then Disconnected;

        transition first Connecting accept TimeoutEvent
            do action CleanupAction then Disconnected;

        // Connected: Session maintenance and degradation
        transition first Connected accept NetworkDegradedEvent
            do action PauseQueueAction then Suspended;

        transition first Connected accept DisconnectCmd
            do action CloseSocketAction then Disconnected;

        transition first Connected accept ConnectionLostEvent
            do action CleanupAction then Disconnected;

        // Suspended: Link recovery and session teardown
        transition first Suspended accept NetworkRestoredEvent
            do action ResumeQueueAction then Connected;

        transition first Suspended accept DisconnectCmd
            do action CloseSocketAction then Disconnected;

        transition first Suspended accept TimeoutEvent
            do action CleanupAction then Disconnected;
    }
}
