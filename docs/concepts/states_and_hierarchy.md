# States & Hierarchical Statecharts (HFSM)

Hierarchical Statecharts (HFSM) allow grouping complex behaviors into composite states with nested substates, initial entries, and history recovery.

---

## State Taxonomy in `FsmIr`

| State Kind | Description | Representation in SysML v2 / UML |
| :--- | :--- | :--- |
| **Atomic** | Simple leaf state with no children. | `state Operational;` |
| **Composite** | State containing one or more nested substates. | `state Active { state Mode1; state Mode2; }` |
| **Initial** | Entry pseudostate designating initial leaf state. | `entry; then state Mode1;` |
| **Final** | Termination state designating end of lifecycle. | `state FinalShutdown;` |
| **Shallow History `[H]`** | Remembers the immediate most recently active substate. | `[H]` |
| **Deep History `[H*]`** | Recursively remembers the deepest active substate across all levels. | `[H*]` |
| **Choice / Junction** | Dynamic decision pseudostate evaluated upon entry. | `state my_choice <<choice>>;` |

---

## Shallow vs Deep History Recovery

=== "Shallow History `[H]`"
    When transitioning to a composite state with shallow history `[H]`, the state machine restores the top-level substate that was active when exiting, re-entering its default initial substate.

=== "Deep History `[H*]`"
    When transitioning with deep history `[H*]`, the state machine recursively descends the entire active substate hierarchy down to the exact leaf state.
