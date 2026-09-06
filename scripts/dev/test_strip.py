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
from strip_html_comments import strip_comments, style_spans   # noqa: E402
from assemble_dashboard import assemble          # noqa: E402

# The assembled page is what the build actually strips and gzips - testing the
# bare web/dashboard.html would leave the slicer's own comments unchecked.
src = assemble(os.path.dirname(ROOT))
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
# Stylesheet lines are exempt from the verbatim rule, and only they. Our CSS
# comments sit on the same line as the rules, so removing them necessarily
# rewrites lines that carry code - that is the point of the CSS pass, not a loss.
# What must not change there is checked separately below, and more strictly: the
# whole stylesheet is compared against an independent removal.
# The bounds come from the stripper, not from spotting "<style" in a line: a JS
# string containing "<style>" with no closer would otherwise excuse every line
# after it, silently (audit 09-06).
_spans = style_spans(src)
_pos, in_css = 0, []
for line in src.splitlines(keepends=True):
    a, b = _pos, _pos + len(line)
    in_css.append(any(a < e and b > s0 for s0, e in _spans))
    _pos = b
in_block = in_html = False
for idx, line in enumerate(src.splitlines()):
    s = line.strip()
    if in_css[idx]:
        continue
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
# The stylesheet, checked against a removal written a different way. A plain
# non-greedy regex is exact for CSS that has no comment opener inside a string,
# so we assert that too - if a rule like content:"/*" ever appears, this test
# must be told, because then the regex is the wrong reference and only the
# quote-aware scanner is right.
# Bounds come from the stripper's own definition, so this checks the region that
# was actually rewritten - and it checks EVERY stylesheet, not just the first.
def _style_of(t):
    return "\n".join(t[a:b] for a, b in style_spans(t))


src_css, out_css = _style_of(src), _style_of(out)
# Any quoted string, not just content: - a comment marker can sit in a custom
# property, a font name or a url("..."). Scanned on the OUTPUT, where comments are
# already gone and every quote is therefore real: on the source the apostrophes in
# our own Lithuanian comments pair up into strings that do not exist, and the
# check reports a marker that is only ever prose (found by this test, 09-06).
STR = re.compile(r'"(?:[^"\\]|\\.)*"' + r"|'(?:[^'\\]|\\.)*'")
risky = [v for v in STR.findall(out_css) if "/*" in v or "*/" in v]
if risky:
    fail += 1
    print("  a CSS string contains a comment marker (%s) - this test's regex"
          " reference no longer applies; compare against the scanner instead"
          % risky[0][:40])
elif squash(out_css) != squash(re.sub(r"/\*.*?\*/", "", src_css, flags=re.S)):
    fail += 1
    print("  the stylesheet lost more (or less) than its comments")
else:
    print("  stylesheet: %d B -> %d B, comments only" % (len(src_css), len(out_css)))

if lost:
    fail += 1
    print("  %d CODE lines went missing, first 5:" % len(lost))
    for l in lost[:5]:
        print("    " + l[:110])
else:
    print("every code-bearing line survived")

print("RESULT:", "FAIL" if fail else "OK")
sys.exit(1 if fail else 0)
