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


def strip_comments(text):
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
