#!/usr/bin/env python3
"""Pull a real model's sampled slices off the printer into a JSON the voxel
preview lab can load - the same sampling the dashboard's fetchSlices does:

  N = min(36, layers); layer index li = 1 + round(k*(layers-1)/(N-1));
  GET /api/files/layer?name=<m>&i=<li>; downscale to 80x60; a cell is "on"
  when the red channel > 96 (the cured-resin mask).

Output: scripts/dev/model-slices/<name>.json with base64 slices (one byte per
cell, same shape as the dashboard's localStorage cache), so the lab and the
0-27 comparison can render REAL geometry, not a synthetic blob.

Usage (PlatformIO penv python; needs Pillow + requests):
  python scripts/dev/fetch_model_slices.py ScreamingEvil [tinymaker.local]
The printer must be IDLE (layer reads answer 409 while printing).
"""
import base64
import io
import json
import os
import sys

import requests
from PIL import Image

GW, GH, NMAX, THRESH = 80, 60, 36, 96


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: fetch_model_slices.py <model> [host]")
    name = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else "tinymaker.local"
    base = f"http://{host}"

    det = requests.get(f"{base}/api/files/model", params={"name": name}, timeout=15).json()
    layers = int(det.get("printLayers") or det.get("layers") or 0)
    model_h = float(det.get("heightMm") or 0)
    if layers <= 0:
        sys.exit(f"model {name!r}: no layers reported ({det})")
    n = min(NMAX, layers)
    print(f"{name}: {layers} layers, {model_h} mm -> sampling {n} slices")

    slices_b64 = []
    for k in range(n):
        li = 1 + round(k * (layers - 1) / (n - 1)) if n > 1 else 1
        r = requests.get(f"{base}/api/files/layer", params={"name": name, "i": li}, timeout=25)
        r.raise_for_status()
        img = Image.open(io.BytesIO(r.content)).convert("RGB").resize((GW, GH), Image.BILINEAR)
        px = img.load()
        cell = bytearray(GW * GH)
        for j in range(GH):
            for i in range(GW):
                cell[j * GW + i] = 1 if px[i, j][0] > THRESH else 0
        slices_b64.append(base64.b64encode(bytes(cell)).decode("ascii"))
        print(f"  slice {k + 1}/{n} (layer {li})", end="\r")
    print()

    out_dir = os.path.join(os.path.dirname(__file__), "model-slices")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"{name}.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"name": name, "gw": GW, "gh": GH, "N": n,
                   "layers": layers, "modelH": model_h, "slices": slices_b64}, f)
    print(f"wrote {out} ({os.path.getsize(out) // 1024} KB)")


if __name__ == "__main__":
    main()
