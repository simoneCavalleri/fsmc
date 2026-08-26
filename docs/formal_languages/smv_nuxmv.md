# nuXmv / SMV Formal Verification

`fsmc` transforms state machines into symbolic transition systems formatted for **nuXmv** and **NuSMV** model checkers.

---

## Example Generated SMV

```smv
MODULE main
VAR
    state : {Idle, Running, Paused};
    event : {EvStart, EvPause, EvResume, EvStop, none};

ASSIGN
    init(state) := Idle;
    next(state) := case
        state = Idle & event = EvStart : Running;
        state = Running & event = EvPause : Paused;
        state = Paused & event = EvResume : Running;
        state = Running & event = EvStop : Idle;
        TRUE : state;
    esac;

-- Formal LTL Specification
LTLSPEC G (event = EvStart -> F (state = Running));
```
