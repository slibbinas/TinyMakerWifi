#!/usr/bin/env python3
"""Feedback KV backup (0-13a) - the notes live ONLY in Cloudflare KV, this
pulls every key into a timestamped local folder so a fat-fingered delete or
a lost CF account doesn't erase the project's field feedback.

Usage (any python3; wrangler must be logged in - `npx wrangler login` once):
    python backup_kv.py [out_dir]

Output: <out_dir>/kv-backup-YYYYMMDD-HHMMSS/
    keys.json            - the full key list as wrangler returned it
    <key>.bin            - each key's raw value (feedback JSON, panel HTML,
                           photo binaries alike; key name percent-encoded)

Restore one key:
    npx wrangler kv key put "<key>" --path <file> --namespace-id <NS> --remote

Run it from this folder (wrangler.jsonc's namespace id is parsed from the
file, so a namespace change doesn't silently back up the wrong store).
"""
import datetime
import json
import os
import re
import subprocess
import sys
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
WRANGLER = "npx.cmd" if os.name == "nt" else "npx"


def wrangler(*args):
    r = subprocess.run([WRANGLER, "wrangler", *args], capture_output=True,
                       text=True, cwd=HERE)
    if r.returncode != 0:
        sys.exit(f"wrangler {' '.join(args)} failed:\n{r.stderr[-800:]}")
    return r.stdout


def namespace_id():
    cfg = open(os.path.join(HERE, "wrangler.jsonc"), encoding="utf-8").read()
    m = re.search(r'"kv_namespaces".*?"id":\s*"([0-9a-f]+)"', cfg, re.S)
    if not m:
        sys.exit("kv namespace id not found in wrangler.jsonc")
    return m.group(1)


def main():
    ns = namespace_id()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "kv-backups")
    out = os.path.join(base, f"kv-backup-{stamp}")
    os.makedirs(out, exist_ok=True)

    keys = json.loads(wrangler("kv", "key", "list", "--namespace-id", ns,
                               "--remote"))
    with open(os.path.join(out, "keys.json"), "w", encoding="utf-8") as f:
        json.dump(keys, f, indent=1)
    print(f"{len(keys)} keys -> {out}")

    for i, k in enumerate(keys, 1):
        name = k["name"]
        safe = urllib.parse.quote(name, safe="")
        path = os.path.join(out, safe + ".bin")
        # binary-safe: let wrangler write the file itself
        r = subprocess.run([WRANGLER, "wrangler", "kv", "key", "get", name,
                            "--namespace-id", ns, "--remote"],
                           capture_output=True, cwd=HERE)
        if r.returncode != 0:
            print(f"  SKIP {name}: {r.stderr[-120:]!r}")
            continue
        with open(path, "wb") as f:
            f.write(r.stdout)
        print(f"  {i}/{len(keys)} {name} ({len(r.stdout)} B)")

    print("done - archive the folder somewhere OFF this machine too.")


if __name__ == "__main__":
    main()
