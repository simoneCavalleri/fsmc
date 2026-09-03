/**
 * fsmc Playground — Format Detector
 * Determines the diagram format from a raw text string via heuristic matching.
 */

/**
 * Detect the FSM diagram format from raw text.
 * @param {string} text
 * @returns {'scxml'|'cameo'|'plantuml'|'mermaid'|'sysml2'|'dot'|'smv'|'json'|'plantuml'}
 */
export function detectFormat(text) {
  const t = (text || "").trim();
  if (t.includes('<scxml') || t.includes('xmlns="http://www.w3.org/2005/07/scxml"')) return 'scxml';
  if (t.includes('<xmi:') || t.includes('<uml:') || t.includes('<packagedElement')) return 'cameo';
  if (t.includes('@startuml') || t.includes('@enduml')) return 'plantuml';
  if (t.includes('stateDiagram') || t.includes('stateDiagram-v2')) return 'mermaid';
  if (
    t.includes('state def ') || t.includes('transition from ') ||
    t.includes('item def ') || t.includes('event def ') ||
    t.includes('entry; then') || t.includes('attribute ')
  ) return 'sysml2';
  if (t.includes('digraph ') || t.startsWith('digraph{') || t.includes('graph ')) return 'dot';
  if (
    t.includes('MODULE main') || t.includes('ASSIGN next(state)') ||
    t.includes('LTLSPEC') || t.includes('INVARSPEC')
  ) return 'smv';
  if (t.startsWith('{') || (t.includes('"states"') && t.includes('"id"'))) return 'json';
  return 'plantuml';
}
