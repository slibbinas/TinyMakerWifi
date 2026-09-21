/* Once a minute: read the printer's status and paint the toolbar badge.
   The dashboard itself asks every 4 s, so this adds no real load. */
importScripts('common.js');

const COLORS = { printing: '#e8720c', paused: '#d69e2e', sdjob: '#4f8fe0', done: '#2fbf4f', offline: '#777777' };

async function tick() {
  const host = await getHost();
  let s = null, err = '';
  try { s = await fetchStatus(host); } catch (e) { err = String(e && e.message || e); }
  const p = phase(s);
  const { wasPrinting, done } = await chrome.storage.local.get(['wasPrinting', 'done']);
  // A print that just ended shows a tick until the popup is opened.
  let doneNow = !!done;
  if (wasPrinting && p === 'idle') doneNow = true;
  if (p === 'printing' || p === 'paused') doneNow = false;
  await chrome.storage.local.set({
    last: s, lastErr: err, lastAt: Date.now(), wasPrinting: p === 'printing' || p === 'paused', done: doneNow
  });
  paint(p, s, doneNow);
}

function paint(p, s, doneNow) {
  let text = '', color = COLORS.offline, title = 'TinyMaker';
  if (p === 'printing') { text = percent(s) + '%'; color = COLORS.printing; title = 'Printing ' + (s.model || '') + ' - ' + (s.layerText || ''); }
  else if (p === 'paused') { text = 'II'; color = COLORS.paused; title = 'Paused - ' + (s.model || ''); }
  else if (p === 'sdjob') { text = 'SD'; color = COLORS.sdjob; title = (s.state || 'SD card job'); }
  else if (p === 'offline') { text = '!'; color = COLORS.offline; title = 'Printer not answering'; }
  else if (doneNow) { text = '✓'; color = COLORS.done; title = 'Print finished'; }
  else { title = 'Idle'; }
  chrome.action.setBadgeText({ text });
  chrome.action.setBadgeBackgroundColor({ color });
  chrome.action.setTitle({ title: 'TinyMaker - ' + title });
}

chrome.runtime.onInstalled.addListener(() => { chrome.alarms.create('tick', { periodInMinutes: 1 }); tick(); });
chrome.runtime.onStartup.addListener(() => { chrome.alarms.create('tick', { periodInMinutes: 1 }); tick(); });
chrome.alarms.onAlarm.addListener(a => { if (a.name === 'tick') tick(); });
chrome.runtime.onMessage.addListener((m, _s, reply) => {
  if (m === 'tick') { tick().then(() => reply(true)); return true; }
  if (m === 'seen') { chrome.storage.local.set({ done: false }).then(tick).then(() => reply(true)); return true; }
});
