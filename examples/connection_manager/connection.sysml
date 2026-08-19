// OMG SysML v2 State Machine Definition
// Network Connection Manager

state def ConnectionFSM {
    entry; then Disconnected;

    state Disconnected;
    state Connecting;
    state Connected;
    state Suspended;

    // Disconnected transitions
    transition from Disconnected accept ConnectCmd if HasNetworkGuard do InitSocketAction then Connecting;
    transition from Disconnected accept ConnectCmd if NoNetworkGuard do LogErrorAction then Disconnected;

    // Connecting transitions
    transition from Connecting accept HandshakeOkEvent do SetupSessionAction then Connected;
    transition from Connecting accept HandshakeFailedEvent do CleanupAction then Disconnected;
    transition from Connecting accept TimeoutEvent do CleanupAction then Disconnected;

    // Connected transitions
    transition from Connected accept NetworkDegradedEvent do PauseQueueAction then Suspended;
    transition from Connected accept DisconnectCmd do CloseSocketAction then Disconnected;
    transition from Connected accept ConnectionLostEvent do CleanupAction then Disconnected;

    // Suspended transitions
    transition from Suspended accept NetworkRestoredEvent do ResumeQueueAction then Connected;
    transition from Suspended accept DisconnectCmd do CloseSocketAction then Disconnected;
    transition from Suspended accept TimeoutEvent do CleanupAction then Disconnected;
}
