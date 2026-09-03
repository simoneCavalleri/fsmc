/**
 * fsmc Playground — FSM Utility Functions
 * Pure functions for state machine graph traversal. No DOM, no WASM deps.
 */

/**
 * Resolves a potentially composite state to its deepest initial leaf state.
 * Follows initial_sub_state links recursively until a leaf is reached.
 * @param {object} model
 * @param {string} targetState
 * @returns {string}
 */
export function resolveLeafState(model, targetState) {
  if (!targetState) return "";
  let curr = targetState.replace(/\[H\*?\]/g, "").trim();
  const visited = new Set();
  while (curr && !visited.has(curr)) {
    visited.add(curr);
    const detail = (model.stateDetails || []).find(d => d.name === curr);
    if (detail && detail.is_composite && detail.initial_sub_state) {
      curr = detail.initial_sub_state;
    } else {
      break;
    }
  }
  return curr;
}

/**
 * Returns the ancestor chain from a leaf state up to the root,
 * as an ordered array [leaf, parent, grandparent, ...].
 * @param {object} model
 * @param {string} stateName
 * @returns {string[]}
 */
export function getAncestorChain(model, stateName) {
  const chain = [];
  let curr = stateName;
  const visited = new Set();
  while (curr && !visited.has(curr)) {
    visited.add(curr);
    chain.push(curr);
    const detail = (model.stateDetails || []).find(d => d.name === curr);
    if (detail && detail.parent) {
      curr = detail.parent;
    } else {
      break;
    }
  }
  return chain;
}

/**
 * Returns the set of transitions available in the current state,
 * including those defined in ancestor (composite) states.
 * Respects UML priority: innermost state wins for duplicate events.
 * @param {object} model
 * @param {string} currState
 * @returns {object[]}
 */
export function getAvailableTransitions(model, currState) {
  const ancestors = getAncestorChain(model, currState);
  const result = [];
  const seenEvents = new Set();
  for (const st of ancestors) {
    const matching = (model.transitions || []).filter(t => t.source === st);
    for (const t of matching) {
      if (!seenEvents.has(t.event)) {
        seenEvents.add(t.event);
        result.push(t);
      }
    }
  }
  return result;
}
