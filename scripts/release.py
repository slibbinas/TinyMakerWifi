#!/usr/bin/env python3
"""TinyMakerWiFi release automation.

Does, in order (asking once before anything is pushed/published):
  1. sanity checks    - clean working tree, on main, version not yet tagged,
                        manual's version strings match
  2. build            - pio run (both envs), reports RAM/Flash
  3. push + tag       - origin main + vX.Y.Z
  4. gh-pages         - firmware.bin, firmware-X.Y.Z.bin, version.txt,
                        versions.txt manifest (newest first)
  5. GitHub Release   - vX.Y.Z with firmware.bin + firmware-full.bin attached
                        (--beta marks it prerelease and not "Latest", so
                        /releases/latest - and the web flasher behind it -
                        keeps handing out the current stable)

--promote turns an already-published beta into the stable release: it points
version.txt/firmware.bin at the binary already on gh-pages, clears the
prerelease flag, and trims the picker to a window (the new stable, anything
newer, the outgoing stable and the two versions below). It does NOT build -
the bytes that were tested are the bytes that ship.

The version is read from FIRMWARE_VERSION in platformio.ini - bump it (both
envs!) and commit before running. Release notes: --notes-file <path> or a
minimal default.

Usage (from the repo root):
  %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe scripts\\release.py
  ... release.py --notes-file notes.md   # richer GitHub Release body
  ... release.py --dry-run               # everything except push/publish
  ... release.py --beta                  # publish, but leave self-update alone
  ... release.py --promote               # make this version's beta the stable one

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


# The manual carries the version in two hand-written spots (hero badge, footer)
# and nothing else ever touches them: 0.15.1..0.15.4 all shipped a manual that
# still said 0.15.0, through a docs flush that rewrote its content. Checked here
# rather than patched, because the fix has to land in the release commit - by the
# time gh-pages is published the tag is already pushed.
def check_manual_version(version):
    manual = REPO_ROOT / "docs/manual/index.html"
    stale = set(re.findall(r"User Manual · v(\d+\.\d+\.\d+)", manual.read_text(encoding="utf-8")))
    stale.discard(version)
    if stale:
        fail(f"docs/manual/index.html still says v{', v'.join(sorted(stale))} - "
             f"set both spots (hero badge, footer) to v{version} and commit")


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


# The picker offered every version ever shipped and gh-pages kept every binary
# (~1.4 MB each) - a list nobody can reason about and a folder that only grows.
# Keep a window instead: the stable release, everything newer (the betas), and
# the two stable releases before it, so going back a step stays one click.
# Nothing is lost - every release keeps its own GitHub Release page with both
# binaries, and older firmware can still be flashed from a file.
KEEP_OLDER_STABLE = 2


def prune_versions(worktree, versions, stable, outgoing=None):
    def key(v):
        return tuple(int(n) for n in v.split("."))
    newer = [v for v in versions if key(v) > key(stable)]
    older = [v for v in versions if key(v) < key(stable)]
    # The outgoing stable is kept by name, not by position: the versions just
    # below a promotion are its own betas, so a window counted purely by
    # distance would drop the last release that ever shipped to everyone -
    # exactly the one to fall back to if the new line turns out bad.
    keep = set(newer + [stable] + older[:KEEP_OLDER_STABLE])
    if outgoing and outgoing in versions:
        keep.add(outgoing)
    dropped = [v for v in versions if v not in keep]
    for v in dropped:
        b = worktree / f"firmware-{v}.bin"
        if b.exists():
            b.unlink()
    if dropped:
        print(f"   picker window: dropped {', '.join(dropped)} (still on their Release pages)")
    return [v for v in versions if v in keep]


def promote(version):
    """Make an already-published beta the stable release.

    Deliberately does not build: the whole point of a beta window is that the
    tested bytes ship unchanged, so this republishes the binary that is already
    on gh-pages rather than compiling a fresh one from a tree that may have
    moved on.
    """
    tag = f"v{version}"
    print(f"== promoting {tag} to stable ==")
    run(["git", "fetch", "origin", "gh-pages"])
    if not GHPAGES_WORKTREE.exists():
        run(["git", "worktree", "add", str(GHPAGES_WORKTREE), "gh-pages"])
    run(["git", "reset", "--hard", "origin/gh-pages"], cwd=GHPAGES_WORKTREE)

    src = GHPAGES_WORKTREE / f"firmware-{version}.bin"
    if not src.exists():
        fail(f"{src.name} is not on gh-pages - release the beta first")

    # Read before overwriting: this is the release everyone is on right now,
    # and it stays in the picker as the way back.
    outgoing = ""
    vt = GHPAGES_WORKTREE / "version.txt"
    if vt.exists():
        outgoing = vt.read_text().splitlines()[0].strip()
    if outgoing == version:
        fail(f"version.txt already points at {version} - nothing to promote")

    (GHPAGES_WORKTREE / "firmware.bin").write_bytes(src.read_bytes())
    (GHPAGES_WORKTREE / "version.txt").write_text(
        f"{version}\n{PAGES_URL}/firmware.bin\n", newline="\n")
    # The beta pointer follows stable: with nothing newer published, a printer on
    # the beta channel would otherwise keep being offered what it already runs.
    (GHPAGES_WORKTREE / "version-beta.txt").write_text(
        f"{version}\n{PAGES_URL}/firmware-{version}.bin\n", newline="\n")

    manifest_path = GHPAGES_WORKTREE / "versions.txt"
    existing = []
    if manifest_path.exists():
        existing = [l.strip() for l in manifest_path.read_text().splitlines()
                    if re.fullmatch(r"\d+\.\d+\.\d+", l.strip())]
    kept = prune_versions(GHPAGES_WORKTREE, existing, version, outgoing)
    manifest_path.write_text("\n".join(kept) + "\n", newline="\n")

    run(["git", "add", "-A"], cwd=GHPAGES_WORKTREE)
    run(["git", "commit", "-m", f"gh-pages: promote {version} to stable"], cwd=GHPAGES_WORKTREE)
    run(["git", "push", "origin", "gh-pages"], cwd=GHPAGES_WORKTREE)

    # /releases/latest is what the web flasher hands newcomers - it skips
    # prereleases, so this flag is the difference between a beta and the
    # version a first-time user gets.
    print("-- clearing the prerelease flag")
    token = github_token()
    rel = gh_api(token, "GET", f"https://api.github.com/repos/{GH_REPO}/releases/tags/{tag}")
    gh_api(token, "PATCH", f"https://api.github.com/repos/{GH_REPO}/releases/{rel['id']}",
           {"prerelease": False, "make_latest": "true"})

    print(f"== {tag} is stable ==")
    print(f"   {PAGES_URL}/version.txt -> {version}")
    print(f"   printers on the stable channel will offer it within a minute")


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
    ap.add_argument("--promote", action="store_true",
                    help="promote the already-published beta of this version to stable: "
                         "point version.txt/firmware.bin at it and drop the Release's "
                         "prerelease flag. No build, no tag - the bytes that were tested "
                         "are the bytes that ship")
    args = ap.parse_args()

    version = read_version()
    if args.promote:
        promote(version)
        return
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
    check_manual_version(version)
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
    # A beta must not become GitHub's "Latest": /releases/latest skips
    # prereleases, and that endpoint is what the web flasher hands newcomers.
    # Without this the fleet stayed on stable (version.txt untouched) while
    # anyone browsing Releases - or flashing over USB - was served the beta.
    # 0.15.0 shipped without it and had to be flipped by hand.
    rel = gh_api(token, "POST", f"https://api.github.com/repos/{GH_REPO}/releases",
                 {"tag_name": tag, "name": tag, "body": notes,
                  "prerelease": bool(args.beta),
                  "make_latest": "false" if args.beta else "true"})
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
