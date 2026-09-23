/* Shared by the background worker and the popup: where the printer is and how to read it.
   The printer answers GET /api/status without CORS headers; an extension with a host
   permission may read it anyway. Nothing here writes to the printer. */
const DEFAULT_HOST = 'tinymaker.local';

async function getHost() {
  const { host } = await chrome.storage.local.get('host');
  return (host || DEFAULT_HOST).trim();
}

// Theme is chosen in the extension's options; the popup only obeys it.
// 'system' (default) = follow the OS via prefers-color-scheme.
async function getTheme() {
  const { theme } = await chrome.storage.local.get('theme');
  return (theme === 'light' || theme === 'dark') ? theme : 'system';
}
function applyTheme(t) {
  if (t === 'light' || t === 'dark') document.documentElement.setAttribute('data-theme', t);
  else document.documentElement.removeAttribute('data-theme');
}

async function fetchStatus(host, timeoutMs = 5000) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const r = await fetch('http://' + host + '/api/status', { cache: 'no-store', signal: ctrl.signal });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return await r.json();
  } finally {
    clearTimeout(t);
  }
}

/* One word for what the printer is doing - the same split the dashboard makes:
   an SD job (unpacking, deleting) is not a print. */
function phase(s) {
  if (!s) return 'offline';
  if (s.busy && s.sdJob) return 'sdjob';
  if (s.busy && (s.paused || s.pausing)) return 'paused';
  if (s.busy) return 'printing';
  return 'idle';
}

function percent(s) {
  const n = Number(s && s.totalLayers) || 0;
  const k = Number(s && s.currentLayer) || 0;
  return n > 0 ? Math.min(100, Math.floor((k * 100) / n)) : 0;
}
