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
    $('state').textContent = 'Not answering';
    $('err').style.display = 'block';
    $('err').textContent = err || 'No answer from the printer.';
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
  if (live) {
    try { s = await fetchStatus(host); } catch (e) { err = String(e && e.message || e); }
  } else {
    const st = await chrome.storage.local.get(['last', 'lastErr']);
    s = st.last; err = st.lastErr;
  }
  render(s, err, host);
}

$('open').addEventListener('click', async () => {
  chrome.tabs.create({ url: 'http://' + (await getHost()) + '/' });
});
$('refresh').addEventListener('click', () => { chrome.runtime.sendMessage('tick'); load(true); });
$('opts').addEventListener('click', e => { e.preventDefault(); chrome.runtime.openOptionsPage(); });

load(false).then(() => load(true));
chrome.runtime.sendMessage('seen');
