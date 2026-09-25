(async () => {
  document.getElementById('host').value = await getHost();
  const seg = document.getElementById('themeSeg');
  const mark = () => {
    const m = document.documentElement.getAttribute('data-theme') || 'system';
    seg.querySelectorAll('button').forEach(b => b.classList.toggle('on', b.dataset.m === m));
  };
  applyTheme(await getTheme());
  mark();
  seg.addEventListener('click', async e => {
    const b = e.target.closest('button[data-m]'); if (!b) return;
    const m = b.dataset.m;
    if (m === 'system') await chrome.storage.local.remove('theme');
    else await chrome.storage.local.set({ theme: m });
    applyTheme(m); mark();
  });
})();
/* Chrome lets only the user pin an extension to the toolbar - an unpinned one is
   hidden behind the puzzle icon and its badge is never seen. Say how, until it is
   pinned (0.2.1, V 2026-09-26). The change event needs Chrome 130+. */
async function showPin() {
  try {
    const s = await chrome.action.getUserSettings();
    document.getElementById('pin').hidden = !!s.isOnToolbar;
  } catch (e) {}
}
showPin();
if (chrome.action.onUserSettingsChanged) chrome.action.onUserSettingsChanged.addListener(showPin);

/* Save asks Chrome for access to this one address. The request comes first, before any
   await: Chrome grants it only inside the click (user gesture). */
document.getElementById('save').addEventListener('click', () => {
  const v = document.getElementById('host').value.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '') || DEFAULT_HOST;
  const msg = document.getElementById('msg');
  chrome.permissions.request({ origins: [originFor(v)] }, async granted => {
    if (!granted) {
      msg.textContent = 'Not allowed - without it the extension cannot read the printer';
      msg.className = 'bad';
      return;
    }
    const old = await getHost();
    await chrome.storage.local.set({ host: v });
    // The previous printer's access is not needed any more.
    if (originFor(old) !== originFor(v)) {
      try { await chrome.permissions.remove({ origins: [originFor(old)] }); } catch (e) {}
    }
    chrome.runtime.sendMessage('tick');
    msg.textContent = 'Saved';
    msg.className = '';
  });
});
