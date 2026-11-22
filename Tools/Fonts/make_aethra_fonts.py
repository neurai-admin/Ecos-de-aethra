"""Original fonts of ECOS DE AETHRA, drawn here stroke by stroke (no third-party font copied or traced).

  Aethra Entalhe       - text and HUD: carved-stone letters. Straight chiselled strokes, bowls cut as
                         octagons, stroke ends cut on a slant like a chisel leaves them.
  Aethra Entalhe Bold  - the same letters with heavier strokes.
  Aethra Runa          - titles: heavy strokes with wedge flares at every free end, like inscriptions.

Writes TrueType files (pure standard library: no fontTools) and a PNG specimen.
  python Tools/Fonts/make_aethra_fonts.py [output folder]   (default: SourceArt/Fonts)

Design units: 1000 per em. Capitals and digits are 700 high, lowercase x-height 480, ascenders 720,
descenders to -200, baseline 0. Each glyph is a list of strokes (polylines along the stroke centre);
a stroke becomes overlapping quads that the renderer fills with the non-zero rule.
"""
import math
import os
import struct
import sys
import time
import zlib

UPM = 1000
ASC, DESC = 920, -260
CAP, XH = 700, 480

# ---------------------------------------------------------------- glyph skeletons

S = {}  # char -> list of strokes; a stroke is a list of (x, y)

def oct_box(x0, y0, x1, y1, c):
    """Closed octagon (a carved bowl) inside the box, corner cut c."""
    return [(x0 + c, y0), (x0, y0 + c), (x0, y1 - c), (x0 + c, y1), (x1 - c, y1), (x1, y1 - c), (x1, y0 + c), (x1 - c, y0), (x0 + c, y0)]

def mirror(strokes, width):
    return [[(width - x, y) for x, y in s] for s in strokes]

def dot(x, y):
    return [(x, y), (x, y + 90)]

# Capitals
S["A"] = [[(0, 0), (275, 700), (550, 0)], [(110, 250), (440, 250)]]
S["B"] = [[(0, 0), (0, 700), (320, 700), (420, 610), (420, 450), (320, 370), (0, 370)],
          [(320, 370), (460, 290), (460, 90), (360, 0), (0, 0)]]
S["C"] = [[(520, 610), (420, 700), (140, 700), (0, 560), (0, 140), (140, 0), (420, 0), (520, 90)]]
S["D"] = [[(0, 0), (0, 700), (310, 700), (480, 530), (480, 170), (310, 0), (0, 0)]]
S["E"] = [[(460, 700), (0, 700), (0, 0), (460, 0)], [(0, 370), (340, 370)]]
S["F"] = [[(460, 700), (0, 700), (0, 0)], [(0, 370), (340, 370)]]
S["G"] = [[(520, 610), (420, 700), (140, 700), (0, 560), (0, 140), (140, 0), (420, 0), (520, 100), (520, 320), (300, 320)]]
S["H"] = [[(0, 0), (0, 700)], [(480, 0), (480, 700)], [(0, 370), (480, 370)]]
S["I"] = [[(0, 0), (0, 700)]]
S["J"] = [[(380, 700), (380, 140), (240, 0), (100, 0), (0, 110)]]
S["K"] = [[(0, 0), (0, 700)], [(460, 700), (0, 290)], [(150, 420), (480, 0)]]
S["L"] = [[(0, 700), (0, 0), (440, 0)]]
S["M"] = [[(0, 0), (0, 700), (300, 260), (600, 700), (600, 0)]]
S["N"] = [[(0, 0), (0, 700), (480, 0), (480, 700)]]
S["O"] = [oct_box(0, 0, 560, 700, 150)]
S["P"] = [[(0, 0), (0, 700), (330, 700), (460, 580), (460, 440), (330, 330), (0, 330)]]
S["Q"] = [oct_box(0, 0, 560, 700, 150), [(320, 170), (590, -80)]]
S["R"] = S["P"] + [[(250, 330), (480, 0)]]
S["S"] = [[(500, 610), (400, 700), (110, 700), (0, 600), (0, 450), (100, 370), (400, 330), (500, 250), (500, 100), (400, 0), (100, 0), (0, 90)]]
S["T"] = [[(0, 700), (520, 700)], [(260, 700), (260, 0)]]
S["U"] = [[(0, 700), (0, 140), (140, 0), (340, 0), (480, 140), (480, 700)]]
S["V"] = [[(0, 700), (260, 0), (520, 700)]]
S["W"] = [[(0, 700), (170, 0), (340, 470), (510, 0), (680, 700)]]
S["X"] = [[(0, 700), (500, 0)], [(500, 700), (0, 0)]]
S["Y"] = [[(0, 700), (260, 350), (520, 700)], [(260, 350), (260, 0)]]
S["Z"] = [[(0, 700), (480, 700), (0, 0), (480, 0)]]

