/**
 * fsmc Playground — Parser Index
 * Re-exports all parsers and the format detector for unified access via ModelManager.
 */

export { detectFormat } from './format_detect.js';
export { parseScxml, parseCameo } from './scxml.js';
export { parsePlantUmlOrMermaid } from './plantuml.js';
export { parseSysml2 } from './sysml.js';
export { parseJson, parseDot, parseSmv } from './misc.js';

/**
 * Dispatch to the correct parser based on detected format.
 * Returns a canonical model object.
 * @param {string} text
 * @param {string} [format]
 * @returns {object}
 */
export async function fallbackParse(text, format) {
  const { detectFormat } = await import('./format_detect.js');
  const fmt = format || detectFormat(text);

  switch (fmt) {
    case 'scxml':   { const { parseScxml }           = await import('./scxml.js');   return parseScxml(text); }
    case 'cameo':   { const { parseCameo }            = await import('./scxml.js');   return parseCameo(text); }
    case 'sysml2':  { const { parseSysml2 }           = await import('./sysml.js');   return parseSysml2(text); }
    case 'json':    { const { parseJson }             = await import('./misc.js');    return parseJson(text); }
    case 'dot':     { const { parseDot }              = await import('./misc.js');    return parseDot(text); }
    case 'smv':     { const { parseSmv }              = await import('./misc.js');    return parseSmv(text); }
    default:        { const { parsePlantUmlOrMermaid } = await import('./plantuml.js'); return parsePlantUmlOrMermaid(text); }
  }
}
