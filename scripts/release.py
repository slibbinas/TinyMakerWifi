#!/usr/bin/env python3
"""TinyMakerWiFi release automation.

Does, in order (asking once before anything is pushed/published):
  1. sanity checks    - clean working tree, on main, version not yet tagged
  2. build            - pio run (both envs), reports RAM/Flash
  3. push + tag       - origin main + vX.Y.Z
  4. gh-pages         - firmware.bin, firmware-X.Y.Z.bin, version.txt,
                        versions.txt manifest (newest first)
  5. GitHub Release   - vX.Y.Z with firmware.bin + firmware-full.bin attached

The version is read from FIRMWARE_VERSION in platformio.ini - bump it (both
envs!) and commit before running. Release notes: --notes-file <path> or a
minimal default.

Usage (from the repo root):
  %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe scripts\\release.py
  ... release.py --notes-file notes.md   # richer GitHub Release body
  ... release.py --dry-run               # everything except push/publish

Auth: the GitHub token is taken from the git credential helper (the same one
`git push` uses) - nothing to configure.
"""

import argparse
import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = Path("C:/PIO-build/TinyMakerWiFi/tinymaker")
GHPAGES_WORKTREE = Path("C:/PIO-build/ghp-wt")
PIO = Path.home() / ".platformio/penv/Scripts/platformio.exe"
GH_REPO = "slibbinas/TinyMakerWiFi"
PAGES_URL = "https://slibbinas.github.io/TinyMakerWifi"


def run(cmd, cwd=REPO_ROOT, capture=False, check=True):
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    r = subprocess.run(cmd, cwd=cwd, check=check,
                       capture_output=capture, text=True)
    return r.stdout if capture else None


def fail(msg):
    print(f"ERROR: {msg}")
    sys.exit(1)


def read_version():
    ini = (REPO_ROOT / "platformio.ini").read_text(encoding="utf-8")
    versions = set(re.findall(r'FIRMWARE_VERSION=\\"(\d+\.\d+\.\d+)\\"', ini))
    if len(versions) != 1:
        fail(f"expected one FIRMWARE_VERSION in platformio.ini, found: {versions or 'none'}")
    return versions.pop()


def github_token():
    r = subprocess.run(["git", "credential", "fill"], cwd=REPO_ROOT,
                       input="protocol=https\nhost=github.com\n\n",
                       capture_output=True, text=True, check=True)
    for line in r.stdout.splitlines():
        if line.startswith("password="):
            return line.split("=", 1)[1]
    fail("no GitHub token from the git credential helper")


