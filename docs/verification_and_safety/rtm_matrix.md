# Requirement Traceability Matrix (RTM)

Maintaining clear traceability between functional requirements, model elements, and formal properties helps ensure that system behavior directly maps to its design specifications.

`fsmc` provides built-in requirement mapping and report generation via the `RtmEmitter`.

---

## 1. Tagging Requirements in Models

Requirements can be attached to states and formal properties natively in SysML v2 (`satisfy requirement`), or via `@fsm:*` diagram/comment directives (`@fsm:state satisfies=[...]`, `@fsm:property ... req="..."`):

=== "OMG SysML v2"
    ```sysml
    package AerospaceEngine {
        state def EngineController {
            // Tagged Formal LTL Property linked to REQ-ENG-SAFE-01
            // @fsm:property SafeShutdownGuaranteed = "G (EmergencyStop -> F SafeShutdown)" req="REQ-ENG-SAFE-01"

            state Running {
                satisfy requirement REQ-ENG-EMERG-02;
                transition on EmergencyStop then SafeShutdown;
            }

            state SafeShutdown {
                satisfy requirement REQ-ENG-SAFE-01;
            }
        }
    }
    ```

=== "PlantUML / Mermaid Directives"
    ```plantuml
    @startuml
    ' @fsm:property name=SafeShutdownGuaranteed formula="G (EmergencyStop -> F SafeShutdown)" req="REQ-ENG-SAFE-01"
    ' @fsm:state name=SafeShutdown satisfies=["REQ-ENG-SAFE-01"]
    ' @fsm:state name=Running satisfies=["REQ-ENG-EMERG-02"]
    [*] --> Running
    Running --> SafeShutdown : EmergencyStop
    @enduml
    ```

---

## 2. Generating Compliance Reports

To generate the traceability report during compilation using `--rtm` (or `--rtm-output`):

=== "Markdown Audit Report"
    ```bash
    fsmc -i engine.sysml --rtm-output engine_rtm.md
    ```

=== "JSON Structured Schema"
    ```bash
    fsmc -i engine.sysml --rtm-output engine_rtm.json --rtm-format json
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
  "compliance_rate": 100.0,
  "requirements": [
    {
      "id": "REQ-ENG-SAFE-01",
      "description": "Safe shutdown guarantee",
      "status": "VERIFIED",
      "covering_states": ["SafeShutdown"],
      "covering_transitions": [],
      "formal_properties": ["SafeShutdownGuaranteed"]
    }
  ]
}
```

---

## 4. Automated CI/CD Quality Gate

You can embed requirement traceability checks into your continuous integration pipeline (GitHub Actions, GitLab CI, Jenkins) as a blocking quality gate:

```yaml
- name: Verify Requirements Coverage (RTM Gate)
  run: |
    # Generate RTM JSON report
    fsmc -i models/engine.sysml --req-audit --rtm-output build/rtm_report.json
    
    # Assert 100% compliance using jq
    COMPLIANCE=$(jq '.compliance_rate' build/rtm_report.json)
    if (( $(echo "$COMPLIANCE < 100.0" | bc -l) )); then
      echo "::error::Requirement Traceability compliance is $COMPLIANCE% (< 100%)!"
      exit 1
    fi
```
