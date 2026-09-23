/* Injected only into the printer's own dashboard (the host set in options).
   Leaves a marker so the dashboard can hide its "get the extension" hint once
   the extension is installed. Sets nothing else, reads nothing. */
document.documentElement.setAttribute('data-tm-ext', '1');
