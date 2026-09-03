/**
 * fsmc Playground — Serializers
 * Each function takes a canonical model object and returns a diagram string
 * in the target format. No DOM, no WASM deps.
 */

const ANON = new Set(["Anonymous", "AnonymousEvent", "anonymous"]);

/** True if event name represents an anonymous/unnamed event */
function isAnon(evt) { return !evt || ANON.has(evt); }

/**
 * Canonical history suffix for a transition target.
 * @param {object} t - transition object
 * @returns {string}
 */
function histSuffix(t) {
  if (t.target_is_deep_history) return '[H*]';
  if (t.target_is_history)      return '[H]';
  return '';
}

/** Effective target name, including history suffix if applicable */
function effectiveTarget(t) {
  return (t.is_internal ? t.source : t.target) + (t.is_internal ? '' : histSuffix(t));
}

/** Build a transition label: Event [Guard] / Action */
function buildLabel(t) {
  let label = !isAnon(t.event) ? t.event : "";
  if (t.guard)  label += (label ? " " : "") + `[${t.guard}]`;
  if (t.action) label += (label ? " " : "") + `/ ${t.action}`;
  return label;
}

// ---------------------------------------------------------------------------

/**
 * Serialize model to Mermaid stateDiagram-v2.
 * @param {object} model
 * @returns {string}
 */
export function toMermaid(model) {
  let out = "";
  if (model.name && model.name !== "GeneratedFSM" && model.name !== "MyStateMachine") {
    out += `--- title: ${model.name} ---\n`;
  }
  out += "stateDiagram-v2\n";
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `%% @fsm:signal ${ev}\n`;
  }
  if (model.initialState) out += `    [*] --> ${model.initialState}\n`;
  for (const t of model.transitions) {
    const label = buildLabel(t);
    const lblStr = label ? ` : ${label}` : "";
    out += `    ${t.source} --> ${effectiveTarget(t)}${lblStr}\n`;
  }
  return out.trim();
}

/**
 * Serialize model to PlantUML (@startuml).
 * @param {object} model
 * @returns {string}
 */
export function toPlantUml(model) {
  const header = (model.name && model.name !== "GeneratedFSM" && model.name !== "MyStateMachine")
    ? `@startuml ${model.name}\n`
    : "@startuml\n";
  let out = header;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `' @fsm:signal ${ev}\n`;
  }
  if (model.initialState) out += `[*] --> ${model.initialState}\n\n`;
  for (const t of model.transitions) {
    const label = buildLabel(t);
    const lblStr = label ? ` : ${label}` : "";
    out += `${t.source} --> ${effectiveTarget(t)}${lblStr}\n`;
  }
  return out + "@enduml";
}

/**
 * Serialize model to OMG SysML v2.
 * @param {object} model
 * @returns {string}
 */
export function toSysml2(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `state def ${fsmName} {\n`;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `    event def ${ev};\n`;
  }
  if (model.events?.length) out += "\n";
  if (model.initialState) out += `    initial state ${model.initialState};\n\n`;
  for (const s of model.states) out += `    state ${s};\n`;
  out += "\n";
  for (const t of model.transitions) {
    const tgt = t.is_internal ? t.source : (t.target + histSuffix(t));
    out += `    transition from ${t.source}`;
    if (!isAnon(t.event))  out += ` accept ${t.event}`;
    if (t.guard)   out += ` if ${t.guard}`;
    if (t.action)  out += ` do ${t.action}`;
    out += ` then ${tgt};\n`;
  }
  return out + "}";
}

/**
 * Serialize model to W3C SCXML.
 * @param {object} model
 * @returns {string}
 */
export function toScxml(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `<?xml version="1.0" encoding="UTF-8"?>\n`;
  out += `<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="${model.initialState}" name="${fsmName}">\n`;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `  <!-- @fsm:signal ${ev} -->\n`;
  }
  for (const s of model.states) {
    const transFromS = model.transitions.filter(t => t.source === s);
    if (transFromS.length === 0) {
      out += `  <state id="${s}"/>\n`;
    } else {
      out += `  <state id="${s}">\n`;
      for (const t of transFromS) {
        out += `    <transition`;
        if (!isAnon(t.event)) out += ` event="${t.event}"`;
        if (t.guard)  out += ` cond="${t.guard.replace(/&/g, '&amp;')}"`;
        if (t.action) out += ` action="${t.action}"`;
        if (t.target && !t.is_internal) out += ` target="${t.target}"`;
        out += `/>\n`;
      }
      out += `  </state>\n`;
    }
  }
  return out + "</scxml>";
}

