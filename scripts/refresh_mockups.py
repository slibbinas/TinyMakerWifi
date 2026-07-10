#!/usr/bin/env python3
"""Refresh the version strings inside the README mockup PNGs.

The mockups in Images/mockups/ are generated illustrations (not photos); the
original full generators were ad-hoc and are gone, so this script only
*patches the version text* at fixed coordinates: it fills the old text with
the sampled background color and draws the new string on top. Re-running is
idempotent as long as the patch boxes below stay generous.

Patched spots:
  printer-screens.png       update tile ("Installed: vX / Latest: vY"),
                            About tile ("FW: vY"), WiFi-info tile ("FW vY")
  web-dashboard.png         "Firmware Y" under the title
  firmware-update-page.png  regenerated in full (dashboard Update tab:
                            installed/latest, Install latest, version
                            picker, upload) - see draw_update_page()

Versions: "latest" comes from FIRMWARE_VERSION in platformio.ini, "installed"
(the update tile's old version) from the newest git tag below it - override
with --installed. Run before committing a release:

  %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe scripts\\refresh_mockups.py

Needs Pillow in that python (pip install pillow, one-time).
"""

import argparse
import re
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
MOCKUPS = REPO_ROOT / "Images" / "mockups"
CONSOLA = r"C:\Windows\Fonts\consola.ttf"
SEGOE = r"C:\Windows\Fonts\segoeui.ttf"

ORANGE = (232, 114, 12)
BLUE = (132, 188, 248)


def read_latest_version():
    ini = (REPO_ROOT / "platformio.ini").read_text(encoding="utf-8")
    versions = set(re.findall(r'FIRMWARE_VERSION=\\"(\d+\.\d+\.\d+)\\"', ini))
    if len(versions) != 1:
        raise SystemExit(f"expected one FIRMWARE_VERSION, found {versions or 'none'}")
    return versions.pop()


def semver_key(v):
    return tuple(int(p) for p in v.split("."))


def read_installed_version(latest):
    """Newest git tag strictly below `latest` (the mockup shows an update
    being available: Installed < Latest)."""
    out = subprocess.run(["git", "tag", "-l", "v*"], cwd=REPO_ROOT,
                         capture_output=True, text=True, check=True).stdout
    tags = [t.strip().lstrip("v") for t in out.splitlines()
            if re.fullmatch(r"v\d+\.\d+\.\d+", t.strip())]
    older = [t for t in tags if semver_key(t) < semver_key(latest)]
    return max(older, key=semver_key) if older else latest


def patch(img, draw, box, text_runs, probe):
    """Fill `box` with the bg color sampled at `probe`, then draw the
    (x_offset, text, font, color) runs starting at the box origin."""
    bg = img.getpixel(probe)
    draw.rectangle(box, fill=bg, outline=bg)
    for dx, text, font, color in text_runs:
        draw.text((box[0] + dx, box[1]), text, font=font, fill=color)


def _seg(size, bold=False):
    return ImageFont.truetype(r"C:\Windows\Fonts\segoeui%s.ttf" % ("b" if bold else ""), size)


def _center(d, box, text, font, fill):
    bb = font.getbbox(text)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    d.text((box[0] + (box[2] - box[0] - w) / 2 - bb[0],
            box[1] + (box[3] - box[1] - h) / 2 - bb[1]), text, font=font, fill=fill)


