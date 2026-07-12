"""PlatformIO pre-script: expose the current git commit as -DGIT_REV.

The firmware's SemVer (FIRMWARE_VERSION) stays clean for OTA/self-update
comparisons; GIT_REV is a display-only build tag shown next to the version in
the dashboard header, so experimental builds (which all report the same
SemVer) can be told apart. Appends "+dirty" when tracked files are modified.
Untracked files (e.g. stray images) are ignored on purpose.
"""

Import("env")
import subprocess


def _git(args):
    try:
        return subprocess.check_output(
            ["git"] + args, cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""


rev = _git(["rev-parse", "--short", "HEAD"]) or "nogit"
dirty = _git(["status", "--porcelain", "--untracked-files=no"])
tag = rev + ("+dirty" if dirty else "")

print("[inject_git_rev] GIT_REV = %s" % tag)
env.Append(CPPDEFINES=[("GIT_REV", '\\"%s\\"' % tag)])