/**
 * Serialize model to OMG XMI 2.1 (Cameo / MagicDraw).
 * @param {object} model
 * @returns {string}
 */
export function toCameo(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `<?xml version="1.0" encoding="UTF-8"?>\n`;
  out += `<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">\n`;
  out += `  <uml:Model xmi:id="_m1" name="${fsmName}Model">\n`;
  out += `    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="${fsmName}">\n`;
  out += `      <region xmi:id="_r1">\n`;
  out += `        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps1" kind="initial"/>\n`;
  let sIdx = 1;
  const idMap = new Map();
  for (const s of model.states) {
    const id = `_s${sIdx++}`;
    idMap.set(s, id);
    out += `        <subvertex xmi:type="uml:State" xmi:id="${id}" name="${s}"/>\n`;
  }
  if (model.initialState && idMap.has(model.initialState)) {
    out += `        <transition xmi:id="_t0" source="_ps1" target="${idMap.get(model.initialState)}"/>\n`;
  }
  let tIdx = 1;
  for (const t of model.transitions) {
    const srcId = idMap.get(t.source) || t.source;
    const dstId = idMap.get(t.target) || t.target;
    out += `        <transition xmi:id="_t${tIdx++}" source="${srcId}" target="${dstId}"`;
    if (!isAnon(t.event)) out += ` trigger="${t.event}"`;
    if (t.guard)  out += ` guard="${t.guard.replace(/&/g, '&amp;')}"`;
    if (t.action) out += ` effect="${t.action}"`;
    out += `/>\n`;
  }
  return out + "      </region>\n    </packagedElement>\n  </uml:Model>\n</xmi:XMI>";
}

/**
 * Serialize model to XState JSON.
 * @param {object} model
 * @returns {string}
 */
export function toJson(model) {
  const fsmName = model.name || "GeneratedFSM";
  const obj = { id: fsmName, initial: model.initialState, states: {} };
  for (const s of model.states) obj.states[s] = { on: {} };
  for (const t of model.transitions) {
    if (!obj.states[t.source]) obj.states[t.source] = { on: {} };
    const transObj = { target: t.is_internal ? t.source : t.target };
    if (t.guard)  transObj.guard  = t.guard;
    if (t.action) transObj.action = t.action;
    const evtKey  = t.event || "Anonymous";
    const existing = obj.states[t.source].on[evtKey];
    if (existing) {
      obj.states[t.source].on[evtKey] = Array.isArray(existing) ? [...existing, transObj] : [existing, transObj];
    } else {
      obj.states[t.source].on[evtKey] = transObj;
    }
  }
  return JSON.stringify(obj, null, 2);
}

/**
 * Serialize model to Graphviz DOT.
 * @param {object} model
 * @returns {string}
 */
export function toDot(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `digraph ${fsmName} {\n`;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev) && ev !== "EVENT") out += `    // @fsm:signal ${ev}\n`;
  }
  out += `    __start__ [shape=point];\n`;
  if (model.initialState) out += `    __start__ -> ${model.initialState};\n`;
  for (const t of model.transitions) {
    let label = t.event || "";
    if (t.guard)  label += ` [${t.guard}]`;
    if (t.action) label += ` / ${t.action}`;
    const lblStr = label && label !== "Anonymous" ? ` [label="${label}"]` : "";
    out += `    ${t.source} -> ${t.is_internal ? t.source : t.target}${lblStr};\n`;
  }
  return out + "}";
}

/**
 * Fallback: serialize to the best available format for the model.
 * @param {object} model
 * @param {string} toFormat
 * @returns {string}
 */
export function serialize(model, toFormat) {
  switch (toFormat) {
    case 'mermaid':  return toMermaid(model);
    case 'plantuml': return toPlantUml(model);
    case 'sysml2':   return toSysml2(model);
    case 'scxml':    return toScxml(model);
    case 'cameo':    return toCameo(model);
    case 'json':     return toJson(model);
    case 'dot':      return toDot(model);
    default:         return toPlantUml(model);
  }
}