def draw_update_page(installed, latest):
    """Full redraw of firmware-update-page.png: the dashboard's Update tab
    (browser chrome + tabs + installed/latest + Install latest + version
    picker + upload), styled to match web-dashboard.png."""
    WHITE = (238, 238, 238)
    GRAY = (170, 170, 170)
    CARD = (42, 42, 46)
    FIELD = (28, 28, 30)
    BTN2 = (58, 58, 64)

    img = Image.new("RGB", (1120, 1330), (0, 0, 0))
    d = ImageDraw.Draw(img)

    # browser window + chrome bar
    d.rounded_rectangle((40, 40, 1080, 1290), 26, fill=(28, 28, 30))
    d.rounded_rectangle((40, 40, 1080, 150), 26, fill=(35, 35, 38))
    d.rectangle((40, 110, 1080, 150), fill=(28, 28, 30))
    for cx, col in ((80, (255, 95, 87)), (115, (254, 188, 46)), (150, (40, 200, 64))):
        d.ellipse((cx - 11, 84, cx + 11, 106), fill=col)
    d.rounded_rectangle((185, 62, 935, 128), 18, fill=(22, 22, 24))
    d.rounded_rectangle((205, 74, 247, 116), 9, fill=ORANGE)
    _center(d, (205, 74, 247, 116), "T", _seg(26, True), (255, 255, 255))
    d.text((265, 78), "192.168.1.42", font=_seg(27), fill=(200, 200, 205))

    # orange wrap + title
    d.rounded_rectangle((75, 185, 1045, 1250), 16, fill=(35, 35, 38), outline=ORANGE, width=3)
    d.text((110, 212), "TinyMaker", font=_seg(42, True), fill=ORANGE)
    d.text((112, 278), f"Firmware {installed}", font=_seg(21), fill=GRAY)

    # tabs (Update active)
    for box, label, fill in (((110, 330, 398, 396), "Dashboard", BTN2),
                             ((415, 330, 703, 396), "Settings", BTN2),
                             ((720, 330, 1008, 396), "Update", ORANGE)):
        d.rounded_rectangle(box, 14, fill=fill)
        _center(d, box, label, _seg(24, True), (255, 255, 255))

    # card content
    d.rounded_rectangle((110, 430, 1008, 1215), 14, fill=CARD)
    d.text((140, 462), "Firmware update", font=_seg(28, True), fill=WHITE)
    d.text((140, 530), "Installed", font=_seg(20), fill=GRAY)
    d.text((140, 560), installed, font=_seg(30), fill=WHITE)
    d.text((560, 530), "Latest", font=_seg(20), fill=GRAY)
    d.text((560, 560), latest, font=_seg(30), fill=WHITE)
    d.text((140, 622), "A newer firmware is available.", font=_seg(20), fill=GRAY)

    d.rounded_rectangle((140, 668, 978, 730), 12, fill=ORANGE)
    _center(d, (140, 668, 978, 730), "Install latest", _seg(24, True), (255, 255, 255))

    d.text((140, 766), "Install a specific version", font=_seg(20), fill=GRAY)
    d.rounded_rectangle((140, 800, 600, 858), 10, fill=FIELD, outline=(85, 85, 90), width=2)
    d.text((165, 812), latest, font=_seg(24), fill=WHITE)
    d.polygon([(556, 822), (580, 822), (568, 838)], fill=GRAY)
    d.rounded_rectangle((622, 800, 978, 858), 10, fill=BTN2)
    _center(d, (622, 800, 978, 858), "Install selected", _seg(22, True), WHITE)

    d.text((140, 902), "Or upload a firmware.bin from", font=_seg(20), fill=GRAY)
    w = _seg(20).getbbox("Or upload a firmware.bin from ")[2]
    d.text((140 + w, 902), "GitHub Releases:", font=_seg(20), fill=BLUE)
    d.rounded_rectangle((140, 940, 978, 1000), 10, fill=FIELD, outline=(85, 85, 90), width=2)
    d.text((168, 953), "firmware.bin", font=_seg(24), fill=WHITE)
    d.rounded_rectangle((140, 1022, 978, 1084), 12, fill=ORANGE)
    _center(d, (140, 1022, 978, 1084), "Upload & flash", _seg(24, True), (255, 255, 255))

    d.text((140, 1120), "Updates are blocked while printing.", font=_seg(19), fill=GRAY)
    d.text((140, 1152), "Do not power off - the printer reboots by itself when done.",
           font=_seg(19), fill=GRAY)
    return img


