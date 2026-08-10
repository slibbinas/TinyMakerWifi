"""Decode every 3rd layer PNG (pure python), downsample 2x2, build voxel volume,
blur a copy, crop to bbox, emit data.js with base64 volumes for the raymarcher."""
import base64
import os
import struct
import sys
import zlib

MODEL = os.path.join(os.path.dirname(__file__), "model")
STEP = 3          # every 3rd layer
W, H = 320, 240   # source PNG dims


def decode_png_gray(fn):
    with open(fn, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", fn
    pos = 8
    idat = b""
    w = h = None
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", chunk[:10])
            assert (bd, ct) == (8, 0), (fn, bd, ct)
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w + 1
    out = bytearray(w * h)
    prev = bytearray(w)
    for y in range(h):
        row = bytearray(raw[y * stride + 1:(y + 1) * stride])
        ft = raw[y * stride]
        if ft == 1:      # sub
            for x in range(1, w):
                row[x] = (row[x] + row[x - 1]) & 0xFF
        elif ft == 2:    # up
            for x in range(w):
                row[x] = (row[x] + prev[x]) & 0xFF
        elif ft == 3:    # average
            row[0] = (row[0] + prev[0] // 2) & 0xFF
            for x in range(1, w):
                row[x] = (row[x] + (row[x - 1] + prev[x]) // 2) & 0xFF
        elif ft == 4:    # paeth
            for x in range(w):
                a = row[x - 1] if x else 0
                b = prev[x]
                c = prev[x - 1] if x else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[x] = (row[x] + pr) & 0xFF
        out[y * w:(y + 1) * w] = row
        prev = row
    return out


def main():
    layers = sorted(int(f[:-4]) for f in os.listdir(MODEL)
                    if f.endswith(".png") and f[:-4].isdigit())
    picked = layers[::STEP]
    nx, ny = W // 2, H // 2          # downsampled slice dims
    nz = len(picked)
    print("layers", len(layers), "picked", nz, "slice", nx, "x", ny)

    vol = bytearray(nx * ny * nz)    # [z][y][x]
    for zi, li in enumerate(picked):
        img = decode_png_gray(os.path.join(MODEL, "%d.png" % li))
        base = zi * nx * ny
        for y in range(ny):
            r0 = 2 * y * W
            r1 = r0 + W
            rowbase = base + y * nx
            for x in range(nx):
                s = img[r0 + 2 * x] + img[r0 + 2 * x + 1] + \
                    img[r1 + 2 * x] + img[r1 + 2 * x + 1]
                vol[rowbase + x] = s >> 2
        if zi % 40 == 0:
            print("slice", zi, "/", nz)

    # bbox of occupied voxels (>=64)
    x0, x1, y0, y1, z0, z1 = nx, 0, ny, 0, nz, 0
    for z in range(nz):
        for y in range(ny):
            row = vol[z * nx * ny + y * nx:z * nx * ny + y * nx + nx]
            for x, v in enumerate(row):
                if v >= 64:
                    if x < x0: x0 = x
                    if x > x1: x1 = x
                    if y < y0: y0 = y
                    if y > y1: y1 = y
                    if z < z0: z0 = z
                    if z > z1: z1 = z
    m = 2  # margin
    x0 = max(0, x0 - m); y0 = max(0, y0 - m); z0 = max(0, z0 - m)
    x1 = min(nx - 1, x1 + m); y1 = min(ny - 1, y1 + m); z1 = min(nz - 1, z1 + m)
    cx, cy, cz = x1 - x0 + 1, y1 - y0 + 1, z1 - z0 + 1
    print("bbox", cx, cy, cz)

    crop = bytearray(cx * cy * cz)
    for z in range(cz):
        for y in range(cy):
            src = (z + z0) * nx * ny + (y + y0) * nx + x0
            dst = z * cx * cy + y * cx
            crop[dst:dst + cx] = vol[src:src + cx]

    # separable [1,2,1] blur, one pass per axis
    def blur_axis(buf, stride, count, line_len, get_index):
        out = bytearray(len(buf))
        for a in range(count):
            for b in range(line_len):
                i = get_index(a, b)
                prev = buf[i - stride] if b > 0 else buf[i]
                nxt = buf[i + stride] if b < line_len - 1 else buf[i]
                out[i] = (prev + 2 * buf[i] + nxt) >> 2
        return out

    sm = crop
    # x axis
    sm = blur_axis(sm, 1, cy * cz, cx,
                   lambda a, b: a * cx + b)
    # y axis
    sm = blur_axis(sm, cx, cx * cz, cy,
                   lambda a, b: (a // cx) * cx * cy + b * cx + (a % cx))
    # z axis
    sm = blur_axis(sm, cx * cy, cx * cy, cz,
                   lambda a, b: b * cx * cy + a)

    with open(os.path.join(os.path.dirname(__file__), "data.js"), "w") as f:
        f.write("const VOL={cx:%d,cy:%d,cz:%d,vx:0.255,vy:0.255,vz:%f};\n"
                % (cx, cy, cz, 0.05 * STEP))
        f.write('const RAW_B64="%s";\n' % base64.b64encode(bytes(crop)).decode())
        f.write('const SM_B64="%s";\n' % base64.b64encode(bytes(sm)).decode())
    print("data.js written")


main()
