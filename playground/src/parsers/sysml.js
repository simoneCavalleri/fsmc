/**
 * fsmc Playground — OMG SysML v2 Parser
 * Parses the native SysML v2 HFSM notation into the canonical AST model,
 * including composite states, history pseudostates, deferred events, and typed ports.
 */

/**
 * Parse SysML v2 state machine definition text.
 * @param {string} text
 * @returns {object}
 */
export function parseSysml2(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const nameMatch = text.match(/(?:state\s+def|package)\s+([A-Za-z0-9_]+)/);
  if (nameMatch) name = nameMatch[1];

  // Strip comments and port constraint blocks
  let cleanText = text.replace(/\/\/.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '');
  cleanText = cleanText.replace(/\{\s*assert\s+constraint[\s\S]*?\}\s*\}/g, ';');

  const stateStack = [];

  const processSysmlStmt = (stmt, stack) => {
    const s = stmt.replace(/\s+/g, ' ').trim();
    if (!s || s.startsWith("attribute") ||
        s.startsWith("in port") || s.startsWith("out port") || s.startsWith("port") ||
        s === "entry" || s === "initial") return;

    // Event / item definitions
    const evtDefMatch = s.match(/^(?:event\s+def|item\s+def)\s+([A-Za-z0-9_]+)/);
    if (evtDefMatch) { events.add(evtDefMatch[1]); return; }

    // Initial/entry -> sub-state
    const initMatch = s.match(/^(?:(?:entry|initial)(?:\s*;)?\s*)?then\s+([A-Za-z0-9_]+)/);
    if (initMatch) {
      if (stack.length === 0) {
        initial = initMatch[1];
      } else {
        const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
        if (pObj) pObj.initial_sub_state = initMatch[1];
      }
      return;
    }

    // do activity
    const doActMatch = s.match(/^do\s+(?:action\s+)?([A-Za-z0-9_]+)/);
    if (doActMatch && stack.length > 0) {
      const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
      if (pObj) pObj.do_activity = doActMatch[1];
      return;
    }

    // entry action
    const entryActMatch = s.match(/^entry\s+(?:action\s+|do\s+)([A-Za-z0-9_]+)/);
    if (entryActMatch && stack.length > 0) {
      const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
      if (pObj) { pObj.entry_actions = pObj.entry_actions || []; pObj.entry_actions.push(entryActMatch[1]); }
      return;
    }

    // exit action
    const exitActMatch = s.match(/^exit\s+(?:action\s+|do\s+)([A-Za-z0-9_]+)/);
    if (exitActMatch && stack.length > 0) {
      const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
      if (pObj) { pObj.exit_actions = pObj.exit_actions || []; pObj.exit_actions.push(exitActMatch[1]); }
      return;
    }

    // defer
    const deferMatch = s.match(/^defer\s+([A-Za-z0-9_]+)/);
    if (deferMatch) {
      events.add(deferMatch[1]);
      if (stack.length > 0) {
        const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
        if (pObj) { pObj.deferred_events = pObj.deferred_events || []; pObj.deferred_events.push(deferMatch[1]); }
      }
      return;
    }

    // Leaf state declaration
    const stateDecl = s.match(/^state\s+([A-Za-z0-9_]+)\s*$/);
    if (stateDecl && stateDecl[1] !== "def") {
      const sName = stateDecl[1];
      const parent = stack.length > 0 ? stack[stack.length - 1] : "";
      if (!states.has(sName)) {
        states.add(sName);
        stateDetails.push({ name: sName, parent, is_composite: false });
      }
      return;
    }

    // Transition
    if (s.startsWith("transition") || s.includes("first ") || s.includes("from ")) {
      const fromMatch  = s.match(/(?:first|from)\s+([A-Za-z0-9_]+)/);
      const acceptMatch = s.match(/(?:accept|when)\s+(?:[A-Za-z0-9_]+\s*:\s*)?([A-Za-z0-9_]+)/);
      const ifMatch    = s.match(/\sif\s+(.+?)(?=\sdo\s|\sthen\s|\sto\s|;|$)/);
      const doMatch    = s.match(/\sdo\s+(?:\{([^}]+)\}|([A-Za-z0-9_]+))/);
      const thenMatch  = s.match(/(?:then|to)\s+([A-Za-z0-9_\[\]\*]+)/);

      const src    = fromMatch ? fromMatch[1] : (stack.length > 0 ? stack[stack.length - 1] : "");
      const rawDst = thenMatch ? thenMatch[1] : src;
      let dst = rawDst;
      let isHist = false, isDeepHist = false;
      if (dst && dst.includes("[H")) {
        isHist = true;
        isDeepHist = dst.includes("[H*]");
        dst = dst.replace(/\[H\*?\]/g, '');
      }

      if (src && dst) {
        // Eventless transition: no accept clause → event = ""
        const evt    = acceptMatch ? acceptMatch[1] : "";
        const guard  = ifMatch  ? ifMatch[1].trim()               : "";
        const action = doMatch  ? (doMatch[1] || doMatch[2]).trim() : "";
        states.add(src);
        states.add(dst);
        if (evt) events.add(evt);
        transitions.push({
          source: src, target: dst,
          event: evt, guard, action,
          is_internal: (src === dst && !thenMatch),
          target_is_history: isHist,
          target_is_deep_history: isDeepHist
        });
      }
    }
  };

  // Tokenise the cleaned text character by character
  let currentStmt = "";
  let inActionBrace = 0;
  for (let i = 0; i < cleanText.length; i++) {
    const ch = cleanText[i];
    if (ch === '{') {
      const stmt = currentStmt.trim();
      if (stmt.startsWith("state def") || stmt.startsWith("package")) {
        currentStmt = "";
        continue;
      }
      const stateDecl = stmt.match(/state\s+([A-Za-z0-9_]+)/);
      if (stateDecl && stateDecl[1] !== "def") {
        currentStmt = "";
        const sName  = stateDecl[1];
        const parent = stateStack.length > 0 ? stateStack[stateStack.length - 1] : "";
        states.add(sName);
        stateDetails.push({ name: sName, parent, is_composite: true });
        stateStack.push(sName);
      } else {
        inActionBrace++;
        currentStmt += "{";
      }
    } else if (ch === '}') {
      if (inActionBrace > 0) {
        inActionBrace--;
        currentStmt += "}";
      } else {
        const stmt = currentStmt.trim();
        currentStmt = "";
        if (stmt) processSysmlStmt(stmt, stateStack);
        if (stateStack.length > 0) stateStack.pop();
      }
    } else if (ch === ';') {
      if (inActionBrace > 0) {
        currentStmt += ";";
      } else {
        const s = currentStmt.trim();
        if (s === "entry" || s === "initial") { currentStmt += "; "; continue; }
        currentStmt = "";
        if (s) processSysmlStmt(s, stateStack);
      }
    } else {
      currentStmt += ch;
    }
  }

  const stateList = Array.from(states);
  if (!initial && stateList.length > 0) initial = stateList[0];
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || "Disconnected"
  };
}
