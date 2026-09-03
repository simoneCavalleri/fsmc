#!/usr/bin/env python3
"""
Bundle playground ES modules from playground/src/ into a single standalone
playground/playground.js file for zero-CORS file:// local execution and
seamless web serving.
"""

import os
import re
import sys

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(BASE_DIR, "playground", "src")
OUTPUT_FILE = os.path.join(BASE_DIR, "playground", "playground.js")

MODULES = [
    "presets.js",
    "fsm_utils.js",
    "wasm_bridge.js",
    "parsers/format_detect.js",
    "parsers/scxml.js",
    "parsers/plantuml.js",
    "parsers/sysml.js",
    "parsers/misc.js",
    "parsers/index.js",
    "serializers/index.js",
    "cpp_generator.js",
    "model_manager.js",
    "viewport.js",
    "graph_renderer.js",
    "simulator.js",
    "app.js",
]

def clean_module_code(code: str, filename: str) -> str:
    lines = []
    for line in code.splitlines():
        # Remove static import statements
        if re.match(r'^\s*import\s+.*?;?\s*$', line):
            continue
        # Remove re-exports like "export { ... } from ...;"
        if re.match(r'^\s*export\s+\{.*?\}\s+from\s+.*?;?\s*$', line):
            continue
        # Replace export function/const/let/var
        line = re.sub(r'^\s*export\s+(function|const|let|var|class|async\s+function)\b', r'\1', line)
        # Replace export default
        line = re.sub(r'^\s*export\s+default\b', '', line)
        # Replace dynamic imports for bundled modules
        line = re.sub(r'const\s*\{\s*ViewportController\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*SimulatorController\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*detectFormat\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parseScxml\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parseCameo\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parseSysml2\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parseJson\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parseDot\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parseSmv\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        line = re.sub(r'const\s*\{\s*parsePlantUmlOrMermaid\s*\}\s*=\s*await\s*import\([^\)]+\);?', '', line)
        lines.append(line)
    return "\n".join(lines)

def bundle():
    header = """/**
 * fsmc Web Playground & Live HFSM Simulator
 * ============================================================================
 * Zero-Overhead C++ State Machine Compiler & Visual Engineering Suite
 * Bundled distribution built from playground/src/ modules.
 * Supports direct file:// local access without CORS errors.
 * ============================================================================
 */
"""
    chunks = [header]
    for mod in MODULES:
        path = os.path.join(SRC_DIR, mod)
        if not os.path.exists(path):
            print(f"Error: Missing module {path}", file=sys.stderr)
            sys.exit(1)
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        cleaned = clean_module_code(content, mod)
        chunks.append(f"\n// --- Module: src/{mod} ---\n")
        chunks.append(cleaned)

    # Expose main controllers on globalThis for browser console interaction / debugging
    chunks.append("""
// Global exposure for browser console access
if (typeof window !== 'undefined') {
  window.CANONICAL_PRESETS = CANONICAL_PRESETS;
  window.ModelManager = ModelManager;
  window.GraphRenderer = GraphRenderer;
  window.ViewportController = ViewportController;
  window.SimulatorController = SimulatorController;
  window.App = App;
}
""")

    bundled_code = "\n".join(chunks)
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(bundled_code)
    print(f"Bundled {len(MODULES)} modules into {OUTPUT_FILE} ({len(bundled_code)} bytes)")

if __name__ == "__main__":
    bundle()
