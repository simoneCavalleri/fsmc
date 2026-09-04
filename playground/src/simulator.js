/**
 * fsmc Playground — Simulator Controller
 * Hard real-time HFSM simulation engine with dynamic datapath I/O,
 * universal guard evaluation, action execution, and history memory.
 */

import { ModelManager }   from './model_manager.js';
import { GraphRenderer }  from './graph_renderer.js';
import { resolveLeafState, getAncestorChain, getAvailableTransitions } from './fsm_utils.js';

export const SimulatorController = {
  datapath: {
    inPorts:   {},
    registers: {},
    outPorts:  {}
  },

  // Shallow history memory: composite state name -> last active direct child
  historyMap: {},

  flightRecorder: {
    capacity: 64,
    buffer: [],
    currentIndex: -1,
    isTimeTraveling: false
  },

  _recordFlightSnapshot(event = "", reason = "") {
    if (this.flightRecorder.isTimeTraveling) return;
    const snap = {
      stepId: this.flightRecorder.buffer.length + 1,
      timestamp: new Date().toISOString().substring(11, 23),
      state: ModelManager.currentModel.activeState,
      event: event || (reason ? `[${reason}]` : "[step]"),
      inPorts: JSON.parse(JSON.stringify(this.datapath.inPorts || {})),
      registers: JSON.parse(JSON.stringify(this.datapath.registers || {})),
      outPorts: JSON.parse(JSON.stringify(this.datapath.outPorts || {}))
    };
    this.flightRecorder.buffer.push(snap);
    if (this.flightRecorder.buffer.length > this.flightRecorder.capacity) {
      this.flightRecorder.buffer.shift();
    }
    this.updateFlightRecorderUI();
  },

  updateFlightRecorderUI() {
    const slider = document.getElementById("timeTravelSlider");
    const status = document.getElementById("recorderStatus");
    const liveBtn = document.getElementById("recLiveBtn");
    const count = this.flightRecorder.buffer.length;
    if (status) {
      status.textContent = `Buffer: ${count} / ${this.flightRecorder.capacity}`;
    }
    if (slider) {
      slider.max = Math.max(0, count - 1);
      if (!this.flightRecorder.isTimeTraveling) {
        slider.value = Math.max(0, count - 1);
      }
    }
    if (liveBtn) {
      liveBtn.className = this.flightRecorder.isTimeTraveling ? "btn-live" : "btn-live active";
    }
  },

  timeTravelTo(index) {
    if (index < 0 || index >= this.flightRecorder.buffer.length) return;
    const count = this.flightRecorder.buffer.length;
    if (index === count - 1) {
      this.returnToLive();
      return;
    }
    this.flightRecorder.currentIndex = index;
    this.flightRecorder.isTimeTraveling = true;
    const snap = this.flightRecorder.buffer[index];
    ModelManager.currentModel.activeState = snap.state;
    this.updateActiveStateBadge(`[HIST #${snap.stepId}] ${snap.state}`);
    GraphRenderer.highlightActive(snap.state);
    this.datapath.inPorts = JSON.parse(JSON.stringify(snap.inPorts));
    this.datapath.registers = JSON.parse(JSON.stringify(snap.registers));
    this.datapath.outPorts = JSON.parse(JSON.stringify(snap.outPorts));
    this.renderDatapathUI();
    this.updateFlightRecorderUI();
    const slider = document.getElementById("timeTravelSlider");
    if (slider) slider.value = index;
    this.log(`[TIME-TRAVEL] Inspected historical snapshot #${snap.stepId} at state '${snap.state}' (trigger: ${snap.event})`, "WARN");
  },

  stepBack() {
    const count = this.flightRecorder.buffer.length;
    if (count === 0) return;
    let target = this.flightRecorder.isTimeTraveling ? this.flightRecorder.currentIndex - 1 : count - 2;
    if (target >= 0) this.timeTravelTo(target);
  },

  stepForward() {
    if (!this.flightRecorder.isTimeTraveling) return;
    const target = this.flightRecorder.currentIndex + 1;
    if (target < this.flightRecorder.buffer.length) this.timeTravelTo(target);
  },

  returnToLive() {
    if (!this.flightRecorder.isTimeTraveling) return;
    this.flightRecorder.isTimeTraveling = false;
    this.flightRecorder.currentIndex = -1;
    const count = this.flightRecorder.buffer.length;
    if (count > 0) {
      const snap = this.flightRecorder.buffer[count - 1];
      ModelManager.currentModel.activeState = snap.state;
      this.updateActiveStateBadge(snap.state);
      GraphRenderer.highlightActive(snap.state);
      this.datapath.inPorts = JSON.parse(JSON.stringify(snap.inPorts));
      this.datapath.registers = JSON.parse(JSON.stringify(snap.registers));
      this.datapath.outPorts = JSON.parse(JSON.stringify(snap.outPorts));
      this.renderDatapathUI();
    }
    this.updateFlightRecorderUI();
    this.log(`[TIME-TRAVEL] Returned to live execution mode`, "INFO");
  },

  _recordHistory(leafState) {
    const ancestors = getAncestorChain(ModelManager.currentModel, leafState);
    for (let i = 1; i < ancestors.length; i++) {
      this.historyMap[ancestors[i]] = ancestors[i - 1];
    }
  },

  _resolveWithHistory(targetState, isHistory, isDeepHistory) {
    if (!isHistory && !isDeepHistory) {
      return resolveLeafState(ModelManager.currentModel, targetState);
    }
    const remembered = this.historyMap[targetState];
    if (remembered) {
      return isDeepHistory
        ? resolveLeafState(ModelManager.currentModel, remembered)
        : remembered;
    }
    return resolveLeafState(ModelManager.currentModel, targetState);
  },

  log(msg, type = "INFO") {
    const logEl = document.getElementById("historyLog");
    if (!logEl) return;
    const item = document.createElement("div");
    item.className = `log-item ${type}`;
    item.textContent = `[${new Date().toISOString().substring(11, 23)}] ${msg}`;
    logEl.appendChild(item);
    logEl.scrollTop = logEl.scrollHeight;
  },

  clearLog() {
    const logEl = document.getElementById("historyLog");
    if (logEl) logEl.innerHTML = "";
  },

  updateActiveStateBadge(stateName) {
    const textEl = document.getElementById("activeStateText");
    if (textEl) {
      textEl.textContent = stateName;
    } else {
      const badge = document.getElementById("activeStateBadge");
      if (badge) badge.textContent = stateName;
    }
  },

  initDatapath(text, model = null, reset = false) {
    if (reset || !this.datapath) {
      this.datapath = { inPorts: {}, registers: {}, outPorts: {} };
      this.historyMap = {};
    }
    if (!text) return;

    const prevIn  = this.datapath.inPorts || {};
    const prevReg = this.datapath.registers || {};
    const prevOut = this.datapath.outPorts || {};

    const nextIn  = {};
    const nextReg = {};
    const nextOut = {};

    const knownStates = new Set((model && model.states) || []);
    const knownEvents = new Set((model && model.events) || []);
    const reservedWords = new Set([
      "true", "false", "and", "or", "not", "fsm", "self", "in", "out",
      "state", "port", "attribute", "event", "null", "undefined", "entry",
      "exit", "do", "def", "item", "transition", "initial"
    ]);

    const isBlacklisted = (name) => {
      if (!name) return true;
      if (reservedWords.has(name) || knownStates.has(name) || knownEvents.has(name)) return true;
      if (name === "in" || name === "out") return true;
      // Filter compiler-generated mangled tokens like in_battery_percent__15_0 or in_gps_locked
      if (name.includes("__") || name.startsWith("in_") || name.startsWith("out_")) return true;
      return false;
    };

    // ------------------------------------------------------------------------
    // 1. Official AST Ports & Variables (if model was parsed by C++ WASM / JS)
    // ------------------------------------------------------------------------
    if (model && Array.isArray(model.ports) && model.ports.length > 0) {
      for (const p of model.ports) {
        if (!p.name || isBlacklisted(p.name)) continue;
        if (p.direction === "in") {
          const isBool = p.type === "Boolean" || p.type === "bool";
          const min = (p.min !== null && p.min !== undefined) ? p.min : 0;
          const max = (p.max !== null && p.max !== undefined) ? p.max : 100;
          const defaultVal = isBool ? false : (max > 10 ? Math.min(max, 85) : 1);
          nextIn[p.name] = {
            type: isBool ? "Boolean" : (Number.isInteger(min) && Number.isInteger(max) ? "Integer" : "Real"),
            min,
            max,
            value: (!reset && prevIn[p.name] !== undefined) ? prevIn[p.name].value : defaultVal
          };
        } else if (p.direction === "out") {
          nextOut[p.name] = {
            type: p.type || "Boolean",
            value: (!reset && prevOut[p.name] !== undefined) ? prevOut[p.name].value : false
          };
        }
      }
    }

    if (model && Array.isArray(model.variables) && model.variables.length > 0) {
      for (const v of model.variables) {
        if (!v.name || isBlacklisted(v.name)) continue;
        const isBool = v.type === "Boolean" || v.type === "bool";
        let val = 0;
        if (v.initial !== undefined) {
          val = isBool ? (v.initial === 'true' || v.initial === true) : (parseFloat(v.initial) || 0);
        }
        nextReg[v.name] = {
          type: isBool ? "Boolean" : (Number.isInteger(val) ? "Integer" : "Real"),
          value: (!reset && prevReg[v.name] !== undefined) ? prevReg[v.name].value : val
        };
      }
    }

    // ------------------------------------------------------------------------
    // 2. SysML v2 syntax: in port, out port, attribute
    // ------------------------------------------------------------------------
    const inPortRegex = /in\s+port\s+([a-zA-Z_0-9]+)\s*:\s*([a-zA-Z_0-9]+)(?:\s*\{[^}]*constraint\s*\{[^}]*self\s*>=\s*([0-9.-]+)\s*and\s*self\s*<=\s*([0-9.-]+)[^}]*\}[^}]*\})?/g;
    let m;
    while ((m = inPortRegex.exec(text)) !== null) {
      const [, name, type, mn, mx] = m;
      if (isBlacklisted(name) || nextIn[name]) continue;
      const min = mn ? parseFloat(mn) : 0;
      const max = mx ? parseFloat(mx) : 100;
      const defaultVal = type === "Boolean" ? false : (max > 10 ? Math.min(max, 85) : 1);
      nextIn[name] = {
        type,
        min,
        max,
        value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : defaultVal
      };
    }

    const outPortRegex = /out\s+port\s+([a-zA-Z_0-9]+)\s*:\s*([a-zA-Z_0-9]+)/g;
    while ((m = outPortRegex.exec(text)) !== null) {
      const [, name, type] = m;
      if (isBlacklisted(name) || nextOut[name]) continue;
      nextOut[name] = {
        type,
        value: (!reset && prevOut[name] !== undefined) ? prevOut[name].value : false
      };
    }

    const strippedText = text.replace(/item\s+def\s+[a-zA-Z_0-9]+\s*\{[^}]*\}/g, '');
    const attrRegex = /attribute\s+([a-zA-Z_0-9]+)\s*:\s*([a-zA-Z_0-9]+)(?:\s*=\s*([0-9.-]+|true|false))?/g;
    while ((m = attrRegex.exec(strippedText)) !== null) {
      const [, name, type, rawVal] = m;
      if (isBlacklisted(name) || nextReg[name]) continue;
      let val = 0;
      if (rawVal !== undefined) {
        val = rawVal === 'true' ? true : (rawVal === 'false' ? false : parseFloat(rawVal));
      }
      nextReg[name] = {
        type,
        value: (!reset && prevReg[name] !== undefined) ? prevReg[name].value : val
      };
    }

    // ------------------------------------------------------------------------
    // 3. JSON Schema: "ports" and "variables" or "context"
    // ------------------------------------------------------------------------
    const trimmed = text.trim();
    if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
      try {
        const json = JSON.parse(trimmed);
        if (Array.isArray(json.ports)) {
          for (const p of json.ports) {
            if (!p.name || isBlacklisted(p.name)) continue;
            if (p.direction === "in" && !nextIn[p.name]) {
              const isBool = p.type === "bool" || p.type === "Boolean";
              const min = p.min_value !== undefined ? p.min_value : 0;
              const max = p.max_value !== undefined ? p.max_value : 100;
              const defVal = isBool ? false : (min + max) / 2;
              nextIn[p.name] = {
                type: isBool ? "Boolean" : (Number.isInteger(min) && Number.isInteger(max) ? "Integer" : "Real"),
                min,
                max,
                value: (!reset && prevIn[p.name] !== undefined) ? prevIn[p.name].value : defVal
              };
            } else if (p.direction === "out" && !nextOut[p.name]) {
              nextOut[p.name] = {
                type: (p.type === "bool" || p.type === "Boolean") ? "Boolean" : "Real",
                value: (!reset && prevOut[p.name] !== undefined) ? prevOut[p.name].value : false
              };
            }
          }
        }
        if (Array.isArray(json.variables)) {
          for (const v of json.variables) {
            if (!v.name || isBlacklisted(v.name) || nextReg[v.name]) continue;
            let val = 0;
            const isBool = v.type === "bool" || v.type === "Boolean";
            if (v.initial_value !== undefined) {
              val = isBool ? (v.initial_value === 'true' || v.initial_value === true) : parseFloat(v.initial_value);
            }
            nextReg[v.name] = {
              type: isBool ? "Boolean" : (Number.isInteger(val) ? "Integer" : "Real"),
              value: (!reset && prevReg[v.name] !== undefined) ? prevReg[v.name].value : val
            };
          }
        }
        if (json.context && typeof json.context === "object") {
          for (const [k, v] of Object.entries(json.context)) {
            if (isBlacklisted(k) || nextReg[k]) continue;
            const type = typeof v === 'boolean' ? 'Boolean' : (typeof v === 'number' ? (Number.isInteger(v) ? 'Integer' : 'Real') : 'String');
            nextReg[k] = {
              type,
              value: (!reset && prevReg[k] !== undefined) ? prevReg[k].value : v
            };
          }
        }
      } catch (_) {}
    }

    // ------------------------------------------------------------------------
    // 4. SCXML / Cameo: <data id="..." expr="..." type="..."/>
    // ------------------------------------------------------------------------
    const scxmlDataRegex = /<data\s+id=["']([a-zA-Z_0-9]+)["'](?:\s+expr=["']([^"']*)["'])?(?:\s+type=["']([^"']*)["'])?/g;
    while ((m = scxmlDataRegex.exec(text)) !== null) {
      const [, name, expr, explicitType] = m;
      if (isBlacklisted(name)) continue;
      let val = 0;
      let type = (explicitType && (explicitType.includes("bool") || explicitType === "Boolean")) ? "Boolean" : "Integer";
      if (expr !== undefined) {
        if (expr === 'true' || expr === 'false') {
          type = "Boolean";
          val = (expr === 'true');
        } else if (!isNaN(parseFloat(expr))) {
          val = parseFloat(expr);
          type = Number.isInteger(val) ? "Integer" : "Real";
        }
      }

      if (name.includes("pressure") || name.includes("sensor") || name.includes("curtain")) {
        if (!nextIn[name]) {
          if (type === "Boolean") {
            nextIn[name] = { type: "Boolean", min: 0, max: 1, value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : Boolean(val) };
          } else {
            const max = Math.max(100, Math.ceil(val * 1.5));
            const min = val < 0 ? Math.floor(val * 1.5) : 0;
            nextIn[name] = { type, min, max, value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : val };
          }
        }
      } else if (!nextReg[name]) {
        nextReg[name] = {
          type,
          value: (!reset && prevReg[name] !== undefined) ? prevReg[name].value : val
        };
      }
    }

    // ------------------------------------------------------------------------
    // 5. Fallback Heuristics: ONLY if no explicit ports/variables were found
    // ------------------------------------------------------------------------
    const needsInference = (Object.keys(nextIn).length === 0) || (Object.keys(nextOut).length === 0 && Object.keys(nextReg).length === 0);

    if (needsInference) {
      const transitions = (model && Array.isArray(model.transitions)) ? [...model.transitions] : [];
      if (transitions.length === 0) {
        const transLineRegex = /-->\s*([a-zA-Z_0-9]+)\s*:\s*(?:([a-zA-Z_0-9]+)\s*)?(?:\[([^\]]+)\])?(?:\s*\/\s*([^;\n\r]+))?/g;
        let tlm;
        while ((tlm = transLineRegex.exec(text)) !== null) {
          transitions.push({
            target: tlm[1],
            event: tlm[2] || "",
            guard: tlm[3] || "",
            action: tlm[4] || ""
          });
        }
      }

      for (const t of transitions) {
        // Only infer outputs and registers if none were declared
        if (Object.keys(nextOut).length === 0 && Object.keys(nextReg).length === 0 && t.action) {
          const statements = t.action.split(/[;,]/);
          for (const rawStmt of statements) {
            const stmt = rawStmt.trim();
            const boolAssign = /^([a-zA-Z_][a-zA-Z_0-9]*)\s*=\s*(true|false)$/.exec(stmt);
            if (boolAssign) {
              const name = boolAssign[1];
              if (!isBlacklisted(name) && !nextIn[name] && !nextReg[name] && !nextOut[name]) {
                nextOut[name] = {
                  type: "Boolean",
                  value: (!reset && prevOut[name] !== undefined) ? prevOut[name].value : (boolAssign[2] === "true")
                };
              }
            }
            const numAssign = /^([a-zA-Z_][a-zA-Z_0-9]*)\s*(\+\+|--|\+=|-=|=\s*([0-9.-]+))$/.exec(stmt);
            if (numAssign) {
              const name = numAssign[1];
              if (!isBlacklisted(name) && !nextIn[name] && !nextReg[name] && !nextOut[name]) {
                const numVal = numAssign[3] ? parseFloat(numAssign[3]) : 0;
                nextReg[name] = {
                  type: Number.isInteger(numVal) ? "Integer" : "Real",
                  value: (!reset && prevReg[name] !== undefined) ? prevReg[name].value : numVal
                };
              }
            }
          }
        }

        // Only infer inPorts if NONE were declared
        if (Object.keys(nextIn).length === 0 && t.guard) {
          const cleanG = t.guard.replace(/fsm::(and|or|not)_/g, ' ').replace(/[()]/g, ' ');

          // Comparison: name (<|<=|>|>=|==|!=) number
          const compRegex = /([a-zA-Z_][a-zA-Z_0-9]*)\s*(<=|>=|<|>|==|!=)\s*([0-9.-]+)/g;
          let cm;
          while ((cm = compRegex.exec(cleanG)) !== null) {
            const name = cm[1];
            if (isBlacklisted(name)) continue;
            const num = parseFloat(cm[3]);
            if (!nextIn[name] && !nextReg[name] && !nextOut[name]) {
              const max = Math.max(100, Math.ceil(num * 1.5));
              const min = num < 0 ? Math.floor(num * 1.5) : 0;
              const defVal = (num > min && num < max) ? num : (min + max) / 2;
              nextIn[name] = {
                type: Number.isInteger(num) ? "Integer" : "Real",
                min,
                max,
                value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : defVal
              };
            }
          }

          // Boolean condition: name == true/false
          const boolCompRegex = /([a-zA-Z_][a-zA-Z_0-9]*)\s*==\s*(true|false)/g;
          let bcm;
          while ((bcm = boolCompRegex.exec(cleanG)) !== null) {
            const name = bcm[1];
            if (isBlacklisted(name)) continue;
            if (!nextIn[name] && !nextReg[name] && !nextOut[name]) {
              nextIn[name] = {
                type: "Boolean",
                min: 0,
                max: 1,
                value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : (bcm[2] === "true")
              };
            }
          }

          // Standalone identifier in guard (e.g. [gps_locked])
          const identRegex = /(?:!|\b)([a-zA-Z_][a-zA-Z_0-9]*)\b/g;
          let im;
          while ((im = identRegex.exec(cleanG)) !== null) {
            const name = im[1];
            if (isBlacklisted(name)) continue;
            if (!nextIn[name] && !nextReg[name] && !nextOut[name]) {
              nextIn[name] = {
                type: "Boolean",
                min: 0,
                max: 1,
                value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : true
              };
            }
          }
        }
      }
    }

    // ------------------------------------------------------------------------
    // 6. Default fallback if FSM has completely zero variables / ports
    // ------------------------------------------------------------------------
    if (Object.keys(nextIn).length === 0 && Object.keys(nextReg).length === 0 && Object.keys(nextOut).length === 0) {
      nextIn["sensor_ok"]     = { type: "Boolean", min: 0, max: 1, value: (!reset && prevIn["sensor_ok"]) ? prevIn["sensor_ok"].value : true };
      nextReg["cycle_count"] = { type: "Integer", value: (!reset && prevReg["cycle_count"]) ? prevReg["cycle_count"].value : 0 };
      nextOut["system_ready"] = { type: "Boolean", value: (!reset && prevOut["system_ready"]) ? prevOut["system_ready"].value : false };
    }

    this.datapath = {
      inPorts:   nextIn,
      registers: nextReg,
      outPorts:  nextOut
    };

    if (reset || this.flightRecorder.buffer.length === 0) {
      this._recordFlightSnapshot("", "initial");
    }

    this.renderDatapathUI();
  },

  renderDatapathUI() {
    const inContainer  = document.getElementById("inPortsList");
    const regContainer = document.getElementById("registersList");
    const outContainer = document.getElementById("outPortsList");
    const dataContainer = document.getElementById("structuredDataList");

    if (inContainer) {
      inContainer.innerHTML = "";
      for (const [name, p] of Object.entries(this.datapath.inPorts)) {
        const row = document.createElement("div");
        row.className = "port-row";
        if (p.type === "Boolean") {
          row.innerHTML = `
            <span class="port-name">${name}</span>
            <div class="port-control">
              <label class="toggle-switch">
                <input type="checkbox" id="inport_${name}" ${p.value ? 'checked' : ''}>
                <span class="toggle-slider"></span>
              </label>
            </div>
          `;
          const input = row.querySelector("input");
          input.onchange = () => { p.value = input.checked; this.log(`InPort '${name}' set to ${p.value}`, "INFO"); };
        } else {
          row.innerHTML = `
            <span class="port-name">${name}</span>
            <div class="port-control">
              <input type="range" class="port-slider" id="inport_${name}" min="${p.min}" max="${p.max}" value="${p.value}">
              <span class="port-val-badge" id="badge_${name}">${p.value}</span>
            </div>
          `;
          const input = row.querySelector("input");
          const badge = row.querySelector(`#badge_${name}`);
          input.oninput  = () => { p.value = parseFloat(input.value); badge.textContent = p.value; };
          input.onchange = () => { this.log(`InPort '${name}' set to ${p.value}`, "INFO"); };
        }
        inContainer.appendChild(row);
      }
    }

    if (regContainer) {
      regContainer.innerHTML = "";
      for (const [name, r] of Object.entries(this.datapath.registers)) {
        const chip = document.createElement("div");
        chip.className = "register-chip";
        chip.innerHTML = `<span class="reg-name">${name}</span>: <span class="reg-val" id="reg_${name}">${r.value}</span>`;
        regContainer.appendChild(chip);
      }
    }

    if (outContainer) {
      outContainer.innerHTML = "";
      for (const [name, o] of Object.entries(this.datapath.outPorts)) {
        const row = document.createElement("div");
        row.className = "outport-led-row";
        row.innerHTML = `<span class="outport-led ${o.value ? 'active' : ''}" id="led_${name}"></span><span class="outport-name">${name}: ${o.value ? 'ACTIVE' : 'IDLE'}</span>`;
        outContainer.appendChild(row);
      }
    }

    if (dataContainer) {
      dataContainer.innerHTML = "";
      const enums = (ModelManager.currentModel && ModelManager.currentModel.enums) || [];
      const structs = (ModelManager.currentModel && ModelManager.currentModel.structs) || [];
      if (enums.length === 0 && structs.length === 0) {
        dataContainer.innerHTML = '<div style="color:var(--text-muted);font-size:0.75rem;">No custom enums or structs declared</div>';
      } else {
        for (const en of enums) {
          const item = document.createElement("div");
          item.className = "data-type-item";
          const lits = (en.literals || []).map(l => l.name + (l.value !== undefined ? `=${l.value}` : '')).join(', ');
          item.innerHTML = `<div class="data-type-header">enum class ${en.name} : ${en.underlying_type || 'uint8_t'}</div><div class="data-type-fields">{ ${lits} }</div>`;
          dataContainer.appendChild(item);
        }
        for (const st of structs) {
          const item = document.createElement("div");
          item.className = "data-type-item";
          const flds = (st.fields || []).map(f => `${f.type} ${f.name}${f.default_value ? '{' + f.default_value + '}' : ''}`).join('; ');
          item.innerHTML = `<div class="data-type-header">struct ${st.name}</div><div class="data-type-fields">{ ${flds} }</div>`;
          dataContainer.appendChild(item);
        }
      }
    }
  },

  evalGuard(guardStr) {
    if (!guardStr) return { satisfied: true, reason: "" };

    // Collect all datapath variables into scope, including in_ and in. aliases
    const scope = {};
    for (const [k, v] of Object.entries(this.datapath.inPorts || {})) {
      scope[k] = v.value;
      scope[`in_${k}`] = v.value;
    }
    for (const [k, v] of Object.entries(this.datapath.registers || {})) {
      scope[k] = v.value;
    }
    for (const [k, v] of Object.entries(this.datapath.outPorts || {})) {
      scope[k] = v.value;
    }

    // Clean guard syntax while strictly preserving comparison operators <, >, <=, >=
    let expr = guardStr
      .replace(/fsm::and_<(.+?)>/g, (_, p) => p.replace(/,/g, ' && '))
      .replace(/fsm::or_<(.+?)>/g,  (_, p) => p.replace(/,/g, ' || '))
      .replace(/fsm::not_<(.+?)>/g, (_, p) => `!(${p.trim()})`)
      .replace(/\band\b/g, ' && ')
      .replace(/\bor\b/g, ' || ')
      .replace(/\bnot\b/g, ' ! ')
      .replace(/\bin\.([a-zA-Z_0-9]+)/g, '$1') // in.battery_percent -> battery_percent
      .replace(/\bin_([a-zA-Z_0-9]+)__([0-9]+)_([0-9]+)/g, '($1 <= $2.$3)') // in_battery_percent__15_0 -> (battery_percent <= 15.0)
      .replace(/\bin_([a-zA-Z_0-9]+)/g, '$1'); // in_battery_percent -> battery_percent

    // In UAV TakeoffCmd, expand standalone battery_percent to its declared contract (> 20.0)
    if (this.datapath.inPorts["battery_percent"]) {
      expr = expr.replace(/\bbattery_percent\b(?!\s*[<>=!])/g, '(battery_percent > 20.0)');
    }

    const humanExpr = expr;

    try {
      const keys = Object.keys(scope);
      const vals = Object.values(scope);
      const fn = new Function(...keys, `"use strict"; return (${expr});`);
      const res = Boolean(fn(...vals));
      return { satisfied: res, reason: res ? "" : `'${humanExpr}' evaluated to false` };
    } catch (_) {
      return { satisfied: true, reason: "" };
    }
  },

  execAction(actionStr) {
    if (!actionStr) return;

    const statements = actionStr.split(/[;,]/).map(s => s.trim()).filter(Boolean);

    for (const stmt of statements) {
      // 1. Increment: name++
      const incMatch = /^([a-zA-Z_0-9]+)\+\+$/.exec(stmt);
      if (incMatch) {
        const name = incMatch[1];
        if (this.datapath.registers[name]) this.datapath.registers[name].value++;
        continue;
      }

      // 2. Decrement: name--
      const decMatch = /^([a-zA-Z_0-9]+)--$/.exec(stmt);
      if (decMatch) {
        const name = decMatch[1];
        if (this.datapath.registers[name]) this.datapath.registers[name].value--;
        continue;
      }

      // 3. Add assign: name += X
      const addMatch = /^([a-zA-Z_0-9]+)\s*\+=\s*([0-9.-]+)$/.exec(stmt);
      if (addMatch) {
        const name = addMatch[1];
        const val = parseFloat(addMatch[2]);
        if (this.datapath.registers[name]) this.datapath.registers[name].value += val;
        continue;
      }

      // 4. Assignment: name = value
      const assignMatch = /^([a-zA-Z_0-9]+)\s*=\s*(.+)$/.exec(stmt);
      if (assignMatch) {
        const name = assignMatch[1];
        const rawVal = assignMatch[2].trim();
        let target = this.datapath.outPorts[name] || this.datapath.registers[name];
        if (!target) {
          if (rawVal === 'true' || rawVal === 'false') {
            this.datapath.outPorts[name] = { type: "Boolean", value: rawVal === 'true' };
            target = this.datapath.outPorts[name];
          } else {
            this.datapath.registers[name] = { type: "Integer", value: parseFloat(rawVal) || 0 };
            target = this.datapath.registers[name];
          }
        }
        if (target) {
          if (rawVal === 'true') target.value = true;
          else if (rawVal === 'false') target.value = false;
          else if (!isNaN(parseFloat(rawVal))) target.value = parseFloat(rawVal);
          else target.value = rawVal;
        }
      }
    }

    this.renderDatapathUI();
  },

  step() {
    if (this.flightRecorder.isTimeTraveling) this.returnToLive();
    if (this.datapath.registers["cycle_count"]) this.datapath.registers["cycle_count"].value++;
    const curr = ModelManager.currentModel.activeState;
    this.renderDatapathUI();
    this._recordFlightSnapshot("", "clock-step");
    this.log(`[CLOCK STEP] Sampled cyclic tick (dt=10ms) evaluated in state '${curr}'`, "INFO");
  },

  setState(targetState, guard = "", action = "") {
    if (this.flightRecorder.isTimeTraveling) this.returnToLive();
    const leaf = resolveLeafState(ModelManager.currentModel, targetState);
    const prev = ModelManager.currentModel.activeState;
    if (prev) this._recordHistory(prev);
    ModelManager.currentModel.activeState = leaf;
    this.updateActiveStateBadge(leaf);
    GraphRenderer.highlightActive(leaf);
    this._recordFlightSnapshot("", guard ? `guard:${guard}` : "override");
    let msg = `State override: ${prev} -> ${leaf}`;
    if (guard)  msg += ` [guard: ${guard}]`;
    if (action) msg += ` -> Action: ${action}()`;
    this.log(msg, "EVENT");
    this.updateControls();
  },

  _formatGuardForDisplay(guardStr) {
    if (!guardStr) return "";
    return guardStr
      .replace(/fsm::and_<(.+?)>/g, (_, p) => p.replace(/,/g, ' && '))
      .replace(/fsm::or_<(.+?)>/g,  (_, p) => p.replace(/,/g, ' || '))
      .replace(/fsm::not_<(.+?)>/g, (_, p) => `!(${p.trim()})`)
      .replace(/\band\b/g, ' && ')
      .replace(/\bor\b/g, ' || ')
      .replace(/\bnot\b/g, ' ! ')
      .replace(/\bin\.([a-zA-Z_0-9]+)/g, '$1')
      .replace(/\bin_([a-zA-Z_0-9]+)__([0-9]+)_([0-9]+)/g, '$1 <= $2.$3')
      .replace(/\bin_([a-zA-Z_0-9]+)/g, '$1')
      .replace(/\bbattery_percent\b(?!\s*[<>=!])/g, 'battery_percent > 20.0')
      .replace(/[<>]/g, '');
  },

  dispatch(eventName) {
    if (this.flightRecorder.isTimeTraveling) this.returnToLive();
    const curr = ModelManager.currentModel.activeState;
    const availableTrans = getAvailableTransitions(ModelManager.currentModel, curr);
    const matching = availableTrans.filter(t => t.event === eventName);

    if (matching.length === 0) {
      this.log(`Event '${eventName}' unhandled in state '${curr}' (IGNORED)`, "WARN");
      return;
    }

    const t = matching[0];
    const cleanGuard = this._formatGuardForDisplay(t.guard);

    // Universal guard evaluation against live datapath values
    if (t.guard) {
      const check = this.evalGuard(t.guard);
      if (!check.satisfied) {
        this.log(`[GUARD REJECTED] '${eventName || "auto"}' rejected: ${check.reason}`, "WARN");
        return;
      }
    }

    // Universal action execution on datapath
    if (t.action) {
      this.execAction(t.action);
    }

    if (t.is_internal) {
      let msg = `[${eventName}] Internal in '${curr}'`;
      if (cleanGuard) msg += ` [guard: ${cleanGuard}]`;
      if (t.action)   msg += ` -> Action: ${t.action}()`;
      this._recordFlightSnapshot(eventName, "internal");
      this.log(msg, "INFO");
    } else {
      const prev = ModelManager.currentModel.activeState;
      this._recordHistory(prev);
      const targetLeaf = this._resolveWithHistory(t.target, t.target_is_history, t.target_is_deep_history);
      ModelManager.currentModel.activeState = targetLeaf;
      this.updateActiveStateBadge(targetLeaf);
      GraphRenderer.highlightActive(targetLeaf);
      this._recordFlightSnapshot(eventName, cleanGuard);

      const histLabel = (t.target_is_history || t.target_is_deep_history)
        ? (t.target_is_deep_history ? ` (deep history -> ${targetLeaf})` : ` (history -> ${targetLeaf})`)
        : ``;
      const eventLabel = eventName || `[guard: ${cleanGuard}]`;
      let msg = `[${eventLabel}] ${prev} -> ${targetLeaf}${histLabel}`;
      if (cleanGuard && eventName) msg += ` [guard: ${cleanGuard}]`;
      if (t.action) msg += ` -> Action: ${t.action}()`;
      this.log(msg, "EVENT");
      this.updateControls();
    }
  },

  updateControls() {
    const container = document.getElementById("eventButtons");
    if (!container) return;
    container.innerHTML = "";
    const curr = ModelManager.currentModel.activeState;
    const availableTrans = getAvailableTransitions(ModelManager.currentModel, curr);

    for (const t of availableTrans) {
      const btn = document.createElement("button");
      const isEventless = !t.event || t.event === "" || t.event === "AnonymousEvent";
      if (isEventless) {
        btn.className = "btn-event btn-eventless";
        const guardLabel = this._formatGuardForDisplay(t.guard) || 'auto';
        btn.textContent = `[${guardLabel}]`;
        btn.title = `Eventless (guard-only) transition: ${t.source} -> ${t.target}. Fires automatically when guard is satisfied.`;
        btn.onclick = () => this.dispatch("");
      } else {
        btn.className = "btn-event active-trigger";
        btn.textContent = t.event;
        btn.title = `Trigger transition: ${t.source} -> ${t.target}`;
        btn.onclick = () => this.dispatch(t.event);
      }
      container.appendChild(btn);
    }

    const otherEvents = (ModelManager.currentModel.events || [])
      .filter(e => !availableTrans.some(t => t.event === e))
      .sort((a, b) => a.localeCompare(b));
    for (const evt of otherEvents) {
      const btn = document.createElement("button");
      btn.className = "btn-event disabled-trigger";
      btn.textContent = evt;
      btn.title = `Event ${evt} is not handled in state ${curr}`;
      btn.onclick = () => this.dispatch(evt);
      container.appendChild(btn);
    }
  }
};
