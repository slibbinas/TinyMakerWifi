/* Loaded in <head> of the popup and the options page so the saved theme is on before
   the first paint. It must be a file: extension pages forbid inline scripts (CSP). */
try {
  chrome.storage.local.get('theme', function (r) {
    var t = r && r.theme;
    if (t === 'light' || t === 'dark') document.documentElement.setAttribute('data-theme', t);
  });
} catch (e) {}
