# Cameo Systems Modeler & MagicDraw (OMG XMI 2.1)

`fsmc` provides bidirectional parsing and code generation for statechart models exported from **Dassault Systèmes / No Magic Cameo Systems Modeler** and **MagicDraw** via the standard OMG XMI 2.1 exchange format.

---

## 1. Exporting Models from Cameo

To export state machine diagrams from Cameo Systems Modeler for compilation with `fsmc`:

1. Open your project in **Cameo Systems Modeler** or **MagicDraw**.
2. Select your State Machine package in the Containment Tree.
3. Navigate to **File** $\to$ **Export To** $\to$ **Eclipse UML2 (XMI 2.1)** (or **OMG XMI**).
4. Save the export file as `model.xmi` or `model.xml`.

---

## 2. Supported XMI 2.1 Semantic Constructs

`fsmc` maps the standard UML 2.5 metamodel elements from the XMI document directly into canonical `FsmIr` nodes:

| UML 2.5 Metamodel Element | XMI XML Tag | `fsmc` Intermediate Representation (`FsmIr`) |
| :--- | :--- | :--- |
| **State Machine Root** | `<packagedElement xmi:type="uml:StateMachine">` | Root `ModelNode` |
| **Execution Region** | `<region xmi:type="uml:Region">` | State container / Composite sub-region |
| **Simple State** | `<subvertex xmi:type="uml:State">` | Atomic `StateNode` |
| **Initial Pseudostate** | `<subvertex xmi:type="uml:Pseudostate" kind="initial">` | Initial entry transition |
| **Choice / Junction** | `<subvertex xmi:type="uml:Pseudostate" kind="choice\|junction">` | `ChoiceNode` branching point |
| **History States** | `<subvertex xmi:type="uml:Pseudostate" kind="shallowHistory\|deepHistory">` | `HistoryNode` with $z^{-1}$ memory |
| **Transition Edge** | `<transition xmi:type="uml:Transition">` | `TransitionEdge` |
| **Guard Predicate** | `<guard xmi:type="uml:Constraint"> <specification body="..."/>` | Strongly typed boolean guard AST |
| **Transition Action** | `<effect xmi:type="uml:OpaqueBehavior" body="..."/>` | Inlined execution action |
| **Port Definition** | `<ownedPort xmi:type="uml:Port">` | Segregated `InPorts` / `OutPorts` domain |
| **Requirement Link** | `<sysml:Satisfy base_Abstraction="...">` | Requirement Traceability (`@fsm:req`) |

---

## 3. Example Cameo XMI Structural Snippet

```xml
<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmi:version="2.1" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1" xmlns:uml="http://www.eclipse.org/uml2/3.0.0/UML">
  <uml:Model xmi:id="_root" name="MissionSystem">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="MissionController">
      <region xmi:id="_region1" name="MainRegion">
        <!-- Initial Pseudostate -->
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_init" kind="initial"/>
        
        <!-- States -->
        <subvertex xmi:type="uml:State" xmi:id="_st_standby" name="Standby"/>
        <subvertex xmi:type="uml:State" xmi:id="_st_active" name="Active"/>
        
        <!-- Transitions -->
        <transition xmi:type="uml:Transition" xmi:id="_tr_init" source="_init" target="_st_standby"/>
        <transition xmi:type="uml:Transition" xmi:id="_tr_start" source="_st_standby" target="_st_active">
          <trigger xmi:type="uml:Trigger" xmi:id="_trig_start" name="EvStart"/>
          <guard xmi:type="uml:Constraint" xmi:id="_guard1">
            <specification xmi:type="uml:OpaqueExpression" body="in.battery_soc > 20.0"/>
          </guard>
          <effect xmi:type="uml:OpaqueBehavior" body="out.heater_power = 50.0"/>
        </transition>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>
```

---

## 4. CLI Invocations

```bash
# Formally verify a Cameo XMI model export
fsmc -i mission_export.xmi --verify

# Generate standalone C++20 header directly from Cameo XMI
fsmc -i mission_export.xmi -o mission_fsm.hpp --std 20 --namespace mission

# Transpile Cameo XMI into canonical SysML v2 syntax
fsmc -i mission_export.xmi --export sysml2 -o mission.sysml

# Transpile Cameo XMI into SMV logic for external nuXmv model checking
fsmc -i mission_export.xmi --export smv -o formal_verification.smv
```