def _pawn_slices(n=36, gw=80, gh=60):
    """Procedural chess-pawn-ish model as boolean slice grids (for the
    preview mockup - looks like a real sliced model without needing one)."""
    import math
    slices = []
    cx, cy = gw * 0.5, gh * 0.5
    for k in range(n):
        t = k / (n - 1)
        if t < 0.10:
            r_mm = 9.0                        # base disc
        elif t < 0.60:
            r_mm = 4.0 + 3.0 * (0.60 - t)     # tapering stem
        elif t < 0.70:
            r_mm = 6.0                        # collar
        else:
            a = (t - 0.85) / 0.15             # head sphere
            r_mm = 7.0 * math.sqrt(max(0.0, 1 - a * a))
        rx = r_mm / 40.8 * gw
        ry = r_mm / 30.6 * gh
        s = bytearray(gw * gh)
        for j in range(gh):
            for i in range(gw):
                if ((i - cx) / rx) ** 2 + ((j - cy) / ry) ** 2 <= 1 if rx > 0 and ry > 0 else False:
                    s[j * gw + i] = 1
        slices.append(bytes(s))
    return slices


def _draw_iso(d, x0, y0, slices, gw, gh, model_h, done_frac):
    """PIL version of the dashboard's drawIso (cube + shaded slice stack)."""
    S, CX, CY = 5.4, x0 + 434, y0 + 322
    MX, MY, MZ = 40.8, 30.6, 68.0

    def pt(x, y, z):
        return (CX + (x - y) * 0.866 * S, CY + (x + y) * 0.35 * S - z * 0.8 * S)

    corners = [(0, 0, 0), (MX, 0, 0), (MX, MY, 0), (0, MY, 0),
               (0, 0, MZ), (MX, 0, MZ), (MX, MY, MZ), (0, MY, MZ)]
    for a, b in [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4),
                 (0, 4), (1, 5), (2, 6), (3, 7)]:
        d.line([pt(*corners[a]), pt(*corners[b])], fill=(74, 74, 82))

    n = len(slices)
    for k in range(n):
        t = k / (n - 1)
        z = t * model_h
        s = slices[k]
        solid = t <= done_frac
        for j in range(gh - 1, -1, -1):
            for i in range(gw):
                if not s[j * gw + i]:
                    continue
                edge = (i == 0 or i == gw - 1 or j == 0 or j == gh - 1 or
                        not s[j * gw + i - 1] or not s[j * gw + i + 1] or
                        not s[(j - 1) * gw + i] or not s[(j + 1) * gw + i])
                if solid:
                    lit = (0.55 + 0.45 * ((i / gw) + (1 - j / gh)) / 2) * (0.5 if edge else 1)
                    col = (int((150 + 105 * t) * lit), int((80 + 90 * t) * lit),
                           int((30 + 50 * t) * lit))
                else:
                    col = (56, 56, 66) if edge else (34, 34, 40)
                x, y = pt((i + 0.5) / gw * MX, (j + 0.5) / gh * MY, z)
                d.rectangle((x - 1, y - 1, x + 1.2, y + 1.2), fill=col)


