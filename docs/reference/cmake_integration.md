# CMake Integration Reference

`fsmc` exports first-class CMake target generation macros via `fsmc_target_sources`.

---

## `fsmc_target_sources` Macro

```cmake
fsmc_target_sources(<TargetName>
    DIAGRAMS <path/to/model.sysml|puml|mmd|xmi>...
    NAME <FsmClassName>
    STANDARD <17|20>
    [NAMESPACE <CppNamespace>]
    [STANDALONE]
)
```

Example:
```cmake
fsmc_target_sources(my_embedded_target
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/models/battery.sysml
    NAME BatteryFSM
    STANDARD 20
    NAMESPACE bms
)
```
