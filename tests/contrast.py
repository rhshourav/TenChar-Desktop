#!/usr/bin/env python3
"""WCAG contrast regression test for the design system, measured per actual usage.

Token values are READ from the QML files, so changing a colour there is checked here. Backgrounds come from the
real aurora formulas (Aurora.qml) sampled across the centred content column at several window sizes and at 16
phases of the 60 s loop; glass plates are composited as GlassPlate.qml draws them (scrim, then the lightest end of
the white fill, then the tint). Exit code 1 if any minimum is missed."""
import math, re, sys, pathlib

QML = pathlib.Path(__file__).resolve().parent.parent / "app" / "qml"
tc_src, plate_src, chat_src = ((QML / n).read_text() for n in ("Tc.qml", "GlassPlate.qml", "ChatScreen.qml"))

def hexc(s): return tuple(int(s[i:i+2], 16) / 255 for i in (1, 3, 5))
def token(name): return hexc(re.search(name + r': "(#[0-9A-Fa-f]{6})"', tc_src).group(1))
def alpha_token(name): return float(re.search(name + r': Qt\.rgba\([^)]*?,\s*([0-9.]+)\)', tc_src).group(1))
SPACE, PLUM, MINT, LAV, LAVS, PEACH, INDIGO, INK = (token(n) for n in
    ("space", "plum", "mint", "lavender", "lavenderSoft", "peach", "indigo", "ink"))
PEACH_TEXT = token("peachText")
MUTED, FAINT = alpha_token("inkMuted"), alpha_token("inkFaint")
SCRIM = float(re.search(r"scrim: ([0-9.]+)", plate_src).group(1))
MINE_A = float(re.search(r"Tc\.alpha\(Tc\.indigo, ([0-9.]+)\)", chat_src).group(1))
MINE_B = float(re.search(r"Tc\.alpha\(Tc\.lavender, ([0-9.]+)\)", chat_src).group(1))
WHITE = (1, 1, 1)

def over(top, a, bottom): return tuple(t * a + b * (1 - a) for t, b in zip(top, bottom))
def lin(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
def lum(c): r, g, b = map(lin, c); return 0.2126 * r + 0.7152 * g + 0.0722 * b
def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)

def aurora_at(x, y, w, h, a):
    glows = [(PLUM, 1.0, 0.5 * w, 0.45 * h, 0.95 * max(w, h)),
             (INDIGO, 0.55, w * (0.10 + 0.10 * math.sin(a)), h * (0.10 + 0.04 * math.cos(a)), w * 1.0),
             (LAV, 0.22, w * (0.95 - 0.10 * math.cos(a)), h * (0.30 + 0.05 * math.sin(a)), w * 0.7),
             (PEACH, 0.14, w * (0.90 + 0.06 * math.sin(a + 2)), h * (0.95 - 0.04 * math.cos(a)), w * 0.8),
             (MINT, 0.12, w * (0.05 + 0.08 * math.cos(a + 1)), h * (0.88 + 0.04 * math.sin(a)), w * 0.7)]
    c = SPACE
    for col, alpha, cx, cy, r in glows:
        d = math.hypot(x - cx, y - cy)
        if d < r: c = over(col, alpha * (1 - d / r), c)
    return c

backs = []
for (w, h) in [(360, 520), (420, 640), (800, 700), (1280, 800), (1920, 1080)]:
    col0, col1 = (w - min(w, 560)) / 2, (w + min(w, 560)) / 2
    for k in range(16):
        for gx in range(21):
            for gy in range(21):
                backs.append(aurora_at(col0 + (col1 - col0) * gx / 20, h * gy / 20, w, h, 2 * math.pi * k / 16))
backs = sorted(backs, key=lambda c: -lum(c))[:400]      # light text is worst on the lightest backgrounds

def surface(back, kind):
    if kind == "aurora": return back                      # text placed directly on the background
    plate = over(WHITE, 0.13, over(SPACE, SCRIM, back))   # scrim, then the lightest end of the glass fill
    return {"plate": plate, "peach tint": over(PEACH, 0.14, plate), "lavender tint": over(LAV, 0.14, plate),
            "their bubble": over(PEACH, 0.07, plate), "my bubble (start)": over(INDIGO, MINE_A, plate),
            "my bubble (end)": over(LAV, MINE_B, plate)}[kind]

# (what, foreground, surfaces it is used on, minimum ratio)
text = lambda a: (lambda s: over(INK, a, s))
checks = [
    ("Ink text", text(1.0), ["plate", "peach tint", "lavender tint", "their bubble", "my bubble (start)", "my bubble (end)", "aurora"], 4.5),
    ("InkMuted text (details, body)", text(MUTED), ["plate", "aurora"], 4.5),
    ("InkFaint (non-text: chevrons)", text(FAINT), ["plate"], 3.0),
    ("Peach text (errors, caution)", lambda s: PEACH_TEXT, ["plate", "peach tint", "aurora"], 4.5),
    ("Mint focus ring (non-text)", lambda s: MINT, ["plate", "peach tint", "my bubble (end)", "aurora"], 3.0),
]
fail = 0
print(f"tokens: InkMuted {MUTED}  InkFaint {FAINT}  peachText {PEACH_TEXT}  scrim {SCRIM}  my bubble {MINE_A}/{MINE_B}")
print(f"{'foreground':34} {'worst':>6} {'min':>4}  on")
for name, fg, kinds, need in checks:
    worst, where = 99, ""
    for b in backs:
        for k in kinds:
            s = surface(b, k); r = ratio(fg(s), s)
            if r < worst: worst, where = r, k
    ok = worst >= need
    fail += not ok
    print(f"{name:34} {worst:6.2f} {need:4}  {where}{'' if ok else '   <-- BELOW MINIMUM'}")
for name, fg, bg, need in [("Space on brand button (mint end)", SPACE, MINT, 4.5), ("Space on brand button (lavender end)", SPACE, LAV, 4.5),
                           ("Ink on dialog (plum)", INK, PLUM, 4.5), ("InkMuted on dialog (plum)", over(INK, MUTED, PLUM), PLUM, 4.5),
                           ("Mint text on dialog (plum)", MINT, PLUM, 4.5), ("Lavender text on dialog (plum)", LAV, PLUM, 4.5)]:
    r = ratio(fg, bg); ok = r >= need; fail += not ok
    print(f"{name:34} {r:6.2f} {need:4}  {'' if ok else '   <-- BELOW MINIMUM'}")
print("FAILED" if fail else "all pass")
sys.exit(1 if fail else 0)
