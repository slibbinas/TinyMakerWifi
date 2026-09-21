(async () => { document.getElementById('host').value = await getHost(); })();
document.getElementById('save').addEventListener('click', async () => {
  const v = document.getElementById('host').value.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '');
  await chrome.storage.local.set({ host: v || DEFAULT_HOST });
  chrome.runtime.sendMessage('tick');
  document.getElementById('msg').textContent = 'Saved';
});
