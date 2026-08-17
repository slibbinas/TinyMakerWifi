"""Build the offline demo: real dashboard UI + demo_shim.js fake printer.

Reads the canonical dashboard source (web/dashboard.html) and injects the shim
right after <title>, so the mock fetch/XHR/<img> layers are in place before any
app script runs. Output is one self-contained HTML - host it anywhere
(gh-pages, artifact) or open locally.

  python scripts/dev/build_demo.py [out.html]
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", ".."))
DASH = os.path.join(PROJ, "web", "dashboard.html")
SHIM = os.path.join(HERE, "demo_shim.js")
SCRIPTS = os.path.join(PROJ, "scripts")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "demo.html")


def dashboard():
    """Pultas taip, kaip ji mato narsykle.

    Nuo 2026-08-17 slicerio dalys guli atskirai (`web/parts/*`), o pulte ju
    vietoje stovi `#include` zymes; sulipdo `scripts/assemble_dashboard.py`
    build metu. Skaitant faila tiesiai gautum pulta BE slicerio ir tikrintum
    ne ta puslapi - i tai jau ikrito kita sesija per pirma patikra.

    Salyga, o ne besalyginis importas: tas skriptas atkeliauja su ju saka, o
    stendas turi veikti ir pries merge, ir po jo, be rankinio perjungimo.
    """
    if os.path.exists(os.path.join(SCRIPTS, "assemble_dashboard.py")):
        sys.path.insert(0, SCRIPTS)
        from assemble_dashboard import assemble
        return assemble(PROJ)
    return io.open(DASH, encoding="utf-8", errors="replace").read()


html = dashboard()

# The dashboard is now version-agnostic (fwVersion/data-build start empty and
# fill from /api/status). The demo's mock status must report a real version:
# read it from platformio.ini rather than hardcoding (a frozen literal here
# once re-published a stale version on every rebuild).
INI = os.path.join(HERE, "..", "..", "platformio.ini")
_vers = set(re.findall(r'FIRMWARE_VERSION=\\"(\d+\.\d+\.\d+)\\"',
                       io.open(INI, encoding="utf-8").read()))
assert len(_vers) == 1, f"expected one FIRMWARE_VERSION in platformio.ini, got {_vers}"
FW = _vers.pop()

shim = io.open(SHIM, encoding="utf-8").read()
# The shim's STATUS reports this version; the page reads it from status and
# fills #fwVersion. (Historically a mismatch here vs a baked-in page version
# tripped reloadIfFirmwareChanged -> location.replace, bouncing /demo visitors
# to the landing page. The static page starts empty, so no mismatch occurs.)
shim, n = re.subn(r"firmwareVersion:'\d+\.\d+\.\d+'",
                  f"firmwareVersion:'{FW}'", shim)
assert n == 1, f"expected exactly one firmwareVersion literal in the shim, patched {n}"
marker = "<title>TinyMaker</title>"
assert marker in html, "title marker not found"
html = html.replace(marker, marker + "<script>\n" + shim + "\n</script>", 1)

io.open(OUT, "w", encoding="utf-8").write(html)
print(f"wrote {OUT}: {len(html)} bytes")
