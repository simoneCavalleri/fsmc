# Requirement Traceability Matrix (RTM)

Safety-critical certification standards—such as **DO-178C (Aerospace)**, **ISO 26262 (Automotive)**, **ECSS (Space)**, and **IEC 62304 (Medical)**—mandate bidirectional traceability between system requirements, architecture design, formal verification outcomes, and generated code artifacts.

`fsmc` automates this verification workflow via the `RtmEmitter`.

---

## 1. Tagging Requirements in Models

Requirements can be attached to states, transitions, and formal properties using the `@fsm:req` directive:

```sysml
package AerospaceEngine {
    state def EngineController {
        // Tagged State
        @fsm:req "REQ-ENG-SAFE-01"
        state SafeShutdown;

        state Running {
            // Tagged Transition
            @fsm:req "REQ-ENG-EMERG-02"
            transition on EmergencyStop to SafeShutdown;
        }

        // Tagged Formal LTL Property
        @fsm:req "REQ-ENG-SAFE-01"
        @fsm:property SafeShutdownGuaranteed = "G (EmergencyStop -> F SafeShutdown)";
    }
}
```

---

## 2. Generating Compliance Reports

To generate the traceability report during compilation:

=== "Markdown Audit Report"
    ```bash
    fsmc -i engine.sysml --rtm-output engine_rtm.md
    ```

=== "JSON Structured Schema"
    ```bash
    fsmc -i engine.sysml --rtm-output engine_rtm.json
    ```

---

## 3. Sample Report Format

### Generated Markdown Report
```markdown
# Requirement Traceability Matrix (RTM): EngineController

**Verification Compliance:** 100.0% (2/2 Requirements Verified)

| Requirement ID | Description | Covered States | Covered Transitions | Formal Properties | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `REQ-ENG-EMERG-02` | Emergency stop response | - | `Running -> SafeShutdown` | - | **PASSED** |
| `REQ-ENG-SAFE-01` | Safe shutdown guarantee | `SafeShutdown` | - | `SafeShutdownGuaranteed` | **PASSED** |
```

### Generated JSON Schema (for CI/CD Integration)
```json
{
  "fsm_name": "EngineController",
  "total_requirements": 2,
  "verified_requirements": 2,
  "compliance_percentage": 100.0,
  "requirements": [
    {
      "id": "REQ-ENG-SAFE-01",
      "description": "Safe shutdown guarantee",
      "covered_states": ["SafeShutdown"],
      "covered_transitions": [],
      "formal_properties": ["SafeShutdownGuaranteed"],
      "status": "PASSED"
    }
  ]
}
```
