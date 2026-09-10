"""Strip whole-line comments from dashboard.html before it is gzipped.

The dashboard is 34 % comments by weight. They are worth every byte in the
source - they are where the reasons live - but they cost ~60 KB of flash once
gzipped into the firmware, and flash is the scarce resource on a 4 MB ESP32.
So: keep them in git, drop them on the way into the binary.

Deliberately conservative. Only comments that occupy a WHOLE line are removed.
That one rule avoids both classic hazards of stripping JS:

  * `//` inside a string or a URL ("https://...") never starts a line;
  * the `/` of a regex literal never starts a line as `//` or `/*`
    (an empty regex `//` is not valid JS).

Trailing comments after code are left untouched - they are the minority and not
worth the risk. Lines inside a multi-line template literal are also left alone,
tracked by counting unescaped backticks.

CSS is the exception, and it gets its own pass first. Inside <style> our comments
sit on the same line as the rules they explain, so the whole-line rule never saw
them: measured 2026-09-06 on the dashboard as served by the printer, 79 comments
totalling 20 046 B - about 38 % of the stylesheet - reached the flash, roughly
9.4 KB of it after gzip. They were also readable by anyone who opened the page
source, which is not where internal notes belong.

Stripping them is safe for the very reason it is unsafe in JS: CSS has no //
comments, no regex literals and no template literals, so /* is unambiguous
everywhere except inside a string. The scanner therefore tracks quotes, which is
what keeps a rule like content:"/*" intact.

Used by scripts/gen_dashboard_gz.py; tested by scripts/dev/test_strip.py.
"""


def _backticks(line):
    """Count backticks that actually open/close a template literal."""
    n, i, q = 0, 0, None
    while i < len(line):
        c = line[i]
        if c == "\\":
            i += 2
            continue
        if q:
            if c == q:
                q = None
        elif c in "'\"":
            q = c
        elif c == "`":
            n += 1
        elif c == "/" and i + 1 < len(line) and line[i + 1] == "/":
            break          # a trailing comment - stop counting here
        i += 1
    return n


import re

# One definition of "where the stylesheet is", shared by everything that needs to
# know: the stripper, the build guard and the test. Three separate answers to
# that question is how a guard ends up watching a different region than the one
# being rewritten.
STYLE_RE = re.compile(r"<style[^>]*>(.*?)</style>", re.S | re.I)


def style_spans(text):
    """(start, end) of every stylesheet body, in source coordinates."""
    return [(m.start(1), m.end(1)) for m in STYLE_RE.finditer(text)]


def _css_no_comments(css):
    """Drop every /* ... */ from a stylesheet, leaving strings alone.

    Character-by-character rather than a regex: the one case that must survive is
    a comment opener inside a string - content:"/*" is legal CSS and a regex would
    eat the rest of the sheet from there.
    """
    out, i, n = [], 0, len(css)
    quote, in_comment = None, False
    while i < n:
        c = css[i]
        if in_comment:
            if c == "*" and i + 1 < n and css[i + 1] == "/":
                in_comment = False
                i += 2
                continue
            i += 1
            continue
        if quote:
            out.append(c)
            if c == "\\" and i + 1 < n:      # escaped char inside a string
                out.append(css[i + 1])
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and css[i + 1] == "*":
            in_comment = True
            i += 2
            continue
        out.append(c)
        i += 1
    if in_comment:
        # An opener with no closer would silently swallow everything after it -
        # including real rules. Two ways to get here, both legal CSS elsewhere:
        # a genuinely unterminated comment, and an unquoted url(http://x/*y).
        # Fail the build instead (audit 09-06).
        raise ValueError("unterminated /* in the stylesheet - refusing to strip")
    if quote:
        raise ValueError("unbalanced quote in the stylesheet - refusing to strip")
    return "".join(out)


def _strip_style_blocks(text):
    """Run _css_no_comments over the contents of every <style> element."""
    out, pos = [], 0
    for a, b in style_spans(text):
        out.append(text[pos:a])
        out.append(_css_no_comments(text[a:b]))
        pos = b
    out.append(text[pos:])
    return "".join(out)


def strip_comments(text):
    # CSS pirma: ten komentarai stovi eilutes viduryje, tad zemiau esanti
    # eilutine taisykle ju nepagauna (zr. modulio docstring'a).
    text = _strip_style_blocks(text)
    out = []
    in_tpl = False          # inside a multi-line template literal
    in_block = False        # inside a whole-line /* ... */ block
    in_html = False         # inside a whole-line <!-- ... --> block

    for line in text.split("\n"):
        s = line.strip()

        if in_block:
            if "*/" in s:
                in_block = False
                rest = s.split("*/", 1)[1]
                if rest.strip():
                    out.append(line.split("*/", 1)[1])
            continue
        if in_html:
            if "-->" in s:
                in_html = False
                rest = s.split("-->", 1)[1]
                if rest.strip():
                    out.append(line.split("-->", 1)[1])
            continue

        if not in_tpl:
            if s.startswith("//"):
                continue
            if s.startswith("<!--"):
                if "-->" in s:
                    if not s.split("-->", 1)[1].strip():
                        continue
                else:
                    in_html = True
                    continue
            if s.startswith("/*"):
                if "*/" in s:
                    if not s.split("*/", 1)[1].strip():
                        continue
                else:
                    in_block = True
                    continue

        out.append(line)
        if _backticks(line) % 2:
            in_tpl = not in_tpl

    # the removals leave runs of blank lines behind; keep at most one
    packed, blank = [], False
    for line in out:
        if not line.strip():
            if blank:
                continue
            blank = True
        else:
            blank = False
        packed.append(line)
    return "\n".join(packed)
