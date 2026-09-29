"""Makes scan.bmp (the camera frame otoca-camhook shows the game) from a printed card image.

usage: python make_scan.py <card.png> [out.bmp]    (out defaults to scan.bmp next to this script)
       python make_scan.py --clear [out.bmp]       (remove it: nothing in front of the camera)

Finds the QR code by its three finder patterns, cuts it out with a quiet zone, and puts it black on white,
scaled up, in the middle of a 640x480 frame (arkkep binarizes the centre 360x360 of the frame).
"""
import os
import sys

import numpy as np
from PIL import Image

W, H, FIT = 640, 480, 340


def runs(line):
    """(start, length, dark) runs of a boolean row."""
    edges = np.flatnonzero(np.diff(line.astype(np.int8))) + 1
    starts = np.concatenate(([0], edges))
    lengths = np.diff(np.concatenate((starts, [len(line)])))
    return [(s, n, bool(line[s])) for s, n in zip(starts, lengths)]


def pattern(line):
    """(centre, module) of each 1:1:3:1:1 dark/light/dark/light/dark run group in a boolean line."""
    r = runs(line)
    out = []
    for i in range(len(r) - 4):
        if not r[i][2]:
            continue
        n = [r[i + j][1] for j in range(5)]
        m = sum(n) / 7
        if m >= 1 and all(abs(a - b * m) <= m * 0.6 for a, b in zip(n, (1, 1, 3, 1, 1))):
            out.append((r[i + 2][0] + n[2] / 2, m))
    return out


def finder_hits(dark):
    """Finder pattern centres: the row pattern must repeat in the column through its centre."""
    hits = []
    for y in range(dark.shape[0]):
        for x, m in pattern(dark[y]):
            for cy, cm in pattern(dark[:, int(x)]):
                if abs(cy - y) < m and abs(cm - m) < m * 0.4:
                    hits.append((x, cy, (m + cm) / 2))
                    break
    return hits


def find_qr(dark):
    """(x0, y0, x1, y1, module) of the QR code: three finder patterns of one size forming an L."""
    groups = []  # [sum_x, sum_y, sum_m, count]
    for x, y, m in finder_hits(dark):
        for g in groups:
            if abs(g[0] / g[3] - x) < 2 * m and abs(g[1] / g[3] - y) < 2 * m:
                g[0] += x
                g[1] += y
                g[2] += m
                g[3] += 1
                break
        else:
            groups.append([x, y, m, 1])
    c = [(g[0] / g[3], g[1] / g[3], g[2] / g[3], g[3]) for g in groups if g[3] >= 2]
    best = None
    for a in c:  # a = top-left corner of the L
        for b in c:
            for d in c:
                if len({a, b, d}) < 3 or not (b[0] > a[0] and d[1] > a[1]):
                    continue
                m = (a[2] + b[2] + d[2]) / 3
                ab, ad = b[0] - a[0], d[1] - a[1]
                size = ab / m + 7  # modules across
                if (max(x[2] for x in (a, b, d)) > 1.3 * min(x[2] for x in (a, b, d)) or abs(ab - ad) > 2 * m
                        or abs(b[1] - a[1]) > 2 * m or abs(d[0] - a[0]) > 2 * m or not 21 - 2 <= size <= 57 + 2):
                    continue
                score = a[3] + b[3] + d[3]
                if not best or score > best[0]:
                    best = (score, a, b, d, m)
    if not best:
        raise SystemExit("QR code not found in the image")
    _, a, b, d, m = best
    # finder centres sit 3.5 modules in from the code's edges
    return a[0] - 3.5 * m, a[1] - 3.5 * m, b[0] + 3.5 * m, d[1] + 3.5 * m, m


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    args = sys.argv[1:]
    if args and args[0] == "--clear":
        out = args[1] if len(args) > 1 else os.path.join(here, "scan.bmp")
        if os.path.exists(out):
            os.remove(out)
        print(f"removed {out}")
        return
    if not args:
        raise SystemExit(__doc__)
    out = args[1] if len(args) > 1 else os.path.join(here, "scan.bmp")

    gray = np.asarray(Image.open(args[0]).convert("L"), dtype=np.float32)
    dark = gray < 110
    x0, y0, x1, y1, m = find_qr(dark)
    q = 4 * m  # quiet zone
    box = [int(round(v)) for v in (x0 - q, y0 - q, x1 + q, y1 + q)]
    qr = Image.fromarray(np.where(dark, 0, 255).astype(np.uint8)).crop(box)
    # outside the code itself everything is quiet zone: paint it white
    inner = [int(round(q)), int(round(q)), int(round(q + x1 - x0)), int(round(q + y1 - y0))]
    white = Image.new("L", qr.size, 255)
    white.paste(qr.crop(inner), inner[:2])
    scale = FIT / max(white.size)
    big = white.resize((round(white.size[0] * scale), round(white.size[1] * scale)), Image.NEAREST)
    frame = Image.new("L", (W, H), 255)
    frame.paste(big, ((W - big.size[0]) // 2, (H - big.size[1]) // 2))
    tmp = out + ".tmp"
    frame.convert("RGB").save(tmp, "BMP")
    os.replace(tmp, out)  # the hook reloads on change; never let it see a half-written file
    print(f"{out}: QR {x1 - x0:.0f}x{y1 - y0:.0f}px ({(x1 - x0) / m:.0f} modules) from {os.path.basename(args[0])}")


if __name__ == "__main__":
    main()
