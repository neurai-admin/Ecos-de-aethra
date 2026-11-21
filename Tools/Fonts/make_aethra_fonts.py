"""Original fonts of ECOS DE AETHRA, drawn here glyph by glyph (no third-party font copied).

  Aethra Pixel       - text and HUD: 5 x 7 pixel capitals, square pixels.
  Aethra Pixel Bold  - the same glyphs one pixel heavier.
  Aethra Runa        - titles: the same skeleton with octagonal "carved stone" pixels.

Writes TrueType files (pure standard library: no fontTools) and a PNG specimen.
  python Tools/Fonts/make_aethra_fonts.py [output folder]   (default: SourceArt/Fonts)

Grid (one pixel = 100 font units, 1000 units per em): rows 0-1 accents, rows 2-8 the 7-row body
(capitals and digits fill it; lowercase x-height is its lower 5 rows), rows 9-10 descenders.
The baseline is the bottom of row 8. Crisp on screen at sizes where the em is a multiple of 10 px.
"""
import os
import struct
import sys
import time
import zlib

P = 100            # font units per pixel
UPM = 1000
ASC, DESC = 9 * P, -2 * P

# ---------------------------------------------------------------- glyphs (7-row body, optional descender)

G = {}
def g(ch, rows, desc=None):
    G[ch] = (rows, desc or [])

def block(text):
    return text.strip("\n").split("\n")

