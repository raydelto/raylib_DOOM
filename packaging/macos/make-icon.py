#!/usr/bin/env python3
"""Draws the raylibDOOM app icon as a 1024x1024 PNG.

    make-icon.py OUT.png

The same picture as packaging/linux/raylibdoom.svg (a blocky "D" on a
dark red tile), laid out on Apple's icon grid: an 824x824 rounded
square centered on the 1024x1024 canvas. Plain Python, so CI needs
nothing beyond the runner's python3; make-dmg.sh turns it into an
.icns with sips and iconutil.
"""

import struct
import sys
import zlib

N = 1024
TILE0, TILE1 = 100, 924          # the rounded square
RADIUS = 185
BORDER = 26
TOP = (0x7a, 0x0e, 0x0e)         # gradient, top to bottom
BOTTOM = (0x2a, 0x04, 0x04)
EDGE = (0xe8, 0xa3, 0x3a)
GLYPH = (0xf2, 0xc1, 0x4e)

# The SVG's 256-unit coordinates, mapped onto the tile.
K = (TILE1 - TILE0) / 240.0


def svg(v):
    return TILE0 + (v - 8) * K


def rect(x, y, w, h):
    return (svg(x), svg(y), svg(x + w), svg(y + h))


# The "D": outer steps minus the counter, as in the SVG path.
D_OUTER = [rect(64, 56, 96, 144), rect(160, 72, 16, 112),
           rect(176, 88, 16, 80)]
D_INNER = [rect(96, 88, 56, 80), rect(152, 104, 16, 48)]
UNDERLINE = [rect(64, 208, 128, 12)]


def cover(boxes, x, y):
    """Area of pixel (x, y) covered by the union of disjoint boxes."""
    a = 0.0
    for x0, y0, x1, y1 in boxes:
        w = min(x + 1, x1) - max(x, x0)
        h = min(y + 1, y1) - max(y, y0)
        if w > 0 and h > 0:
            a += w * h
    return a


def rounded(x, y, x0, y0, x1, y1, r):
    """Coverage of pixel (x, y) by a rounded rectangle, 4x4 sampled."""
    if x + 1 <= x0 or x >= x1 or y + 1 <= y0 or y >= y1:
        return 0.0
    cx = min(max(x + 0.5, x0 + r), x1 - r)
    cy = min(max(y + 0.5, y0 + r), y1 - r)
    if abs(x + 0.5 - cx) < 1 and abs(y + 0.5 - cy) < 1 and \
       x >= x0 and x + 1 <= x1 and y >= y0 and y + 1 <= y1:
        return 1.0
    n = 0
    for i in range(4):
        for j in range(4):
            sx = x + (i + 0.5) / 4
            sy = y + (j + 0.5) / 4
            if not (x0 <= sx < x1 and y0 <= sy < y1):
                continue
            px = min(max(sx, x0 + r), x1 - r)
            py = min(max(sy, y0 + r), y1 - r)
            if (sx - px) ** 2 + (sy - py) ** 2 <= r * r:
                n += 1
    return n / 16.0


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def main():
    rows = []
    t0, t1 = TILE0, TILE1
    b0, b1 = TILE0 + BORDER, TILE1 - BORDER
    for y in range(N):
        row = bytearray([0])
        grad = mix(TOP, BOTTOM, min(max((y + 0.5 - b0) / (b1 - b0), 0), 1))
        for x in range(N):
            outer = rounded(x, y, t0, t0, t1, t1, RADIUS)
            if outer == 0:
                row += b"\0\0\0\0"
                continue
            inner = rounded(x, y, b0, b0, b1, b1, RADIUS - BORDER)
            c = mix(EDGE, grad, inner)
            g = cover(D_OUTER, x, y) - cover(D_INNER, x, y) \
                + cover(UNDERLINE, x, y)
            c = mix(c, GLYPH if g and cover(UNDERLINE, x, y) == 0 else EDGE,
                    min(max(g, 0), 1))
            row += bytes(int(round(v)) for v in c) + \
                bytes([int(round(outer * 255))])
        rows.append(bytes(row))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + \
            struct.pack(">I", zlib.crc32(body) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
    png += chunk(b"IEND", b"")
    with open(sys.argv[1], "wb") as f:
        f.write(png)


if __name__ == "__main__":
    main()
