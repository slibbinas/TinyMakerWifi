"""Stitch the embedded dashboard page out of src/Network.ino into one HTML file.

Order (mirrors sendRootStyledPage + handleRootPage):
  header/style PSTR strings -> rootBodyBeforeFw raw -> FW version ->
  rootBodyAfterFw raw #1 -> GIT_REV -> rootBodyAfterFw raw #2 -> closing PSTR.
"""
import re, sys, io

SRC = r"C:\Users\SViktoras\Documents\PlatformIO\Projects\TinyMakerWiFi\src\Network.ino"
OUT = sys.argv[1] if len(sys.argv) > 1 else "dashboard.html"

text = io.open(SRC, encoding="utf-8", errors="replace").read()

def quoted_strings(block: str) -> str:
    # Drop full-line // comments first so quotes inside them can't confuse us.
    block = "\n".join(l for l in block.splitlines() if not l.lstrip().startswith("//"))
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
    return "".join(p.replace('\\"', '"').replace("\\\\", "\\") for p in parts)

# 1) Header + <style> block: first PSTR(...) inside sendRootStyledPage.
fn_start = text.index("void sendRootStyledPage(PGM_P bodyBeforeFw")
fn_block = text[fn_start:text.index("server.sendContent_P(bodyBeforeFw)", fn_start)]
pstr_start = fn_block.index("server.sendContent_P(PSTR(")
head = quoted_strings(fn_block[pstr_start:])

# 2) Raw R"SPA(...)SPA" segments: [0]=bodyBeforeFw, [1..]=bodyAfterFw pieces
#    joined by GIT_REV and then FIRMWARE_VERSION (hosted-connect cache buster).
raws = re.findall(r'R"SPA\((.*?)\)SPA"', text, re.S)
assert len(raws) in (3, 4), f"expected 3-4 raw segments, got {len(raws)}"

body_after = raws[1] + "abc1234" + raws[2]
if len(raws) == 4:
    body_after += "0.15.0-dev" + raws[3]

html = head + raws[0] + "0.15.0-dev" + body_after + "</main></body></html>"
io.open(OUT, "w", encoding="utf-8").write(html)
print(f"wrote {OUT}: {len(html)} bytes")
