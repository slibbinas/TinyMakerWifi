const $ = id => document.getElementById(id);

function row(k, v) {
  if (v === undefined || v === null || v === '') return '';
  const td = s => '<td>' + String(s).replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c])) + '</td>';
  return '<tr>' + td(k) + td(v) + '</tr>';
}

function eta(secs) {
  const n = Number(secs) || 0;
  if (!n) return '';
  const d = new Date(Date.now() + n * 1000);
  return d.toTimeString().slice(0, 5);
}

function render(s, err, host) {
  $('host').textContent = host;
  if (!s) {
    const setup = err === 'no-access';
    $('state').textContent = setup ? 'Not set up yet' : 'Not answering';
    $('err').style.display = 'block';
    $('err').textContent = setup
      ? 'Press "Printer address" below, check the address and Save - Chrome then lets the extension read the printer.'
      : (err || 'No answer from the printer.');
    $('job').style.display = 'none';
    $('facts').innerHTML = '';
    return;
  }
  $('err').style.display = 'none';
  $('ver').textContent = s.firmwareVersion ? 'v' + s.firmwareVersion : '';
  const p = phase(s);
  $('state').textContent = s.state || p;
  const printing = p === 'printing' || p === 'paused';
  $('job').style.display = printing ? 'block' : 'none';
  if (printing) {
    $('model').textContent = s.model || '';
    $('fill').style.width = percent(s) + '%';
  }
  let h = '';
  if (printing) {
    h += row('Layer', s.layerText);
    h += row('Running', s.runTime);
    h += row('Remaining', s.remainingTime ? s.remainingTime + (eta(s.remainingSecs) ? ' · ~' + eta(s.remainingSecs) : '') : '');
    h += row('Resin used', s.resinText);
    if (s.dryRun) h += row('Dry run', 'on');
  } else if (p === 'sdjob') {
    h += row('SD job', (s.sdJobName || '') + (s.sdJobTotal ? ' ' + s.sdJobDone + '/' + s.sdJobTotal : ''));
  }
  h += row('Resin left', s.vatText);
  h += row('SD card', s.sdText);
  h += row('WiFi', s.wifiText);
  $('facts').innerHTML = h;
}

async function load(live) {
  const host = await getHost();
  let s = null, err = '';
  if (live && !(await hasAccess(host))) {
    err = 'no-access';
  } else if (live) {
    try { s = await fetchStatus(host); } catch (e) { err = String(e && e.message || e); }
  } else {
    const st = await chrome.storage.local.get(['last', 'lastErr']);
    s = st.last; err = st.lastErr;
  }
  render(s, err, host);
}

/* An open dashboard tab is brought forward instead of opening another one (V 2026-09-21).
   Matched by the printer's address: `url` is readable because of the printer's host permission. */
$('open').addEventListener('click', async () => {
  const host = (await getHost()).toLowerCase();
  const tabs = await chrome.tabs.query({});
  const tab = tabs.find(t => { try { const u = new URL(t.url || ''); return u.protocol === 'http:' && u.host === host; } catch (e) { return false; } });
  if (tab) {
    await chrome.tabs.update(tab.id, { active: true });
    await chrome.windows.update(tab.windowId, { focused: true });
    window.close();
  } else {
    chrome.tabs.create({ url: 'http://' + host + '/' });
  }
});
$('refresh').addEventListener('click', () => { chrome.runtime.sendMessage('tick'); load(true); });
$('opts').addEventListener('click', e => { e.preventDefault(); chrome.runtime.openOptionsPage(); });

load(false).then(() => load(true));
chrome.runtime.sendMessage('seen');
