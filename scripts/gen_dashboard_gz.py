"""PlatformIO pre-script: gzip web/dashboard.html into src/dashboard_html_gz.h.

The dashboard (HTML+CSS+JS) is authored in web/dashboard.html and served
pre-compressed with Content-Encoding: gzip. Storing the ~43 KB gzip blob instead
of ~143 KB of PROGMEM string literals frees ~99 KB of flash.

Since 0.17 the slicer's own pieces live in web/parts/ and the page carries
`#include` markers where they go - two sessions work on this project at once
(one on the printer, one on the slicer) and they were writing into the same
file from two branches. scripts/assemble_dashboard.py splices them back here,
at build time, so the browser still receives one file and the flash cost is
unchanged: the assembled bytes are identical to the single-file page.

The page is version-agnostic: #fwVersion / #fwBuild fill from /api/status at
runtime, so no build-time splice is needed and the whole page compresses cleanly.

The generated header is git-ignored (regenerated from web/dashboard.html on every
build). To avoid a needless full-TU rebuild we only rewrite it when the bytes
change (mtime bump would make SCons recompile the whole ~4000-line unit).
"""

Import("env")
import gzip
import io
import os
import sys
import zlib

proj = env["PROJECT_DIR"]
out = os.path.join(proj, "src", "dashboard_html_gz.h")

# Comments are 34 % of the dashboard by weight and ~60 KB of flash once gzipped.
# They stay in web/dashboard.html and in git; only the copy that goes into the
# firmware loses them. scripts/dev/test_strip.py proves nothing but comments go.
sys.path.insert(0, os.path.join(proj, "scripts"))
from strip_html_comments import strip_comments, style_spans
from assemble_dashboard import assemble

# The page plus web/parts/* - see the module docstring. A part that changes
# changes these bytes, so the write-only-if-changed guard below still triggers
# the rebuild it should.
raw = assemble(proj)
stripped = strip_comments(raw)
# Guard: every line that carries code must survive. Outside <style> the stripper
# only removes whole-line comments, so anything else disappearing means it
# mis-tracked a template literal - fail the build rather than flash a broken
# dashboard. The stylesheet is exempt and only it: there comments sit on the same
# line as the rules, so removing them rewrites lines that carry code by design.
# What the CSS pass may and may not do is checked by scripts/dev/test_strip.py,
# which compares the whole stylesheet against an independent removal.
_kept = set(l.strip() for l in stripped.splitlines() if l.strip())
# Stylesheet bounds come from the stripper itself, not from spotting "<style" in
# a line: a JS string like "<style>" with no closer would otherwise switch the
# guard off for the whole rest of the file, and it would never say so (audit
# 09-06). One source of truth, so the guard watches exactly what was rewritten.
_spans = style_spans(raw)
_pos, _skip = 0, []
for _line in raw.splitlines(keepends=True):
    _a, _b = _pos, _pos + len(_line)
    _skip.append(any(_a < e and _b > s_ for s_, e in _spans))
    _pos = _b
_in_block = _in_html = False
for _idx, _line in enumerate(raw.splitlines()):
    _s = _line.strip()
    if _skip[_idx]:
        continue
    if _in_block:
        if "*/" in _s: _in_block = False
        continue
    if _in_html:
        if "-->" in _s: _in_html = False
        continue
    if _s.startswith("/*") and "*/" not in _s: _in_block = True; continue
    if _s.startswith("<!--") and "-->" not in _s: _in_html = True; continue
    if not _s or _s.startswith("//") or _s.startswith("/*") or _s.startswith("<!--"):
        continue
    if not any(c in _s for c in "=(){};<>"):
        continue
    if _s not in _kept:
        raise SystemExit("[gen_dashboard_gz] comment strip dropped code: " + _s[:90])
html = stripped.encode("utf-8")
# mtime=0: gzip embeds a timestamp in its header by default, which would make the
# blob differ on every build even when the HTML is unchanged - defeating the
# write-only-if-changed guard below and forcing a full-TU recompile each time.
gz = gzip.compress(html, 9, mtime=0)
# ETag derived from the gzip bytes, so it changes iff the dashboard content
# changes - not with FIRMWARE_VERSION/GIT_REV. Fixes stale-cache 304s on dirty
# dev builds (same commit hash) and avoids re-sending an unchanged page when a
# release touched only firmware. CRC32 (8 hex) is plenty for cache validation.
etag = "%08x" % (zlib.crc32(gz) & 0xffffffff)

lines = [
    "// AUTO-GENERATED from web/dashboard.html by scripts/gen_dashboard_gz.py.",
    "// Do not edit; git-ignored. Edit web/dashboard.html instead.",
    "#pragma once",
    "#include <pgmspace.h>",
    "const size_t DASHBOARD_HTML_GZ_LEN = %d;" % len(gz),
    "#define DASHBOARD_ETAG \"%s\"" % etag,
    "const uint8_t DASHBOARD_HTML_GZ[] PROGMEM = {",
]
lines += ["".join("0x%02x," % b for b in gz[i:i + 20]) for i in range(0, len(gz), 20)]
lines += ["};", ""]
new = "\n".join(lines)

old = io.open(out, encoding="utf-8").read() if os.path.exists(out) else ""
if new != old:
    io.open(out, "w", encoding="utf-8").write(new)
    print("[gen_dashboard_gz] wrote %d gz bytes (from %d raw, %d after comment strip)"
          % (len(gz), len(raw.encode("utf-8")), len(html)))
else:
    print("[gen_dashboard_gz] up to date (%d gz bytes)" % len(gz))
