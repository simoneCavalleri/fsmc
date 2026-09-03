/**
 * fsmc Playground — PlantUML / Mermaid stateDiagram Parser
 * Parses PlantUML and Mermaid stateDiagram-v2 into the canonical AST model.
 */

/**
 * Parse PlantUML (@startuml) or Mermaid (stateDiagram-v2) text.
 * Both formats share the --> transition arrow syntax.
 * @param {string} text
 * @returns {object}
 */
export function parsePlantUmlOrMermaid(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const lines = text.split('\n');
  for (const raw of lines) {
    const line = raw.trim();
    if (!line) continue;

    // Title detection
    const titleMatch = line.match(
      /(?:---\s*title:\s*([A-Za-z0-9_]+)|title:\s*([A-Za-z0-9_]+)|@startuml\s+([A-Za-z0-9_]+)|@fsm:name\s+([A-Za-z0-9_]+))/
    );
    if (titleMatch) {
      name = titleMatch[1] || titleMatch[2] || titleMatch[3] || titleMatch[4];
      continue;
    }

    // Signal annotations
    const sigMatch = line.match(/(?:'|%%|<!--)\s*@fsm:signal\s+([A-Za-z0-9_]+)/);
    if (sigMatch) { events.add(sigMatch[1]); continue; }

    // Skip directive lines
    if (line.startsWith('@') || line.startsWith('stateDiagram') || line.startsWith('<?xml') || line.startsWith('<scxml')) continue;

    // State declaration (no transition)
    if (line.startsWith('state ') && !line.includes('-->')) {
      const stName = line.replace('state ', '').split('{')[0].split('[')[0].trim();
      if (stName) {
        states.add(stName);
        stateDetails.push({ name: stName, parent: "", is_composite: line.includes('{') });
      }
    }

    // Transition: A --> B : Event [Guard] / Action
    if (line.includes('-->')) {
      const parts = line.split('-->');
      const src = parts[0].trim();
      const rest = parts[1].trim();
      const rawDst = rest.split(':')[0].trim();
      const isHist = rawDst.includes('[H');
      const isDeepHist = rawDst.includes('[H*]');
      const dst = rawDst.replace(/\[H\*?\]/g, '');
      let evt = "Anonymous";
      let guard = "";
      let action = "";

      if (rest.includes(':')) {
        const rawLabel = rest.split(':')[1].trim();
        evt = rawLabel.split('[')[0].split('/')[0].trim() || "Anonymous";
        const gMatch = rawLabel.match(/\[([^\]]+)\]/);
        if (gMatch) guard = gMatch[1];
        const aMatch = rawLabel.match(/\/([^\n]+)/);
        if (aMatch) action = aMatch[1].trim();
      }

      if (src === '[*]' && dst) {
        initial = dst;
      } else {
        if (src && src !== '[*]') states.add(src);
        if (dst && dst !== '[*]') states.add(dst);
        if (evt && evt !== 'Anonymous') events.add(evt);
        transitions.push({
          source: src, target: dst,
          event: evt, guard, action,
          is_internal: (src === dst),
          target_is_history: isHist,
          target_is_deep_history: isDeepHist
        });
      }
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
