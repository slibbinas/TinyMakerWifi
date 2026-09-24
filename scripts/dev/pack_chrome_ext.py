"""Pack the TinyMaker Chrome extension for the Chrome Web Store (and the manual-install
download on gh-pages).

    python scripts/dev/pack_chrome_ext.py [out_dir]     (default: the current folder)

Writes tinymaker-chrome-<manifest version>.zip with manifest.json at the zip root - the
store rejects a package whose manifest sits in a subfolder. README.md stays out: it is
for this repo, not for the browser.
"""
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "tinymaker-chrome")
SKIP = {"README.md"}


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    with open(os.path.join(SRC, "manifest.json"), encoding="utf-8") as f:
        version = json.load(f)["version"]
    out = os.path.abspath(os.path.join(out_dir, "tinymaker-chrome-%s.zip" % version))
    names = []
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _dirs, files in os.walk(SRC):
            for fn in sorted(files):
                rel = os.path.relpath(os.path.join(root, fn), SRC).replace(os.sep, "/")
                if rel in SKIP:
                    continue
                z.write(os.path.join(root, fn), rel)
                names.append(rel)
    print(out)
    print("  %d files: %s" % (len(names), ", ".join(names)))


if __name__ == "__main__":
    main()
