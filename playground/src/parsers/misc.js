/**
 * fsmc Playground — Miscellaneous Format Parsers
 * JSON (XState), Graphviz DOT, and NuSMV/nuXmv SMV parsers.
 */

/**
 * Parse XState-style JSON.
 * @param {string} text
 * @returns {object}
 */
export function parseJson(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  try {
    const obj = JSON.parse(text);
    name = obj.id || "GeneratedFSM";
    initial = obj.initial || "";
    if (obj.states) {
      for (const [stName, stObj] of Object.entries(obj.states)) {
        states.add(stName);
        stateDetails.push({ name: stName, parent: "", is_composite: false });
        if (stObj.on) {
          for (const [evtName, targetVal] of Object.entries(stObj.on)) {
            events.add(evtName);
            const push = (target, guard, action) => {
              states.add(target);
              transitions.push({ source: stName, target, event: evtName, guard: guard || "", action: action || "", is_internal: stName === target });
            };
            if (Array.isArray(targetVal)) {
              for (const item of targetVal) {
                push(typeof item === 'string' ? item : item.target, item.guard || item.cond, item.action || item.actions);
              }
            } else if (typeof targetVal === 'string') {
              push(targetVal, "", "");
            } else if (targetVal && targetVal.target) {
              push(targetVal.target, targetVal.guard || targetVal.cond, targetVal.action || targetVal.actions);
            }
          }
        }
      }
    }
  } catch (_) {}

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

/**
 * Parse Graphviz DOT digraph.
 * @param {string} text
 * @returns {object}
 */
export function parseDot(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const nameMatch = text.match(/digraph\s+([A-Za-z0-9_]+)/);
  if (nameMatch) name = nameMatch[1];

  for (const raw of text.split('\n')) {
    const line = raw.trim();
    const sigMatch = line.match(/\/\/\s*@fsm:signal\s+([A-Za-z0-9_]+)/);
    if (sigMatch) { events.add(sigMatch[1]); continue; }
    if (!line.includes('->')) continue;

    const parts = line.split('->');
    const src = parts[0].replace(/;/g, '').trim();
    const rest = parts[1].split(';')[0].trim();
    const dst  = rest.split('[')[0].trim();
    let evt = "Anonymous", guard = "", action = "";

    const lblMatch = line.match(/label="([^"]+)"/);
    if (lblMatch) {
      const rawLbl = lblMatch[1];
      const evPart = rawLbl.split('[')[0].split('/')[0].trim();
      if (evPart) evt = evPart;
      const gM = rawLbl.match(/\[([^\]]+)\]/);
      if (gM) guard = gM[1];
      const aM = rawLbl.match(/\/([^"]+)/);
      if (aM) action = aM[1].trim();
    }

    if (src === '__start__' || src === '[*]') {
      initial = dst;
    } else {
      states.add(src); states.add(dst);
      if (evt !== "Anonymous") events.add(evt);
      transitions.push({ source: src, target: dst, event: evt, guard, action, is_internal: src === dst });
    }
  }

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

/**
 * Parse NuSMV / nuXmv SMV formal verification language.
 * @param {string} text
 * @returns {object}
 */
export function parseSmv(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const modMatch = text.match(/MODULE\s+([A-Za-z0-9_]+)/);
  if (modMatch && modMatch[1] !== 'main') name = modMatch[1];

  const lines = text.split('\n');
  let currentSection = "";

  for (let raw of lines) {
    let line = raw.trim();
    if (!line || line.startsWith("--")) continue;
    if (line === "VAR")    { currentSection = "var";    continue; }
    if (line === "ASSIGN") { currentSection = "assign"; continue; }
    if (line.includes("next(state)") && line.includes("case")) { currentSection = "next_state"; continue; }
    if (line === "esac;" || line === "esac") { currentSection = ""; continue; }

    if (currentSection === "var") {
      if (line.startsWith("state :") || line.startsWith("state:")) {
        const enumMatch = line.match(/\{([^}]+)\}/);
        if (enumMatch) {
          for (const s of enumMatch[1].split(',').map(s => s.trim()).filter(Boolean)) {
            states.add(s);
            stateDetails.push({ name: s, parent: "", is_composite: false });
          }
        }
      } else if (line.startsWith("event :") || line.startsWith("event:")) {
        const enumMatch = line.match(/\{([^}]+)\}/);
        if (enumMatch) {
          for (const ev of enumMatch[1].split(',').map(e => e.trim()).filter(e => e && e !== "none")) events.add(ev);
        }
      }
    } else if (currentSection === "assign") {
      if (line.startsWith("init(state)")) {
        const eqIdx = line.indexOf(":=");
        if (eqIdx !== -1) initial = line.substring(eqIdx + 2).replace(/;/g, '').trim();
      }
    } else if (currentSection === "next_state") {
      if (line.includes("TRUE :") || line.includes("TRUE:")) continue;
      let colonIdx = -1;
      for (let i = line.length - 1; i >= 0; i--) {
        if (line[i] === ':') {
          const isDouble = (i > 0 && line[i-1] === ':') || (i+1 < line.length && line[i+1] === ':');
          if (!isDouble) { colonIdx = i; break; }
        }
      }
      if (colonIdx === -1) continue;
      let commentAct = "";
      const commentPos = line.indexOf("--");
      if (commentPos !== -1 && commentPos > colonIdx) {
        const cStr = line.substring(commentPos);
        const actMatch = cStr.match(/action:\s*([A-Za-z0-9_]+)/);
        if (actMatch) commentAct = actMatch[1];
        line = line.substring(0, commentPos).trim();
      }
      const condPart   = line.substring(0, colonIdx).trim();
      const targetPart = line.substring(colonIdx + 1).replace(/;/g, '').trim();
      if (!targetPart) continue;
      const clauses = condPart.split('&').map(c => c.trim().replace(/^\(+|\)+$/g, '').trim());
      let srcState = "", evName = "Anonymous", guardParts = [];
      for (const c of clauses) {
        if (c.startsWith("state =") || c.startsWith("state=")) srcState = c.split('=')[1].trim();
        else if (c.startsWith("event =") || c.startsWith("event=")) {
          const ev = c.split('=')[1].trim();
          if (ev && ev !== "none") { evName = ev; events.add(ev); }
        } else if (c) guardParts.push(c);
      }
      if (srcState && targetPart) {
        states.add(srcState); states.add(targetPart);
        transitions.push({ source: srcState, target: targetPart, event: evName, guard: guardParts.join(" && "), action: commentAct, is_internal: srcState === targetPart });
      }
    }
  }

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}
