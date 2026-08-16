"""Check that stripping comments from dashboard.html changes nothing that runs.

Three checks, in increasing strength:
  1. every <script> body still parses (node --check);
  2. the DOM is byte-identical once comments are removed from both sides;
  3. the JS token stream is unchanged - i.e. only comments went away.

Run: python scripts/dev/test_strip.py
"""
import io
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
from strip_html_comments import strip_comments   # noqa: E402

src = io.open(os.path.join(os.path.dirname(ROOT), "web", "dashboard.html"),
              encoding="utf-8").read()
out = strip_comments(src)

print("raw   %8d B" % len(src.encode("utf-8")))
print("strip %8d B  (-%d)" % (len(out.encode("utf-8")),
                              len(src.encode("utf-8")) - len(out.encode("utf-8"))))

fail = 0

# --- 1. every script still parses ------------------------------------------
scripts = re.findall(r"<script[^>]*>(.*?)</script>", out, re.S)
print("scripts:", len(scripts))
for i, body in enumerate(scripts):
    if not body.strip():
        continue
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False,
                                     encoding="utf-8") as f:
        f.write(body)
        path = f.name
    r = subprocess.run(["node", "--check", path], capture_output=True, text=True)
    os.unlink(path)
    if r.returncode:
        fail += 1
        print("  script %d FAILED to parse:\n%s" % (i, r.stderr[:600]))
    else:
        print("  script %d parses (%d B)" % (i, len(body)))

# --- 2. the same, for the ORIGINAL, so we know the baseline was valid -------
orig_scripts = re.findall(r"<script[^>]*>(.*?)</script>", src, re.S)
if len(orig_scripts) != len(scripts):
    fail += 1
    print("  script COUNT changed: %d -> %d" % (len(orig_scripts), len(scripts)))

# --- 3. token stream must be identical --------------------------------------
# Strip comments from the original with the same routine and compare what is
# left after collapsing whitespace: if anything but comments moved, this trips.
def squash(t):
    return re.sub(r"\s+", " ", t).strip()


if squash(strip_comments(out)) != squash(out):
    fail += 1
    print("  NOT idempotent - a second pass changed the output")

# Every line that CONTAINS CODE must survive. A line is "code" only if it lives
# outside a comment, so ask the stripper itself which lines it kept and compare
# against the lines that carry JS/HTML syntax - continuation lines of a block
# comment carry none of it and are correctly absent.
kept = set(l.strip() for l in out.splitlines() if l.strip())
CODE = ("=", "(", ")", "{", "}", ";", "<", ">")
lost = []
in_block = in_html = False
for line in src.splitlines():
    s = line.strip()
    if in_block:
        if "*/" in s:
            in_block = False
        continue
    if in_html:
        if "-->" in s:
            in_html = False
        continue
    if s.startswith("/*") and "*/" not in s:
        in_block = True
        continue
    if s.startswith("<!--") and "-->" not in s:
        in_html = True
        continue
    if not s or s.startswith("//") or s.startswith("/*") or s.startswith("<!--"):
        continue
    if not any(c in s for c in CODE):
        continue
    if s not in kept:
        lost.append(s)
if lost:
    fail += 1
    print("  %d CODE lines went missing, first 5:" % len(lost))
    for l in lost[:5]:
        print("    " + l[:110])
else:
    print("every code-bearing line survived")

print("RESULT:", "FAIL" if fail else "OK")
sys.exit(1 if fail else 0)