# Lowercase
BOWL_R = [(0, 380), (110, 480), (290, 480), (400, 380), (400, 100), (290, 0), (110, 0), (0, 100)]
S["a"] = [[(50, 430), (140, 480), (320, 480), (400, 400), (400, 0)], [(400, 270), (110, 270), (0, 190), (0, 80), (80, 0), (300, 0), (400, 90)]]
S["b"] = [[(0, 720), (0, 0)], BOWL_R]
S["c"] = [[(400, 400), (320, 480), (110, 480), (0, 380), (0, 100), (110, 0), (320, 0), (400, 80)]]
S["d"] = mirror(S["b"], 400)
S["e"] = [[(0, 250), (400, 250), (400, 380), (300, 480), (100, 480), (0, 380), (0, 100), (100, 0), (320, 0), (400, 70)]]
S["f"] = [[(320, 700), (240, 720), (170, 720), (100, 650), (100, 0)], [(0, 460), (300, 460)]]
S["g"] = [[(400, 480), (400, -110), (300, -200), (90, -200), (10, -130)],
          [(400, 380), (290, 480), (110, 480), (0, 380), (0, 150), (110, 50), (290, 50), (400, 150)]]
S["h"] = [[(0, 720), (0, 0)], [(0, 380), (110, 480), (290, 480), (400, 380), (400, 0)]]
S["i"] = [[(0, 0), (0, 480)], dot(0, 600)]
S["j"] = [[(100, 480), (100, -120), (20, -200), (-60, -200)], dot(100, 600)]
S["k"] = [[(0, 720), (0, 0)], [(380, 480), (0, 190)], [(120, 280), (400, 0)]]
S["l"] = [[(0, 720), (0, 80), (70, 0), (130, 0)]]
S["m"] = [[(0, 0), (0, 480)], [(0, 380), (90, 480), (220, 480), (300, 400), (300, 0)], [(300, 400), (380, 480), (510, 480), (600, 380), (600, 0)]]
S["n"] = [[(0, 0), (0, 480)], [(0, 380), (110, 480), (290, 480), (400, 380), (400, 0)]]
S["o"] = [oct_box(0, 0, 420, 480, 110)]
S["p"] = [[(0, 480), (0, -200)], BOWL_R]
S["q"] = mirror(S["p"], 400)
S["r"] = [[(0, 0), (0, 480)], [(0, 350), (130, 480), (320, 480)]]
S["s"] = [[(380, 420), (300, 480), (90, 480), (0, 410), (0, 320), (80, 260), (320, 220), (400, 160), (400, 70), (320, 0), (80, 0), (0, 60)]]
S["t"] = [[(120, 680), (120, 100), (200, 0), (300, 0)], [(0, 460), (300, 460)]]
S["u"] = [[(0, 480), (0, 100), (110, 0), (290, 0), (400, 100)], [(400, 480), (400, 0)]]
S["v"] = [[(0, 480), (220, 0), (440, 480)]]
S["w"] = [[(0, 480), (140, 0), (290, 330), (440, 0), (580, 480)]]
S["x"] = [[(0, 480), (400, 0)], [(400, 480), (0, 0)]]
S["y"] = [[(0, 480), (220, 0)], [(440, 480), (150, -200), (70, -200)]]
S["z"] = [[(0, 480), (400, 480), (0, 0), (400, 0)]]
S["ı"] = [[(0, 0), (0, 480)]]  # dotless i, base of í ì î ï

