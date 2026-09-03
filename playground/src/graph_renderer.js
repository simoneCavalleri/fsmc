/**
 * fsmc Playground — Graph Renderer
 * Renders the canonical FSM model as a Mermaid stateDiagram-v2 SVG.
 * Manages active state highlight and click handlers on the SVG nodes.
 */

import { ModelManager } from './model_manager.js';
import { getModule }    from './wasm_bridge.js';
import { resolveLeafState, getAncestorChain } from './fsm_utils.js';

let renderSeq = 0;

export const GraphRenderer = {
  buildCanonicalGraph(model) {
    let out = "stateDiagram-v2\n";
    if (model.initialState) out += `    [*] --> ${model.initialState}\n`;

    const details = model.stateDetails || [];
    const emittedStates = new Set();

    function emitSubtree(parentName, indent) {
      const pad = "    ".repeat(indent);
      for (const s of details) {
        if (s.parent !== parentName) continue;
        emittedStates.add(s.name);
        if (s.is_composite) {
          out += `${pad}state ${s.name} {\n`;
          if (s.initial_sub_state) out += `${pad}    [*] --> ${s.initial_sub_state}\n`;
          emitSubtree(s.name, indent + 1);
          out += `${pad}}\n`;

          const hasNotes = (s.entry_actions?.length) || (s.exit_actions?.length) || s.do_activity || (s.deferred_events?.length);
          if (hasNotes) {
            out += `${pad}note right of ${s.name}\n`;
            for (const act of (s.entry_actions || [])) out += `${pad}    entry / ${typeof act === 'object' ? act.name : act}\n`;
            if (s.do_activity) out += `${pad}    do / ${s.do_activity}\n`;
            for (const act of (s.exit_actions  || [])) out += `${pad}    exit / ${typeof act === 'object' ? act.name : act}\n`;
            for (const dev of (s.deferred_events || [])) out += `${pad}    defer ${dev}\n`;
            out += `${pad}end note\n`;
          }
        } else {
          const hasActions = (s.entry_actions?.length) || s.do_activity || (s.exit_actions?.length) || (s.deferred_events?.length);
          if (!hasActions) {
            out += `${pad}state ${s.name}\n`;
          } else {
            const actLines = [];
            for (const act of (s.entry_actions || [])) actLines.push(`entry / ${typeof act === 'object' ? act.name : act}`);
            if (s.do_activity) actLines.push(`do / ${s.do_activity}`);
            for (const act of (s.exit_actions  || [])) actLines.push(`exit / ${typeof act === 'object' ? act.name : act}`);
            for (const dev of (s.deferred_events || [])) actLines.push(`defer ${dev}`);
            const label = `<b>${s.name}</b><hr/>${actLines.join("<br/>")}`;
            out += `${pad}state "${label}" as ${s.name}\n`;
          }
        }
      }
    }

    emitSubtree("", 1);

    for (const st of model.states) {
      if (!emittedStates.has(st)) out += `    state ${st}\n`;
    }

    const ANON = new Set(["Anonymous", "AnonymousEvent", "anonymous"]);
    for (const t of model.transitions) {
      let label = (!t.event || ANON.has(t.event)) ? "" : t.event;
      if (t.guard) {
        let cg = t.guard
          .replace(/fsm::and_<(.+?)>/g, (_, p) => p.replace(/,/g, ' && '))
          .replace(/fsm::or_<(.+?)>/g,  (_, p) => p.replace(/,/g, ' || '))
          .replace(/fsm::not_<(.+?)>/g, (_, p) => `!${p.trim()}`)
          .replace(/&amp;/g, '&').replace(/;/g, ' ');
        label += label ? ` [${cg}]` : `[${cg}]`;
      }
      if (t.action) {
        const cact = t.action.replace(/;/g, ', ').replace(/,\s*$/, '').trim();
        label += label ? ` / ${cact}` : `/ ${cact}`;
      }

      let effectiveTgt = t.target;
      if (t.source !== t.target && getAncestorChain(model, t.source).includes(t.target)) {
        effectiveTgt = resolveLeafState(model, t.target);
        if (t.target_is_history && !label.includes("[H]")) {
          label = label ? `${label} [H]` : "[H]";
        }
      }

      const lblStr = label ? ` : ${label}` : "";
      out += `    ${t.source} --> ${t.is_internal ? t.source : effectiveTgt}${lblStr}\n`;
    }
    return out.trim();
  },

  async render(model, sourceCode, format) {
    const canvas = document.getElementById("mermaidCanvas");
    if (!model.states || model.states.length === 0) {
      canvas.innerHTML = `<div style="color:var(--text-muted);font-family:var(--font-mono);font-size:0.8rem;padding:20px;text-align:center;">No states detected in diagram.</div>`;
      return;
    }

    const mod = getModule();
    let canonicalGraph = "";
    if (mod && mod.exportDiagram && sourceCode && sourceCode.trim()) {
      try {
        const exported = mod.exportDiagram(sourceCode, format, "mermaid");
        if (exported && !exported.startsWith("// [FSMC ERROR]")) canonicalGraph = exported;
      } catch (e) {
        console.warn("WASM exportDiagram notice:", e);
      }
    }
    if (!canonicalGraph) canonicalGraph = this.buildCanonicalGraph(model);

    if (window.mermaid && canonicalGraph) {
      const seq = ++renderSeq;
      const tryRender = async (graph) => {
        let renderGraph = graph.trim();
        const sdPos = renderGraph.indexOf("stateDiagram");
        renderGraph = sdPos !== -1 ? renderGraph.slice(sdPos) : "stateDiagram-v2\n" + renderGraph;

        const id = "mermaid_svg_" + seq;
        const { svg } = await mermaid.render(id, renderGraph);
        if (seq === renderSeq) {
          canvas.innerHTML = svg;
          this.highlightActive(model.activeState);
          this.attachHandlers();
          // ViewportController imported lazily to avoid circular dep
          const { ViewportController } = await import('./viewport.js');
          ViewportController.applyTransform(false);
        }
      };

      try {
        await tryRender(canonicalGraph);
      } catch (err) {
        console.warn("Mermaid layout notice:", err);
        const tempEl = document.getElementById("d" + "mermaid_svg_" + seq);
        if (tempEl) tempEl.remove();
        try {
          await tryRender(this.buildCanonicalGraph(model));
        } catch (fbErr) {
          console.warn("Mermaid fallback notice:", fbErr);
        }
      }
    }
  },

  highlightActive(activeState) {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg || !activeState) return;

    svg.querySelectorAll(".active-state-node, .active-state").forEach(n => {
      n.classList.remove("active-state-node", "active-state");
      n.querySelectorAll("rect, polygon, circle, path, .label-container").forEach(shape => {
        shape.style.stroke = "";
        shape.style.strokeWidth = "";
        shape.style.filter = "";
        shape.style.fill = "";
        shape.style.fillOpacity = "";
      });
    });

    const allNodes = Array.from(svg.querySelectorAll(".node, .statediagram-state"));
    let bestMatch = null;

    for (const n of allNodes) {
      const idMatch = n.id && (n.id === `state-${activeState}` || n.id.startsWith(`state-${activeState}-`) || n.id.endsWith(`-${activeState}`));
      const text = (n.querySelector(".nodeLabel, text, foreignObject, div") || n).textContent?.trim() || "";
      const firstLine = text.split(/\n|\/|<br>/)[0].trim().replace(/^\*\s*/, '');
      if (idMatch || text === activeState || firstLine === activeState) { bestMatch = n; break; }
    }
    if (!bestMatch) {
      for (const n of allNodes) {
        const text = n.textContent?.trim() || "";
        if (text.startsWith(activeState) || text.includes(activeState)) { bestMatch = n; break; }
      }
    }

    if (bestMatch) {
      bestMatch.classList.add("active-state-node", "active-state");
      bestMatch.querySelectorAll("rect, polygon, circle, path, .label-container").forEach(shape => {
        shape.style.stroke = "#10b981";
        shape.style.strokeWidth = "3.5px";
        shape.style.filter = "drop-shadow(0 0 12px rgba(16,185,129,0.85))";
        shape.style.fill = "#064e3b";
        shape.style.fillOpacity = "0.45";
      });
    }
  },

  attachHandlers() {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;
    svg.querySelectorAll(".node, .statediagram-state").forEach(n => {
      n.style.cursor = "pointer";
      n.onclick = async (e) => {
        e.stopPropagation();
        const text = n.textContent.trim();
        const found = ModelManager.currentModel.states.find(s => {
          const firstLine = text.split(/\n|\/|<br>/)[0].trim();
          return firstLine === s || text.startsWith(s);
        });
        if (found) {
          const { SimulatorController } = await import('./simulator.js');
          SimulatorController.setState(found);
        }
      };
    });
  }
};
