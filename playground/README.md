# 🌐 fsmc Live Web Playground & State Machine Simulator

The **fsmc Playground** is an interactive, browser-based developer environment for designing, formally checking, compiling, and simulating state machines in real time.

---

## ✨ Features

- **Multi-Format Live Editor**: Write statecharts in Mermaid, PlantUML, SysML v2, or JSON.
- **Visual Statechart Rendering**: Instant interactive graph rendering powered by Mermaid.js.
- **Formal Verification Engine**: Real-time diagnostics for livelocks, choice pseudostate completeness, deadlocks, and transition determinism.
- **C++ Code Generator**: Generates clean, zero-overhead C++17 and C++20 transition tables with one-click copy.
- **Interactive Simulator**: Click on extracted events to dispatch them, trigger guards/actions, inspect current state, and view execution history logs.

---

## 🚀 Running Locally

You can open `index.html` directly in any modern browser without a web server:

```bash
xdg-open playground/index.html
# Or open in your favorite browser (Chrome, Firefox, Safari, Edge)
```

---

## 📦 Building with WebAssembly (Emscripten)

To compile the native C++ `fsmc` compiler to WebAssembly (`fsmc.wasm`):

```bash
emcmake cmake -B build-wasm -S . -DFSM_BUILD_WASM=ON
cmake --build build-wasm
```