def draw_preview_mockup():
    """model-preview-3d.png: dashboard model preview + 3D print progress."""
    WHITE = (238, 238, 238)
    GRAY = (170, 170, 170)
    CARD = (42, 42, 46)
    img = Image.new("RGB", (1120, 1560), (0, 0, 0))
    d = ImageDraw.Draw(img)

    d.rounded_rectangle((40, 40, 1080, 1520), 26, fill=(28, 28, 30))
    d.rounded_rectangle((40, 40, 1080, 150), 26, fill=(35, 35, 38))
    d.rectangle((40, 110, 1080, 150), fill=(28, 28, 30))
    for cx, col in ((80, (255, 95, 87)), (115, (254, 188, 46)), (150, (40, 200, 64))):
        d.ellipse((cx - 11, 84, cx + 11, 106), fill=col)
    d.rounded_rectangle((185, 62, 935, 128), 18, fill=(22, 22, 24))
    d.rounded_rectangle((205, 74, 247, 116), 9, fill=ORANGE)
    _center(d, (205, 74, 247, 116), "T", _seg(26, True), (255, 255, 255))
    d.text((265, 78), "192.168.1.42", font=_seg(27), fill=(200, 200, 205))

    d.rounded_rectangle((75, 185, 1045, 1490), 16, fill=(35, 35, 38), outline=ORANGE, width=3)
    d.text((110, 210), "TinyMaker", font=_seg(40, True), fill=ORANGE)

    slices = _pawn_slices()

    # card 1: model preview
    d.rounded_rectangle((110, 285, 1008, 900), 14, fill=CARD)
    d.text((140, 312), "Pawn - Preview 3D", font=_seg(26, True), fill=WHITE)
    d.rounded_rectangle((140, 360, 978, 860), 10, fill=(21, 21, 23), outline=(58, 58, 63))
    _draw_iso(d, 140, 380, slices, 80, 60, 40.0, 1.0)
    d.text((156, 828), "Pawn - 40.0 mm - 800 layers", font=_seg(16), fill=GRAY)

    # card 2: print progress
    d.rounded_rectangle((110, 930, 1008, 1460), 14, fill=CARD)
    d.text((140, 955), "Print progress 3D", font=_seg(26, True), fill=WHITE)
    d.rounded_rectangle((140, 1000, 978, 1420), 10, fill=(21, 21, 23), outline=(58, 58, 63))
    _draw_iso(d, 140, 968, slices, 80, 60, 40.0, 0.45)
    d.text((156, 1388), "Pawn - 40.0 mm - 800 layers - 45% printed", font=_seg(16), fill=GRAY)

    img.save(MOCKUPS / "model-preview-3d.png")
    print("  model-preview-3d.png ok (regenerated)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--installed", help="version shown as 'Installed:' on the "
                    "update tile (default: newest git tag below the current)")
    args = ap.parse_args()

    latest = read_latest_version()
    installed = args.installed or read_installed_version(latest)
    print(f"mockups -> Installed: v{installed}, Latest/FW: v{latest}")

    mono34 = ImageFont.truetype(CONSOLA, 34)
    mono28 = ImageFont.truetype(CONSOLA, 28)
    mono26 = ImageFont.truetype(CONSOLA, 26)
    seg21 = ImageFont.truetype(SEGOE, 21)

    # --- printer-screens.png (2080x1920, 4x3 LCD tile collage) ---
    p = MOCKUPS / "printer-screens.png"
    img = Image.open(p).convert("RGB")
    d = ImageDraw.Draw(img)
    # update tile, two mono lines
    patch(img, d, (1466, 1100, 1886, 1144),
          [(0, f"Installed: v{installed}", mono34, (224, 224, 224))], (1900, 1100))
    patch(img, d, (1466, 1148, 1886, 1192),
          [(0, f"Latest: v{latest}", mono34, BLUE)], (1900, 1148))
    # About tile: "FW:" orange + value white
    patch(img, d, (786, 1560, 1106, 1600),
          [(0, "FW:", mono28, ORANGE), (62, f"v{latest}", mono28, (238, 238, 238))],
          (1100, 1565))
    # WiFi-info tile, small gray line
    patch(img, d, (1464, 1705, 1724, 1741),
          [(0, f"FW v{latest}", mono26, (170, 170, 170))], (1800, 1705))
    img.save(p)
    print(f"  {p.name} ok")

    # --- web-dashboard.png (1120x1440) ---
    p = MOCKUPS / "web-dashboard.png"
    img = Image.open(p).convert("RGB")
    d = ImageDraw.Draw(img)
    patch(img, d, (110, 276, 330, 304),
          [(0, f"Firmware {latest}", seg21, (170, 170, 170))], (400, 285))
    img.save(p)
    print(f"  {p.name} ok")

    # --- firmware-update-page.png: full redraw of the Update tab ---
    p = MOCKUPS / "firmware-update-page.png"
    draw_update_page(installed, latest).save(p)
    print(f"  {p.name} ok (regenerated)")

    # --- model-preview-3d.png: full redraw of the 3D preview/progress ---
    draw_preview_mockup()


if __name__ == "__main__":
    main()
