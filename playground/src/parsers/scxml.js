/**
 * fsmc Playground — XML/SCXML Parser
 * Parses W3C SCXML and OMG XMI/Cameo-style XML into the canonical AST model.
 */

/** @param {string} str */
function unescapeXml(str) {
  return (str || "")
    .replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'");
}

/**
 * Parse W3C SCXML into the canonical model.
 * @param {string} text
 * @returns {object}
 */
export function parseScxml(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const nameMatch = text.match(/name="([^"]+)"/);
  if (nameMatch) name = nameMatch[1];
  const initMatch = text.match(/initial="([^"]+)"/);
  if (initMatch) initial = initMatch[1];

  const stateHeaderRegex = /<state\s+([^>]*?)(\/?>|>)/g;
  let sh;
  while ((sh = stateHeaderRegex.exec(text)) !== null) {
    const idMatch = sh[1].match(/id="([^"]+)"/);
    if (idMatch) {
      const sId = idMatch[1];
      if (!states.has(sId)) {
        states.add(sId);
        stateDetails.push({ name: sId, parent: "", is_composite: false });
      }
    }
  }

  const stateBlocks = text.matchAll(/<state\s+([^>]*?)>([\s\S]*?)<\/state>/g);
  for (const sb of stateBlocks) {
    const stateAttrs = sb[1];
    const stateBody = sb[2];
    const idMatch = stateAttrs.match(/id="([^"]+)"/);
    if (!idMatch) continue;
    const srcId = idMatch[1];

    const transTags = stateBody.matchAll(/<transition\s+([^>]*?)\/?>/g);
    for (const tm of transTags) {
      const tAttrs = tm[1];
      const evtM  = tAttrs.match(/event="([^"]+)"/);
      const condM = tAttrs.match(/cond="([^"]*)"/);
      const targetM = tAttrs.match(/target="([^"]+)"/);
      const actM  = tAttrs.match(/action="([^"]*)"/) || tAttrs.match(/effect="([^"]*)"/);

      const evt  = evtM   ? evtM[1]  : "Anonymous";
      const cond = condM  ? condM[1].replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">") : "";
      const dst  = targetM ? targetM[1] : srcId;
      const act  = actM   ? actM[1]  : "";

      if (evt !== "Anonymous") events.add(evt);
      if (dst && !states.has(dst)) {
        states.add(dst);
        stateDetails.push({ name: dst, parent: "", is_composite: false });
      }
      transitions.push({
        source: srcId, target: dst,
        event: evt, guard: cond, action: act,
        is_internal: (srcId === dst)
      });
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
 * Parse OMG XMI / Cameo-MagicDraw into the canonical model.
 * @param {string} text
 * @returns {object}
 */
export function parseCameo(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  const name = "GeneratedFSM";

  const stateMap = new Map();
  const stateMatches = text.matchAll(/<subvertex\s+([^>]*?)\/?>/g);
  for (const m of stateMatches) {
    const attrs = m[1];
    const idMatch   = attrs.match(/xmi:id="([^"]+)"/);
    const nameMatch = attrs.match(/name="([^"]+)"/);
    const typeMatch = attrs.match(/xmi:type="([^"]+)"/);
    if (idMatch && nameMatch && (!typeMatch || typeMatch[1].includes("State"))) {
      stateMap.set(idMatch[1], nameMatch[1]);
      states.add(nameMatch[1]);
      stateDetails.push({ name: nameMatch[1], parent: "", is_composite: false });
    }
  }

  const initMatch = text.match(/<transition\s+[^>]*source="[^"]*ps[^"]*"\s+target="([^"]+)"/);
  if (initMatch && stateMap.has(initMatch[1])) {
    initial = stateMap.get(initMatch[1]);
  }

  const transMatches = text.matchAll(/<transition\s+([^>]*?)\/?>/g);
  for (const t of transMatches) {
    const attrs = t[1];
    const srcM   = attrs.match(/source="([^"]+)"/);
    const dstM   = attrs.match(/target="([^"]+)"/);
    const trigM  = attrs.match(/trigger="([^"]*)"/);
    const guardM = attrs.match(/guard="([^"]*)"/);
    const effM   = attrs.match(/effect="([^"]*)"/) || attrs.match(/action="([^"]*)"/);

    if (!srcM || !dstM) continue;
    const src   = stateMap.get(srcM[1]) || srcM[1];
    const dst   = stateMap.get(dstM[1]) || dstM[1];
    const evt   = trigM  ? trigM[1]           : "Anonymous";
    const guard = guardM ? unescapeXml(guardM[1]) : "";
    const act   = effM   ? unescapeXml(effM[1])   : "";

    if (src.includes("ps") || src.includes("initial")) {
      initial = dst;
      continue;
    }
    if (evt !== "Anonymous") events.add(evt);
    transitions.push({ source: src, target: dst, event: evt, guard, action: act, is_internal: (src === dst) });
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
