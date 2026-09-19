// Test-panel marks: merging one device's snapshot into the stored state.
//
// This lives in its own file so it can be unit-tested without Cloudflare.
// It is the rule that decides whose mark survives when the phone (at the
// printer) and the computer (on the desk) both hold the checklist open, and
// both send FULL snapshots. Get it wrong and a tab left open yesterday
// quietly undoes an hour of testing - a failure nobody notices until the
// report is already wrong. See test/state.test.mjs.

export const TEST_ID = /^[ST]-\d{1,3}$/;   // only real checklist ids get stored
export const MAX_NOTE = 2000;

/**
 * @param {object} current  state as stored (may be null/garbage)
 * @param {object} incoming one device's full snapshot
 * @returns {object} new state - `current` is never mutated
 */
export function mergeState(current, incoming) {
  const merged = { ...(current && typeof current === 'object' ? current : {}) };
  if (!incoming || typeof incoming !== 'object' || Array.isArray(incoming)) return merged;

  for (const nr of Object.keys(incoming)) {
    if (!TEST_ID.test(nr)) continue;
    const a = incoming[nr];
    if (!a || typeof a !== 'object' || Array.isArray(a)) continue;

    const at = Number(a.t) || 0;
    const bt = Number(merged[nr] && merged[nr].t) || 0;
    if (merged[nr] && at < bt) continue;        // ours is newer - keep it

    const v = (a.v === 'pass' || a.v === 'fail' || a.v === 'skip') ? a.v : null;
    const n = (typeof a.n === 'string') ? a.n.slice(0, MAX_NOTE) : '';
    if (!v && !n) { delete merged[nr]; continue; }   // row cleared on that device
    merged[nr] = { v, n, t: at };
  }
  return merged;
}
