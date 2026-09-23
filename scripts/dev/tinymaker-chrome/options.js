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
document.getElementById('save').addEventListener('click', async () => {
  const v = document.getElementById('host').value.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '');
  await chrome.storage.local.set({ host: v || DEFAULT_HOST });
  chrome.runtime.sendMessage('tick');
  document.getElementById('msg').textContent = 'Saved';
});