# Digits
S["0"] = [oct_box(0, 0, 480, 700, 130), [(130, 170), (350, 530)]]
S["1"] = [[(60, 560), (200, 700), (200, 0)], [(40, 0), (360, 0)]]
S["2"] = [[(0, 580), (110, 700), (350, 700), (460, 600), (460, 450), (0, 0), (470, 0)]]
S["3"] = [[(0, 630), (100, 700), (360, 700), (460, 610), (460, 460), (360, 380), (160, 380)],
          [(360, 380), (470, 300), (470, 100), (370, 0), (100, 0), (0, 70)]]
S["4"] = [[(380, 0), (380, 700), (0, 220), (500, 220)]]
S["5"] = [[(460, 700), (30, 700), (0, 380), (330, 400), (460, 300), (460, 100), (360, 0), (80, 0), (0, 70)]]
S["6"] = [[(420, 660), (330, 700), (140, 700), (0, 560), (0, 120), (120, 0), (340, 0), (460, 110), (460, 300), (340, 410), (120, 410), (0, 320)]]
S["7"] = [[(0, 700), (470, 700), (160, 0)]]
S["8"] = [[(120, 380), (20, 460), (20, 610), (110, 700), (350, 700), (440, 610), (440, 460), (340, 380), (120, 380),
           (0, 290), (0, 100), (100, 0), (360, 0), (460, 100), (460, 290), (340, 380)]]
S["9"] = [[(40, 50), (130, 0), (320, 0), (460, 140), (460, 580), (340, 700), (120, 700), (0, 600), (0, 400), (120, 290), (340, 290), (460, 380)]]

# Punctuation and symbols
S["."] = [dot(0, 0)]
S[","] = [[(60, 90), (60, 0), (0, -110)]]
S[":"] = [dot(0, 0), dot(0, 380)]
S[";"] = [[(60, 90), (60, 0), (0, -110)], dot(60, 380)]
S["!"] = [[(0, 700), (0, 230)], dot(0, 0)]
S["?"] = [[(0, 600), (100, 700), (330, 700), (430, 600), (430, 460), (220, 330), (220, 230)], dot(220, 0)]
S["'"] = [[(0, 700), (0, 500)]]
S['"'] = [[(0, 700), (0, 500)], [(160, 700), (160, 500)]]
S["-"] = [[(0, 300), (300, 300)]]
S["+"] = [[(0, 300), (400, 300)], [(200, 100), (200, 500)]]
S["="] = [[(0, 200), (400, 200)], [(0, 400), (400, 400)]]
S["_"] = [[(0, -120), (500, -120)]]
S["/"] = [[(0, -60), (380, 760)]]
S["\\"] = [[(0, 760), (380, -60)]]
S["("] = [[(200, 760), (60, 620), (60, 80), (200, -60)]]
S[")"] = mirror(S["("], 260)
S["["] = [[(200, 760), (0, 760), (0, -60), (200, -60)]]
S["]"] = mirror(S["["], 200)
S["{"] = [[(240, 760), (140, 700), (140, 420), (40, 350), (140, 280), (140, 0), (240, -60)]]
S["}"] = mirror(S["{"], 280)
S["<"] = [[(400, 560), (0, 300), (400, 40)]]
S[">"] = mirror(S["<"], 400)
S["*"] = [[(200, 700), (200, 400)], [(60, 640), (340, 460)], [(340, 640), (60, 460)]]
S["#"] = [[(130, 0), (200, 700)], [(330, 0), (400, 700)], [(0, 230), (500, 230)], [(30, 470), (530, 470)]]
S["%"] = [[(0, 0), (480, 700)], oct_box(0, 500, 170, 700, 50), oct_box(310, 0, 480, 200, 50)]
S["&"] = [[(480, 0), (100, 480), (100, 620), (180, 700), (300, 700), (360, 620), (360, 520), (0, 250), (0, 100), (100, 0), (280, 0), (480, 260)]]
S["@"] = [[(500, 200), (500, 560), (380, 700), (130, 700), (0, 560), (0, 120), (130, 0), (420, 0)],
          [(500, 200), (330, 200), (260, 270), (260, 420), (330, 480), (500, 480)]]
