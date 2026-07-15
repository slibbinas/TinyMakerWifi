"""Build the offline demo: real dashboard UI + demo_shim.js fake printer.

Extracts the embedded dashboard page from src/Network.ino (same stitching as
extract_dashboard.py) and injects the shim right after <title>, so the mock
fetch/XHR/<img> layers are in place before any app script runs. Output is one
self-contained HTML - host it anywhere (gh-pages, artifact) or open locally.

  python scripts/dev/build_demo.py [out.html]
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "src", "Network.ino")
SHIM = os.path.join(HERE, "demo_shim.js")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "demo.html")

text = io.open(SRC, encoding="utf-8", errors="replace").read()


def quoted_strings(block: str) -> str:
    block = "\n".join(l for l in block.splitlines() if not l.lstrip().startswith("//"))
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
    return "".join(p.replace('\\"', '"').replace("\\\\", "\\") for p in parts)


fn_start = text.index("void sendRootStyledPage(PGM_P bodyBeforeFw")
fn_block = text[fn_start:text.index("server.sendContent_P(bodyBeforeFw)", fn_start)]
pstr_start = fn_block.index("server.sendContent_P(PSTR(")
head = quoted_strings(fn_block[pstr_start:])

raws = re.findall(r'R"SPA\((.*?)\)SPA"', text, re.S)
assert len(raws) in (3, 4), f"expected 3-4 raw segments, got {len(raws)}"

body_after = raws[1] + "demo" + raws[2]
if len(raws) == 4:
    body_after += "0.15.0" + raws[3]

html = head + raws[0] + "0.15.0" + body_after + "</main></body></html>"

shim = io.open(SHIM, encoding="utf-8").read()
marker = "<title>TinyMaker</title>"
assert marker in html, "title marker not found"
html = html.replace(marker, marker + "<script>\n" + shim + "\n</script>", 1)

io.open(OUT, "w", encoding="utf-8").write(html)
print(f"wrote {OUT}: {len(html)} bytes")
