"""PlatformIO pre-script: gzip web/dashboard.html into src/dashboard_html_gz.h.

The dashboard (HTML+CSS+JS) is authored as one static file in web/dashboard.html
and served pre-compressed with Content-Encoding: gzip. Storing the ~43 KB gzip
blob instead of ~143 KB of PROGMEM string literals frees ~99 KB of flash.

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
import zlib

proj = env["PROJECT_DIR"]
src = os.path.join(proj, "web", "dashboard.html")
out = os.path.join(proj, "src", "dashboard_html_gz.h")

html = io.open(src, "rb").read()
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
    print("[gen_dashboard_gz] wrote %d gz bytes (from %d raw)" % (len(gz), len(html)))
else:
    print("[gen_dashboard_gz] up to date (%d gz bytes)" % len(gz))