S["$"] = S["S"] + [[(250, 800), (250, -100)]]
S["^"] = [[(0, 500), (200, 700), (400, 500)]]
S["~"] = [[(0, 300), (100, 380), (300, 300), (400, 380)]]
S["`"] = [[(0, 720), (120, 600)]]
S["|"] = [[(0, 760), (0, -160)]]
S["°"] = [oct_box(0, 480, 220, 700, 60)]
S["º"] = [oct_box(0, 440, 220, 700, 60), [(0, 340), (220, 340)]]
S["ª"] = [oct_box(0, 440, 220, 700, 60), [(220, 440), (220, 700)], [(0, 340), (220, 340)]]
S["«"] = [[(250, 420), (100, 240), (250, 60)], [(450, 420), (300, 240), (450, 60)]]
S["»"] = mirror(S["«"], 450)

# Accents, composed over a letter (centred on its width); caps carry them higher.
ACCENTS = {
    "acute": lambda cx, y: [[(cx - 60, y), (cx + 80, y + 120)]],
    "grave": lambda cx, y: [[(cx + 60, y), (cx - 80, y + 120)]],
    "circ": lambda cx, y: [[(cx - 120, y), (cx, y + 120), (cx + 120, y)]],
    "tilde": lambda cx, y: [[(cx - 130, y + 20), (cx - 50, y + 100), (cx + 50, y + 30), (cx + 130, y + 110)]],
    "diaer": lambda cx, y: [dot(cx - 100, y + 20), dot(cx + 100, y + 20)],
}
COMPOSE = {}
for base, marks in {"A": "ÁÀÂÃÄ", "E": "ÉÈÊ Ë", "I": "ÍÌÎ Ï", "O": "ÓÒÔÕÖ", "U": "ÚÙÛ Ü", "N": "   Ñ ",
                    "a": "áàâãä", "e": "éèê ë", "ı": "íìî ï", "o": "óòôõö", "u": "úùû ü", "n": "   ñ "}.items():
    for kind, ch in zip(["acute", "grave", "circ", "tilde", "diaer"], marks):
        if ch != " ":
            COMPOSE[ch] = (base, kind)

def skeleton(ch):
    if ch in S:
        return S[ch]
    if ch in COMPOSE:
        base, kind = COMPOSE[ch]
        strokes = S[base]
        xs = [x for s in strokes for x, _ in s]
        cx = (min(xs) + max(xs)) // 2
        return strokes + ACCENTS[kind](cx, 790 if base.isupper() else 590)
    if ch in "Çç":
        strokes = S["C" if ch == "Ç" else "c"]
        cx = 260 if ch == "Ç" else 200
        return strokes + [[(cx, 0), (cx, -80), (cx + 80, -130), (cx - 40, -210)]]
    raise KeyError(ch)

CHARS = [chr(c) for c in range(33, 127)] + list("ÁÀÂÃÄÉÈÊËÍÌÎÏÓÒÔÕÖÚÙÛÜÇÑáàâãäéèêëíìîïóòôõöúùûüçñ°ºª«»")

# ---------------------------------------------------------------- strokes to outlines

STYLES = {"regular": dict(w=84, flare=0.0), "bold": dict(w=128, flare=0.0), "runa": dict(w=116, flare=1.0)}

def clockwise(poly):
    area = sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))
    return poly if area < 0 else poly[::-1]  # TrueType: outer contours clockwise (y up)