def gh_api(token, method, url, payload=None, content_type="application/json"):
    data = None
    if payload is not None:
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "Content-Type": content_type,
        "User-Agent": "tinymaker-release",
    })
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read() or b"{}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--notes-file", help="markdown file with the GitHub Release body")
    ap.add_argument("--dry-run", action="store_true",
                    help="build + checks only; no push, no publish")
    ap.add_argument("--yes", action="store_true", help="skip the confirmation prompt")
    ap.add_argument("--beta", action="store_true",
                    help="beta release: publish the Release, the versioned bin and the "
                         "picker manifest, but do NOT touch version.txt/firmware.bin - "
                         "self-update stays on the previous version until promotion")
    args = ap.parse_args()

    version = read_version()
    tag = f"v{version}"
    print(f"== TinyMakerWiFi release {tag} ==")

    # --- 1. sanity checks -----------------------------------------------------
    if run(["git", "status", "--porcelain"], capture=True).strip():
        fail("working tree is not clean - commit or stash first")
    branch = run(["git", "rev-parse", "--abbrev-ref", "HEAD"], capture=True).strip()
    if branch != "main":
        fail(f"on branch '{branch}', releases are cut from main")
    tags = run(["git", "tag", "-l", tag], capture=True).strip()
    if tags:
        fail(f"tag {tag} already exists - bump FIRMWARE_VERSION first")
    notes = f"Release {version}."
    if args.notes_file:
        notes = Path(args.notes_file).read_text(encoding="utf-8")

    # --- 2. build -------------------------------------------------------------
    print("-- building (pio run, both envs)")
    out = run([str(PIO), "run"], capture=True)
    for line in out.splitlines():
        if "RAM:" in line or "Flash:" in line or "SUCCESS" in line:
            print("   " + line.strip())
    fw = BUILD_DIR / "firmware.bin"
    fw_full = BUILD_DIR / "firmware-full.bin"
    for f in (fw, fw_full):
        if not f.exists():
            fail(f"build artifact missing: {f}")

    if args.dry_run:
        print("== dry run: stopping before push/publish ==")
        return

    if not args.yes:
        answer = input(f"Push main, publish gh-pages and GitHub Release {tag}? [y/N] ")
        if answer.strip().lower() != "y":
            print("aborted")
            return

    # --- 3. push + tag ----------------------------------------------------------
    print("-- pushing main + tag")
    run(["git", "tag", tag])
    run(["git", "push", "origin", "main", tag])

    # --- 4. gh-pages ------------------------------------------------------------
    print("-- publishing gh-pages")
    run(["git", "fetch", "origin", "gh-pages"])
    if not GHPAGES_WORKTREE.exists():
        run(["git", "worktree", "add", str(GHPAGES_WORKTREE), "gh-pages"])
    run(["git", "reset", "--hard", "origin/gh-pages"], cwd=GHPAGES_WORKTREE)

    data = fw.read_bytes()
    (GHPAGES_WORKTREE / f"firmware-{version}.bin").write_bytes(data)
    if args.beta:
        # Beta window: printers self-update only from version.txt + firmware.bin,
        # so leaving both untouched keeps the fleet on the previous release while
        # the picker (versions.txt) already offers the new one to testers.
        # version-beta.txt is the beta channel pointer (firmware's Stable/Beta
        # update-channel switch reads it) - points at the versioned bin.
        (GHPAGES_WORKTREE / "version-beta.txt").write_text(
            f"{version}\n{PAGES_URL}/firmware-{version}.bin\n", newline="\n")
        print(f"   beta: version.txt/firmware.bin untouched; version-beta.txt -> {version}")
    else:
        (GHPAGES_WORKTREE / "firmware.bin").write_bytes(data)
        (GHPAGES_WORKTREE / "version.txt").write_text(
            f"{version}\n{PAGES_URL}/firmware.bin\n", newline="\n")

    # versions.txt: newest first, one X.Y.Z per line (the dashboard's picker)
    manifest_path = GHPAGES_WORKTREE / "versions.txt"
    existing = []
    if manifest_path.exists():
        existing = [l.strip() for l in manifest_path.read_text().splitlines()
                    if re.fullmatch(r"\d+\.\d+\.\d+", l.strip())]
    if version not in existing:
        existing.insert(0, version)
    manifest_path.write_text("\n".join(existing) + "\n", newline="\n")

    run(["git", "add", "-A"], cwd=GHPAGES_WORKTREE)
    run(["git", "commit", "-m", f"gh-pages: firmware {version}"], cwd=GHPAGES_WORKTREE)
    run(["git", "push", "origin", "gh-pages"], cwd=GHPAGES_WORKTREE)

    # --- 5. GitHub Release --------------------------------------------------------
    print("-- creating GitHub Release")
    token = github_token()
    rel = gh_api(token, "POST", f"https://api.github.com/repos/{GH_REPO}/releases",
                 {"tag_name": tag, "name": tag, "body": notes})
    for f in (fw, fw_full):
        print(f"   uploading {f.name}")
        gh_api(token, "POST",
               f"https://uploads.github.com/repos/{GH_REPO}/releases/{rel['id']}/assets?name={f.name}",
               f.read_bytes(), content_type="application/octet-stream")

    print(f"== released {tag} ==")
    print(f"   {rel.get('html_url', '')}")
    print(f"   {PAGES_URL}/version.txt")


if __name__ == "__main__":
    main()
