#!/usr/bin/env python3
"""Draws the game's icon (a little steam engine on a blue tile) and writes it as a Windows .ico with
PNG images at 16 to 256 pixels. Run again after changing the drawing:

    python scripts/make-icon.py src/client/transport-poly.ico
"""
import math
import struct
import sys
import zlib

SS = 4            # supersampling
SIZE = 256 * SS


def hexrgb(h, a=255):
    return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), a)


def blend(dst, src):
    a = src[3] / 255.0
    if a <= 0:
        return dst
    oa = dst[3] / 255.0
    out_a = a + oa * (1 - a)
    if out_a <= 0:
        return (0, 0, 0, 0)
    return tuple(int(round((src[i] * a + dst[i] * oa * (1 - a)) / out_a)) for i in range(3)) + (int(round(out_a * 255)),)


class Canvas:
    def __init__(self):
        self.px = [[(0, 0, 0, 0)] * SIZE for _ in range(SIZE)]

    def fill(self, inside, colour, box):
        x0, y0, x1, y1 = (max(0, int(v * SS)) for v in box)
        x1, y1 = min(SIZE, x1 + 1), min(SIZE, y1 + 1)
        for y in range(y0, y1):
            fy = (y + 0.5) / SS
            row = self.px[y]
            for x in range(x0, x1):
                fx = (x + 0.5) / SS
                c = colour(fx, fy) if callable(colour) else colour
                if inside(fx, fy):
                    row[x] = blend(row[x], c)

    def rounded(self, x0, y0, x1, y1, r, colour):
        def inside(x, y):
            cx = min(max(x, x0 + r), x1 - r)
            cy = min(max(y, y0 + r), y1 - r)
            return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
        self.fill(inside, colour, (x0, y0, x1, y1))

    def rect(self, x0, y0, x1, y1, colour):
        self.fill(lambda x, y: x0 <= x <= x1 and y0 <= y <= y1, colour, (x0, y0, x1, y1))

    def circle(self, cx, cy, r, colour):
        self.fill(lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r, colour, (cx - r, cy - r, cx + r, cy + r))

    def scaled(self, size):
        step = SIZE // size
        out = []
        for y in range(size):
            row = []
            for x in range(size):
                acc = [0.0, 0.0, 0.0, 0.0]
                for yy in range(y * step, (y + 1) * step):
                    for xx in range(x * step, (x + 1) * step):
                        p = self.px[yy][xx]
                        a = p[3]
                        acc[0] += p[0] * a
                        acc[1] += p[1] * a
                        acc[2] += p[2] * a
                        acc[3] += a
                n = step * step
                if acc[3] > 0:
                    row.append((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(acc[3] / n)))
                else:
                    row.append((0, 0, 0, 0))
            out.append(row)
        return out


def png(pixels):
    size = len(pixels)
    raw = b''.join(b'\x00' + bytes(c for p in row for c in p) for row in pixels)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def draw():
    c = Canvas()
    top, bottom = hexrgb('#2f6aa3'), hexrgb('#163556')

    def sky(x, y):
        t = min(max((y - 8) / 240.0, 0.0), 1.0)
        return tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
    c.rounded(8, 8, 248, 248, 52, sky)
    # Steam drifting back from the chimney.
    for cx, cy, r in [(170, 62, 17), (196, 44, 13), (148, 46, 11), (216, 30, 9)]:
        c.circle(cx, cy, r, (245, 247, 250, 225))
    # Rails and sleepers.
    for x in range(26, 232, 22):
        c.rect(x, 204, x + 12, 212, hexrgb('#6b4a32'))
    c.rect(18, 200, 238, 205, hexrgb('#d9dde2'))
    dark = hexrgb('#26282e')
    # Cab, boiler, smokebox and chimney.
    c.rect(38, 84, 100, 170, hexrgb('#e8892a'))
    c.rect(32, 76, 106, 88, dark)
    c.rect(50, 98, 88, 128, hexrgb('#163556'))
    c.rounded(92, 112, 190, 162, 20, hexrgb('#ffba42'))
    for x in (118, 146):
        c.rect(x, 112, x + 6, 162, hexrgb('#c98a1c'))
    c.rect(174, 112, 196, 162, dark)
    c.rect(160, 72, 180, 114, dark)
    c.rect(154, 66, 186, 76, dark)
    c.rect(118, 98, 136, 114, hexrgb('#d9a33a'))
    # Frame, buffer beam and wheels.
    c.rect(36, 160, 204, 172, dark)
    c.rect(196, 150, 210, 172, hexrgb('#d33a2c'))
    for cx, cy, r in [(70, 182, 19), (120, 182, 19), (172, 187, 14)]:
        c.circle(cx, cy, r, dark)
        c.circle(cx, cy, r * 0.62, hexrgb('#c4372a'))
        c.circle(cx, cy, r * 0.25, hexrgb('#d9dde2'))
    c.rect(70, 179, 172, 185, hexrgb('#b9bec6'))
    return c


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'transport-poly.ico'
    canvas = draw()
    images = [(s, png(canvas.scaled(s))) for s in (256, 64, 48, 32, 16)]
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b'', b''
    for size, image in images:
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(image), offset + len(data))
        data += image
    with open(out, 'wb') as f:
        f.write(header + entries + data)
    print('wrote', out)


if __name__ == '__main__':
    main()