def outline(strokes, style):
    """Overlapping polygons for the strokes: a quad per segment (joints overlap square, free ends
    cut on a chisel slant), and for the title style a wedge flare at every free end."""
    w, flare = STYLES[style]["w"], STYLES[style]["flare"]
    h = w / 2
    polys = []
    for s in strokes:
        closed = len(s) > 2 and s[0] == s[-1]
        n = len(s) - 1
        for i in range(n):
            (x0, y0), (x1, y1) = s[i], s[i + 1]
            L = math.hypot(x1 - x0, y1 - y0)
            if L == 0:
                continue
            dx, dy = (x1 - x0) / L, (y1 - y0) / L
            nx, ny = -dy, dx
            start_free = i == 0 and not closed
            end_free = i == n - 1 and not closed
            k = min(h * 0.55, L / 3)  # the chisel slant on free ends
            e0 = 0 if start_free else h
            e1 = 0 if end_free else h
            a = (x0 - dx * e0 + nx * h - (dx * k if start_free else 0), y0 - dy * e0 + ny * h - (dy * k if start_free else 0))
            b = (x0 - dx * e0 - nx * h + (dx * k if start_free else 0), y0 - dy * e0 - ny * h + (dy * k if start_free else 0))
            c = (x1 + dx * e1 - nx * h - (dx * k if end_free else 0), y1 + dy * e1 - ny * h - (dy * k if end_free else 0))
            d = (x1 + dx * e1 + nx * h + (dx * k if end_free else 0), y1 + dy * e1 + ny * h + (dy * k if end_free else 0))
            polys.append([a, b, c, d])
            if flare:
                for free, px, py, sx in ((start_free, x0, y0, -1), (end_free, x1, y1, 1)):
                    if free and L > w:
                        # Wedge: wide at the end, tapering back into the stroke.
                        f = w * 0.55 * flare
                        polys.append([(px + nx * (h + f), py + ny * (h + f)), (px - nx * (h + f), py - ny * (h + f)),
                                      (px - sx * dx * w * 1.1, py - sx * dy * w * 1.1)])
    return [clockwise([(int(round(x)), int(round(y))) for x, y in p]) for p in polys]

