# Formal Model Checking (LTL / CTL)

`fsmc` includes an embedded model checking engine that exhaustively explores the reachable Kripke structure of the state machine.

---

## Supported Temporal Properties

1. **Safety Invariants (`G (P)`)**: The predicate $P$ holds globally in every reachable state.
2. **Reachability / Target (`F (P)`)**: The predicate $P$ is guaranteed to eventually be reachable.
3. **Response / Liveness (`G (P -> F Q)`)**: Every occurrence of trigger/state $P$ is eventually followed by $Q$.
4. **Mutual Exclusion (`G (!(StateA && StateB))`)**: Proves orthogonal regions never enter conflicting states simultaneously.
5. **Deadlock Freedom (`G (!Deadlock)`)**: Proves that every reachable non-terminal state has at least one valid outgoing transition.
