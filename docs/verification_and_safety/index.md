# Formal Verification & Safety Analysis

Safety-critical applications (such as aerospace, automotive, and medical robotics) require rigorous verification before software execution. `fsmc` embeds automated formal analysis into the compilation pipeline.

---

## Section Contents

| Analysis Module | Primary Target | Description | Link |
| :--- | :--- | :--- | :--- |
| **Model Checking (LTL/CTL)** | Temporal Properties | Proves safety invariants (`INVARSPEC`), reachability, and livelock freedom. | [Model Checking](model_checking.md) |
| **EFSM Interval Analysis** | Numerical Variables | Abstract interpretation bounding numerical variables and detecting dead guards. | [Interval Analysis](interval_analysis.md) |
| **Traceability Matrix (RTM)** | Safety Certification | Automated Markdown & JSON Requirement Traceability Matrix linking `@fsm:req`. | [RTM Specification](rtm_matrix.md) |

---

## Verification Pipeline

```
  Input Model ──► Middle-End Passes ──► EFSM Interval Analysis ──► Temporal Model Checker ──► Sound IR ──► C++ Engine
                                             │                              │
                                             ▼                              ▼
                                     Dead Guard Warning             Counterexample Trace
```

---

For full details on interval analysis and model checking capabilities, see [EFSM Interval Analysis](interval_analysis.md) and [Model Checking (LTL/CTL)](model_checking.md).