def glyph(ch, style):
    contours = outline(skeleton(ch), style)
    w = STYLES[style]["w"]
    xs = [x for c in contours for x, _ in c]
    shift = int(w * 0.6) - min(xs)  # left side bearing
    contours = [[(x + shift, y) for x, y in c] for c in contours]
    advance = max(x for c in contours for x, _ in c) + int(w * 0.6) + 40
    return contours, advance

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
               4: full, 5: "Version 2.000", 6: ps, 9: "ECOS DE AETHRA", 13: "All rights reserved by ECOS DE AETHRA."}
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
    glyphs = []
    notdef = [[(60, 0), (60, 700), (460, 700), (460, 0)], [(140, 80), (380, 80), (380, 620), (140, 620)]]
    glyphs.append((notdef, 560))
    glyphs.append(([], 300))
    for ch in CHARS:
        glyphs.append(glyph(ch, style))
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
    bold = style != "regular"
    now = int(time.time()) + 2082844800
    avg = sum(a for _, a in glyphs) // n
    tables = {
        b"head": struct.pack(">LLLLHHqqhhhhHHhhh", 0x00010000, 0x00020000, 0, 0x5F0F3CF5, 0x000B, UPM, now, now,
                             xmin, ymin, xmax, ymax, 1 if bold else 0, 8, 2, 1, 0),
        b"hhea": struct.pack(">LhhhHhhhhhhhhhhhH", 0x00010000, ASC, DESC, 60, max(a for _, a in glyphs), min(0, xmin), 0, xmax,
                             1, 0, 0, 0, 0, 0, 0, 0, n),
        b"maxp": struct.pack(">LHHHHHHHHHHHHHH", 0x00010000, n, maxp_pts, maxp_cont, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0),
        b"OS/2": struct.pack(">HhHHH" + "h" * 10 + "h10s4L4sHHHhhhHH2LhhHHH", 4, avg, 700 if bold else 400, 5, 0,
                             650, 700, 0, 140, 650, 700, 0, 480, 50, 300, 0, bytes(10), 3, 0, 0, 0, b"AETH",
                             0x20 if bold else 0x40, 0x20, 0xFF, ASC, DESC, 60, ASC, -DESC, 1, 0, XH, CAP, 0, 0x20, 1),
        b"hmtx": hmtx,
        b"cmap": cmap_table({ord(ch): i + 2 for i, ch in enumerate(CHARS)} | {32: 1}),
        b"loca": b"".join(struct.pack(">L", o) for o in loca),
        b"glyf": glyf,
        b"name": name_table(family, sub),
        b"post": struct.pack(">LLhhLLLLL", 0x00030000, 0, -120, 60, 0, 0, 0, 0, 0),
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

# ---------------------------------------------------------------- PNG specimen (non-zero scanline fill)

def specimen(path, lines, size=56):
    scale = size / UPM
    line_h = int(size * 1.35)
    widths = [sum(glyph(ch, st)[1] if ch != " " else 300 for ch in text) * scale for text, st in lines]
    width, height = int(max(widths)) + 2 * size, line_h * len(lines) + size // 2
    img = [[(24, 30, 36)] * width for _ in range(height)]
    for li, (text, style) in enumerate(lines):
        base_y = size // 3 + li * line_h + int(ASC * scale * 0.9)
        pen = size
        color = (236, 214, 150) if style == "runa" else (230, 234, 226)
        for ch in text:
            if ch == " ":
                pen += 300 * scale
                continue
            contours, adv = glyph(ch, style)
            edges = []
            for c in contours:
                for i in range(len(c)):
                    (x0, y0), (x1, y1) = c[i], c[(i + 1) % len(c)]
                    if y0 != y1:
                        edges.append((pen + x0 * scale, base_y - y0 * scale, pen + x1 * scale, base_y - y1 * scale))
            ys = [e[1] for e in edges] + [e[3] for e in edges]
            for py in range(max(0, int(min(ys))), min(height, int(max(ys)) + 1)):
                cy = py + 0.5
                xs = []
                for x0, y0, x1, y1 in edges:
                    if (y0 <= cy < y1) or (y1 <= cy < y0):
                        xs.append((x0 + (cy - y0) * (x1 - x0) / (y1 - y0), 1 if y1 > y0 else -1))
                xs.sort()
                wind = 0
                for k in range(len(xs) - 1):
                    wind += xs[k][1]
                    if wind != 0:
                        for px in range(max(0, int(xs[k][0] + 0.5)), min(width, int(xs[k + 1][0] + 0.5))):
                            img[py][px] = color
            pen += adv * scale
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in img)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "SourceArt", "Fonts")
    os.makedirs(out, exist_ok=True)
    for family, sub, style, file in [("Aethra Entalhe", "Regular", "regular", "AethraEntalhe-Regular.ttf"),
                                     ("Aethra Entalhe", "Bold", "bold", "AethraEntalhe-Bold.ttf"),
                                     ("Aethra Runa", "Regular", "runa", "AethraRuna-Regular.ttf")]:
        data = build_font(family, sub, style)
        with open(os.path.join(out, file), "wb") as f:
            f.write(data)
        print(file, len(data), "bytes,", len(CHARS) + 2, "glyphs")
    specimen(os.path.join(out, "specimen.png"), [
        ("ECOS DE AETHRA", "runa"),
        ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "regular"),
        ("abcdefghijklmnopqrstuvwxyz 0123456789", "regular"),
        ("Ação, coração, pão, avô, você, até já! Çá Õ É Ü", "regular"),
        (".,:;!?'\"-+=_/\\()[]{}<>*#%&@$^~|° «»", "regular"),
        ("Tijolos de pedra | Esc: menu | 1-8 escolher", "bold"),
        ("Caverna Encantada - Ruínas de Aldarim", "runa"),
    ])
    print("specimen.png")

if __name__ == "__main__":
    main()