g("A", block("""
.###.
#...#
#...#
#####
#...#
#...#
#...#"""))
g("B", block("""
####.
#...#
####.
#...#
#...#
#...#
####."""))
g("C", block("""
.####
#....
#....
#....
#....
#....
.####"""))
g("D", block("""
####.
#...#
#...#
#...#
#...#
#...#
####."""))
g("E", block("""
#####
#....
####.
#....
#....
#....
#####"""))
g("F", block("""
#####
#....
####.
#....
#....
#....
#...."""))
g("G", block("""
.####
#....
#....
#.###
#...#
#...#
.###."""))
g("H", block("""
#...#
#...#
#####
#...#
#...#
#...#
#...#"""))
g("I", block("""
###
.#.
.#.
.#.
.#.
.#.
###"""))
g("J", block("""
..###
....#
....#
....#
#...#
#...#
.###."""))
g("K", block("""
#...#
#..#.
###..
#..#.
#...#
#...#
#...#"""))
g("L", block("""
#....
#....
#....
#....
#....
#....
#####"""))
g("M", block("""
#...#
##.##
#.#.#
#...#
#...#
#...#
#...#"""))
g("N", block("""
#...#
##..#
#.#.#
#..##
#...#
#...#
#...#"""))
g("O", block("""
.###.
#...#
#...#
#...#
#...#
#...#
.###."""))
g("P", block("""
####.
#...#
#...#
####.
#....
#....
#...."""))
g("Q", block("""
.###.
#...#
#...#
#...#
#.#.#
#..#.
.##.#"""))
g("R", block("""
####.
#...#
#...#
####.
#..#.
#...#
#...#"""))
g("S", block("""
.####
#....
#....
.###.
....#
....#
####."""))
g("T", block("""
#####
..#..
..#..
..#..
..#..
..#..
..#.."""))
g("U", block("""
#...#
#...#
#...#
#...#
#...#
#...#
.###."""))
g("V", block("""
#...#
#...#
#...#
#...#
.#.#.
.#.#.
..#.."""))
g("W", block("""
#...#
#...#
#...#
#.#.#
#.#.#
##.##
#...#"""))
g("X", block("""
#...#
#...#
.#.#.
..#..
.#.#.
#...#
#...#"""))
g("Y", block("""
#...#
#...#
.#.#.
..#..
..#..
..#..
..#.."""))
g("Z", block("""
#####
....#
...#.
..#..
.#...
#....
#####"""))
g("0", block("""
.###.
#...#
#..##
#.#.#
##..#
#...#
.###."""))
g("1", block("""
.#.
##.
.#.
.#.
.#.
.#.
###"""))
g("2", block("""
.###.
#...#
....#
..##.
.#...
#....
#####"""))
g("3", block("""
####.
....#
....#
.###.
....#
....#
####."""))
g("4", block("""
#..#.
#..#.
#..#.
#####
...#.
...#.
...#."""))
g("5", block("""
#####
#....
####.
....#
....#
#...#
.###."""))
g("6", block("""
.###.
#....
#....
####.
#...#
#...#
.###."""))
g("7", block("""
#####
....#
...#.
..#..
..#..
..#..
..#.."""))
g("8", block("""
.###.
#...#
#...#
.###.
#...#
#...#
.###."""))
g("9", block("""
.###.
#...#
#...#
.####
....#
....#
.###."""))
g("a", block("""
.....
.....
.###.
....#
.####
#...#
.####"""))
g("b", block("""
#....
#....
####.
#...#
#...#
#...#
####."""))
g("c", block("""
....
....
.###
#...
#...
#...
.###"""))
g("d", block("""
....#
....#
.####
#...#
#...#
#...#
.####"""))
g("e", block("""
.....
.....
.###.
#...#
#####
#....
.###."""))
g("f", block("""
..##
.#..
####
.#..
.#..
.#..
.#.."""))
g("g", block("""
.....
.....
.####
#...#
#...#
#...#
.####"""), ["....#", ".###."])
g("h", block("""
#....
#....
####.
#...#
#...#
#...#
#...#"""))
g("i", ["#", ".", "#", "#", "#", "#", "#"])
g("j", block("""
..#
...
..#
..#
..#
..#
..#"""), ["..#", "##."])
g("k", block("""
#...
#...
#..#
#.#.
##..
#.#.
#..#"""))
g("l", ["#.", "#.", "#.", "#.", "#.", "#.", ".#"])
g("m", block("""
.....
.....
##.#.
#.#.#
#.#.#
#.#.#
#.#.#"""))
g("n", block("""
....
....
###.
#..#
#..#
#..#
#..#"""))
g("o", block("""
....
....
.##.
#..#
#..#
#..#
.##."""))
g("p", block("""
.....
.....
####.
#...#
#...#
#...#
####."""), ["#....", "#...."])
g("q", block("""
.....
.....
.####
#...#
#...#
#...#
.####"""), ["....#", "....#"])
g("r", block("""
....
....
#.##
##..
#...
#...
#..."""))
g("s", block("""
....
....
.###
#...
.##.
...#
###."""))
g("t", block("""
.#..
.#..
####
.#..
.#..
.#..
..##"""))
g("u", block("""
....
....
#..#
#..#
#..#
#..#
.###"""))
g("v", block("""
.....
.....
#...#
#...#
#...#
.#.#.
..#.."""))
g("w", block("""
.....
.....
#...#
#.#.#
#.#.#
#.#.#
.#.#."""))
g("x", block("""
.....
.....
#...#
.#.#.
..#..
.#.#.
#...#"""))
g("y", block("""
....
....
#..#
#..#
#..#
#..#
.###"""), ["...#", "###."])
g("z", block("""
....
....
####
...#
..#.
.#..
####"""))
# Punctuation and symbols.
g(".", [".", ".", ".", ".", ".", ".", "#"])
g(",", [".", ".", ".", ".", ".", ".", "#"], ["#"])
g(":", [".", ".", "#", ".", ".", ".", "#"])
g(";", [".", ".", "#", ".", ".", ".", "#"], ["#"])
g("!", ["#", "#", "#", "#", "#", ".", "#"])
g("?", block("""
.###.
#...#
....#
..##.
..#..
.....
..#.."""))
g("'", ["#", "#", ".", ".", ".", ".", "."])
g('"', ["#.#", "#.#", "...", "...", "...", "...", "..."])
g("-", ["....", "....", "....", "####", "....", "....", "...."])
g("+", [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."])
g("=", ["....", "....", "####", "....", "####", "....", "...."])
g("_", [".....", ".....", ".....", ".....", ".....", ".....", "....."], ["#####"])
g("/", ["....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."])
g("\\", ["#....", ".#...", ".#...", "..#..", "...#.", "...#.", "....#"])
g("(", [".#", "#.", "#.", "#.", "#.", "#.", ".#"])
g(")", ["#.", ".#", ".#", ".#", ".#", ".#", "#."])
g("[", ["##", "#.", "#.", "#.", "#.", "#.", "##"])
g("]", ["##", ".#", ".#", ".#", ".#", ".#", "##"])
g("{", ["..#", ".#.", ".#.", "#..", ".#.", ".#.", "..#"])
g("}", ["#..", ".#.", ".#.", "..#", ".#.", ".#.", "#.."])
g("<", ["...#", "..#.", ".#..", "#...", ".#..", "..#.", "...#"])
g(">", ["#...", ".#..", "..#.", "...#", "..#.", ".#..", "#..."])
g("*", [".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."])
g("#", [".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."])
g("%", ["##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##"])
g("&", [".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"])
g("@", [".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".####"])
g("$", ["..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."])
g("^", ["..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....."])
g("~", [".....", ".....", ".##.#", "#.##.", ".....", ".....", "....."])
g("`", ["#.", ".#", "..", "..", "..", "..", ".."])
g("|", ["#"] * 7, ["#"])
g("°", [".##", "#.#", ".##", "...", "...", "...", "..."])
g("º", [".#.", "#.#", ".#.", "...", "###", "...", "..."])
g("ª", [".##", "#.#", ".##", "...", "###", "...", "..."])
g("«", [".....", ".....", "..#.#", ".#.#.", "#.#..", ".#.#.", "..#.#"])
g("»", [".....", ".....", "#.#..", ".#.#.", "..#.#", ".#.#.", "#.#.."])

# Accents (2 rows) composed above letters; cedilla below.
ACC = {"acute": ["...#.", "..#.."], "grave": [".#...", "..#.."], "circ": ["..#..", ".#.#."],
       "tilde": [".##.#", "#.##."], "diaer": [".....", ".#.#."]}
ACC_I = {"acute": ["..#", ".#."], "grave": ["#..", ".#."], "circ": [".#.", "#.#"], "diaer": ["...", "#.#"]}
COMPOSE = {
    "Á": ("A", "acute"), "À": ("A", "grave"), "Â": ("A", "circ"), "Ã": ("A", "tilde"), "Ä": ("A", "diaer"),
    "É": ("E", "acute"), "È": ("E", "grave"), "Ê": ("E", "circ"), "Ë": ("E", "diaer"),
    "Í": ("I", "acute"), "Ì": ("I", "grave"), "Î": ("I", "circ"), "Ï": ("I", "diaer"),
    "Ó": ("O", "acute"), "Ò": ("O", "grave"), "Ô": ("O", "circ"), "Õ": ("O", "tilde"), "Ö": ("O", "diaer"),
    "Ú": ("U", "acute"), "Ù": ("U", "grave"), "Û": ("U", "circ"), "Ü": ("U", "diaer"), "Ñ": ("N", "tilde"),
    "á": ("a", "acute"), "à": ("a", "grave"), "â": ("a", "circ"), "ã": ("a", "tilde"), "ä": ("a", "diaer"),
    "é": ("e", "acute"), "è": ("e", "grave"), "ê": ("e", "circ"), "ë": ("e", "diaer"),
    "ó": ("o", "acute"), "ò": ("o", "grave"), "ô": ("o", "circ"), "õ": ("o", "tilde"), "ö": ("o", "diaer"),
    "ú": ("u", "acute"), "ù": ("u", "grave"), "û": ("u", "circ"), "ü": ("u", "diaer"), "ñ": ("n", "tilde"),
}

def full_grid(ch):
    """11 rows x width of '#'/'.' for a character, or None for space."""
    if ch in G:
        rows, desc = G[ch]
        w = max(len(r) for r in rows + desc)
        pad = lambda r: r.ljust(w, ".")
        return ["." * w] * 2 + [pad(r) for r in rows] + [pad(r) for r in desc] + ["." * w] * (2 - len(desc))
    if ch in "íìîï":
        accent = ACC_I[{"í": "acute", "ì": "grave", "î": "circ", "ï": "diaer"}[ch]]
        return ["...", "...", accent[0], accent[1], ".#.", ".#.", ".#.", ".#.", ".#.", "...", "..."]
    if ch in COMPOSE:
        base, accent = COMPOSE[ch]
        grid = full_grid(base)
        w = len(grid[0])
        marks = [r[max(0, (5 - w) // 2):][:w] if w < 5 else r.center(w, ".") for r in ACC[accent]]
        if base.isupper():
            grid[0], grid[1] = marks
        else:
            grid[2], grid[3] = marks
        return grid
    if ch in "Çç":
        grid = full_grid("C" if ch == "Ç" else "c")
        w = len(grid[0])
        grid[9], grid[10] = ("..#..", ".##..") if w == 5 else ("..#.", ".##.")
        return grid
    return None

CHARS = [chr(c) for c in range(33, 127)] + list("ÁÀÂÃÄÉÈÊËÍÌÎÏÓÒÔÕÖÚÙÛÜÇÑáàâãäéèêëíìîïóòôõöúùûüçñ°ºª«»")

# ---------------------------------------------------------------- outlines

def embolden(r):
    """One pixel heavier to the right, but never closing a one-pixel gap (m, w, M, W stay open)."""
    r = r + "."
    out = list(r)
    for i in range(len(r) - 1):
        if r[i] == "#" and r[i + 1] == "." and (i + 2 >= len(r) or r[i + 2] != "#"):
            out[i + 1] = "#"
    return "".join(out)

def contours_for(grid, style):
    """Contours (lists of (x, y) font units, clockwise) for a glyph grid."""
    out = []
    rows = len(grid)
    if style == "bold":
        grid = [embolden(r) for r in grid]
    for r, line in enumerate(grid):
        y1 = (8 - r + 1) * P - P  # row r spans y0..y1 with the baseline at the bottom of row 8
        y0 = (8 - r) * P
        y1 = y0 + P
        if style == "runa":
            c = P // 4
            for i, ch in enumerate(line):
                if ch == "#":
                    x0, x1 = i * P, (i + 1) * P
                    out.append([(x0, y0 + c), (x0, y1 - c), (x0 + c, y1), (x1 - c, y1), (x1, y1 - c), (x1, y0 + c), (x1 - c, y0), (x0 + c, y0)])
        else:
            i = 0
            while i < len(line):
                if line[i] == "#":
                    j = i
                    while j < len(line) and line[j] == "#":
                        j += 1
                    x0, x1 = i * P, j * P
                    out.append([(x0, y0), (x0, y1), (x1, y1), (x1, y0)])
                    i = j
                else:
                    i += 1
    return out, len(grid[0])

# ---------------------------------------------------------------- TrueType writer

def checksum(data):
    data += b"\0" * (-len(data) % 4)
    return sum(struct.unpack(">%dL" % (len(data) // 4), data)) & 0xFFFFFFFF

def glyf_entry(contours):
    if not contours:
        return b"", (0, 0, 0, 0), 0, 0
    pts = [p for c in contours for p in c]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    box = (min(xs), min(ys), max(xs), max(ys))
    data = struct.pack(">hhhhh", len(contours), *box)
    end = -1
    for c in contours:
        end += len(c)
        data += struct.pack(">H", end)
    data += struct.pack(">H", 0)                   # no instructions
    data += bytes([1] * len(pts))                  # every point on the curve, long coordinates
    px = py = 0
    xd, yd = b"", b""
    for x, y in pts:
        xd += struct.pack(">h", x - px)
        yd += struct.pack(">h", y - py)
        px, py = x, y
    return data + xd + yd, box, len(pts), len(contours)

def name_table(family, sub):
    full = family if sub == "Regular" else f"{family} {sub}"
    ps = (family + "-" + sub).replace(" ", "")
    strings = {0: "Copyright 2026 ECOS DE AETHRA. Original design.", 1: family, 2: sub, 3: f"ECOS DE AETHRA: {full}: 2026",
               4: full, 5: "Version 1.000", 6: ps, 9: "ECOS DE AETHRA", 13: "All rights reserved by ECOS DE AETHRA."}
    recs, store = b"", b""
    for nid, text in strings.items():
        raw = text.encode("utf-16-be")
        recs += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(raw), len(store))
        store += raw
    return struct.pack(">HHH", 0, len(strings), 6 + 12 * len(strings)) + recs + store

def cmap_table(codes):
    # Format 4, one segment per code point (plus the 0xFFFF end segment).
    segs = sorted(codes.items()) + [(0xFFFF, 0)]
    n = len(segs)
    search = 2 * (1 << (n.bit_length() - 1))
    sub = struct.pack(">HHHHHHH", 4, 0, 0, 2 * n, search, (search // 2).bit_length() - 1, 2 * n - search)
    sub += b"".join(struct.pack(">H", c) for c, _ in segs) + b"\0\0"
    sub += b"".join(struct.pack(">H", c) for c, _ in segs)
    sub += b"".join(struct.pack(">h", ((gid - c) + 0x8000) % 0x10000 - 0x8000 if c != 0xFFFF else 1) for c, gid in segs)
    sub += b"\0\0" * n
    sub = sub[:2] + struct.pack(">H", len(sub)) + sub[4:]
    return struct.pack(">HHHHL", 0, 1, 3, 1, 12) + sub

def build_font(family, sub, style):
    names = [".notdef", "space"] + CHARS
    glyphs = []
    # .notdef: an outlined box
    box = [[(0, 0), (0, 7 * P), (5 * P, 7 * P), (5 * P, 0)], [(P, P), (4 * P, P), (4 * P, 6 * P), (P, 6 * P)]]
    glyphs.append((box, 6 * P))
    glyphs.append(([], 4 * P))
    for ch in CHARS:
        contours, w = contours_for(full_grid(ch), style)
        glyphs.append((contours, (w + 1 + (1 if style == "bold" else 0)) * P))
    glyf, loca, hmtx = b"", [], b""
    maxp_pts = maxp_cont = 0
    xmin = ymin = 10 ** 6
    xmax = ymax = -10 ** 6
    for contours, adv in glyphs:
        data, bbox, npts, ncont = glyf_entry(contours)
        loca.append(len(glyf))
        glyf += data + b"\0" * (-len(data) % 4)
        hmtx += struct.pack(">Hh", adv, bbox[0])
        maxp_pts, maxp_cont = max(maxp_pts, npts), max(maxp_cont, ncont)
        if contours:
            xmin, ymin, xmax, ymax = min(xmin, bbox[0]), min(ymin, bbox[1]), max(xmax, bbox[2]), max(ymax, bbox[3])
    loca.append(len(glyf))
    n = len(glyphs)
    bold = style == "bold"
    now = int(time.time()) + 2082844800
    tables = {
        b"head": struct.pack(">LLLLHHqqhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, UPM, now, now,
                             xmin, ymin, xmax, ymax, 1 if bold else 0, 8, 2, 1, 0),
        b"hhea": struct.pack(">LhhhHhhhhhhhhhhhH", 0x00010000, ASC, DESC, P, max(a for _, a in glyphs), min(0, xmin), 0, xmax,
                             1, 0, 0, 0, 0, 0, 0, 0, n),
        b"maxp": struct.pack(">LHHHHHHHHHHHHHH", 0x00010000, n, maxp_pts, maxp_cont, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0),
        b"OS/2": struct.pack(">HhHHH" + "h" * 10 + "h10s4L4sHHHhhhHH2LhhHHH", 4, 6 * P, 700 if bold else 400, 5, 0,
                             650, 700, 0, 140, 650, 700, 0, 480, P, 3 * P, 0, bytes(10), 3, 0, 0, 0, b"AETH",
                             0x20 if bold else 0x40, 0x20, 0xFF, ASC, DESC, P, ASC, -DESC, 1, 0, 5 * P, 7 * P, 0, 0x20, 1),
        b"hmtx": hmtx,
        b"cmap": cmap_table({ord(ch): i + 2 for i, ch in enumerate(CHARS)} | {32: 1}),
        b"loca": b"".join(struct.pack(">L", o) for o in loca),
        b"glyf": glyf,
        b"name": name_table(family, sub),
        b"post": struct.pack(">LLhhLLLLL", 0x00030000, 0, -P, P, 0, 0, 0, 0, 0),
    }
    tags = sorted(tables)
    count = len(tags)
    search = 16 * (1 << (count.bit_length() - 1))
    header = struct.pack(">LHHHH", 0x00010000, count, search, (search // 16).bit_length() - 1, count * 16 - search)
    offset = 12 + 16 * count
    directory, body = b"", b""
    for tag in tags:
        data = tables[tag]
        directory += struct.pack(">4sLLL", tag, checksum(data), offset + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    font = bytearray(header + directory + body)
    head_at = 12 + 16 * tags.index(b"head")
    head_offset = struct.unpack(">L", font[head_at + 8:head_at + 12])[0]
    font[head_offset + 8:head_offset + 12] = struct.pack(">L", (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF)
    return bytes(font)

# ---------------------------------------------------------------- PNG specimen

def specimen(path, lines, scale=4):
    pix_lines = []
    for text, style in lines:
        cols = []
        for ch in text:
            grid = full_grid(ch) if ch != " " else ["...."] * 11
            if style == "bold" and ch != " ":
                grid = [embolden(r) for r in grid]
            for c in range(len(grid[0])):
                cols.append([grid[r][c] == "#" for r in range(11)])
            cols.append([False] * 11)
        pix_lines.append((cols, style))
    width = max(len(c) for c, _ in pix_lines) * scale + 2 * 8 * scale
    height = (len(pix_lines) * 14 + 4) * scale
    img = [[(24, 30, 36)] * width for _ in range(height)]
    for li, (cols, style) in enumerate(pix_lines):
        oy = (2 + li * 14) * scale
        color = (236, 214, 150) if style == "runa" else (230, 234, 226)
        for x, col in enumerate(cols):
            for y, on in enumerate(col):
                if on:
                    for dy in range(scale):
                        for dx in range(scale):
                            if style == "runa" and (dx in (0, scale - 1)) and (dy in (0, scale - 1)):
                                continue  # octagonal pixels
                            img[oy + y * scale + dy][8 * scale + x * scale + dx] = color
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in img)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "SourceArt", "Fonts")
    os.makedirs(out, exist_ok=True)
    for family, sub, style, file in [("Aethra Pixel", "Regular", "pixel", "AethraPixel-Regular.ttf"),
                                     ("Aethra Pixel", "Bold", "bold", "AethraPixel-Bold.ttf"),
                                     ("Aethra Runa", "Regular", "runa", "AethraRuna-Regular.ttf")]:
        data = build_font(family, sub, style)
        with open(os.path.join(out, file), "wb") as f:
            f.write(data)
        print(file, len(data), "bytes,", len(CHARS) + 2, "glyphs")
    specimen(os.path.join(out, "specimen.png"), [
        ("ECOS DE AETHRA", "runa"),
        ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "pixel"),
        ("abcdefghijklmnopqrstuvwxyz 0123456789", "pixel"),
        ("Ação, coração, pão, avô, você, até já! Çá Õ É Ü", "pixel"),
        (".,:;!?'\"-+=_/\\()[]{}<>*#%&@$^~|° «»", "pixel"),
        ("Grama | 1-4 blocos | clique esquerdo remove", "bold"),
        ("Caverna Encantada - Ruinas de Aldarim", "runa"),
    ])
    print("specimen.png")

if __name__ == "__main__":
    main()
