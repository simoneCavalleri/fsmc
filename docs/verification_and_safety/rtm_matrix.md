# Requirement Traceability Matrix (RTM)

For DO-178C (DAL A/B) and ISO 26262 (ASIL D) certification, `fsmc` automatically compiles requirements into audit-ready traceability matrices.

---

## Example RTM Export

```bash
fsmc -i flight_control.sysml --rtm-output rtm.md
fsmc -i flight_control.sysml --rtm-output rtm.json
```

### Markdown Table Output

| Requirement ID | Description | Covered States | Covered Transitions | Formal Properties | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `REQ-SAFETY-01` | Guarantees emergency return upon low battery | `SafeMode` | `Idle -> SafeMode` | `SafeLandingOnLowBattery` | **PASSED** |
