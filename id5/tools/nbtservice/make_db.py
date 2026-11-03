#!/usr/bin/env python3
"""Builds NbtService_ALL_Texts.zip and NbtService_ALL_Images.zip.

Same format as BMW's PluginsCoded apps and as rhmiapp/make_db.py: one "<lang>.txt" per
language with "id=text" lines, and "<imageId>.png" per image. English text in every
language (this page is for the car's owner, not for passengers).

The row icon is drawn here rather than committed as a binary: a padlock, 63x54 like the
Android Auto row icon, white on transparent, written with zlib — no image library needed.
"""
import os
import struct
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol",
         "es-US": "Espanol (US)", "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano",
         "pt": "Portugues", "ru": "Russian"}

# 10 is the checkbox label. It names the window length, so it has to be changed together with
# OPEN_S in knock.conf (the plugin reads OPEN_S but cannot format a text: iDrive text comes from
# this DB by id, which is also why the status row has one entry per bucket).
TEXTS = {
    1: "NBT Service",
    10: "SSH (15 min)",
    20: "SSH: closed",
    21: "SSH: open, 15 min left",
    22: "SSH: open, 10 min left",
    23: "SSH: open, 5 min left",
    24: "SSH: open, under a minute",
    25: "SSH: gate not running",
    26: "SSH: starting the gate...",
}

ICON_ID = 55101
W, H = 63, 54


def png(width, height, pixels):
    """pixels: list of (r, g, b, a) rows, each a list of 4-tuples."""
    raw = b"".join(b"\x00" + b"".join(struct.pack("4B", *px) for px in row) for row in pixels)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def padlock():
    """A padlock: shackle (upper annulus) + rounded body + keyhole."""
    cx, body_top, body_bot = W // 2, 24, 48
    body_l, body_r, radius = 17, 45, 4
    ring_out, ring_in, ring_cy = 12, 7, body_top
    rows = []
    for y in range(H):
        row = []
        for x in range(W):
            on = False
            # body, with rounded corners
            if body_l <= x <= body_r and body_top <= y <= body_bot:
                dx = min(x - body_l, body_r - x)
                dy = min(y - body_top, body_bot - y)
                on = not (dx < radius and dy < radius and
                          (radius - dx) ** 2 + (radius - dy) ** 2 > radius * radius)
            # shackle: the top half of a ring sitting on the body
            if y < ring_cy:
                d2 = (x - cx) ** 2 + (y - ring_cy) ** 2
                if ring_in * ring_in <= d2 <= ring_out * ring_out:
                    on = True
            # keyhole
            if (x - cx) ** 2 + (y - (body_top + 10)) ** 2 <= 9:
                on = False
            if on and (x - cx) ** 2 / 4.0 + (y - (body_top + 17)) ** 2 / 16.0 <= 1:
                on = False
            row.append((255, 255, 255, 255) if on else (0, 0, 0, 0))
        rows.append(row)
    return png(W, H, rows)


with zipfile.ZipFile(os.path.join(HERE, "NbtService_ALL_Texts.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for lang, langname in LANGS.items():
        body = "100000=%s\n" % langname + "".join("%d=%s\n" % (k, v) for k, v in sorted(TEXTS.items()))
        z.writestr(lang + ".txt", body.encode("utf-8"))

with zipfile.ZipFile(os.path.join(HERE, "NbtService_ALL_Images.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("%d.png" % ICON_ID, padlock())

print("wrote NbtService_ALL_Texts.zip, NbtService_ALL_Images.zip")
