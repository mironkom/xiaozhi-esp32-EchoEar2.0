"""Generate 9 unique left-eye animations in the original EchoEar visual language.

Rules learned from the originals:
- Canvas is 125x160 and holds ONE eye (the LEFT one). The firmware mirrors it
  for the right eye (layout.json: eye_anim mirror="auto"), so designs must be
  a single shape, complete inside the canvas (nothing may run off an edge).
- Original neutral eye: circle centered ~(73,78), r~51. Other emotes keep
  roughly that mass and position.
- Palette: blob (230,246,254), dark pupil/details (25,27,30), pink (254,182,182).
"""
import math, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image, ImageDraw
from eaf_encode import encode_eaf

W, H = 125, 160
SS = 4  # supersampling
BLOB = (230, 246, 254, 255)
DARK = (25, 27, 30, 255)
PINK = (254, 182, 182, 255)
CX, CY = 73, 78          # original eye center
FPS = 20

OUT_DIR = Path(__file__).resolve().parents[2] / "main" / "boards" / "ostb-echoear-2st" / "emoji"
OUT_DIR.mkdir(parents=True, exist_ok=True)


def canvas():
    return Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))


def down(img):
    return img.resize((W, H), Image.LANCZOS)


def E(d, cx, cy, rx, ry, fill):
    d.ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=fill)


def ease(t):
    return 0.5 - 0.5 * math.cos(t * 2 * math.pi)


# ---------------------------------------------------------------- happy (soft)
# Replaces stock Happy.eaf for the "happy" emote: the same content smile arc,
# but WITHOUT the flying hearts (hearts now belong to "loving" only) and with
# a calm slow sway instead of laughing's vigorous bounce.
def gen_happy_soft(n=24):
    frames = []
    for i in range(n):
        t = i / n
        sway = 3 * math.sin(t * 2 * math.pi)          # gentle side-to-side
        breathe = 1.0 + 0.04 * math.sin(t * 2 * math.pi)  # subtle size breathing
        img = canvas(); d = ImageDraw.Draw(img)
        cx = CX + sway * 0.5
        rx, ry = 45 * breathe, 44 * breathe
        cy = CY + sway * 0.3
        # thick content arc, slightly wider opening than laughing
        E(d, cx, cy, rx, ry, BLOB)
        E(d, cx, cy + 14, rx - 20, ry - 15, (0, 0, 0, 0))
        d.rectangle([0, (cy + ry * 0.26) * SS, W * SS, H * SS], fill=(0, 0, 0, 0))
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- laughing
# Closed-eye "smile arc" that bounces and squashes, like shaking with laughter.
def gen_laughing(n=16):
    frames = []
    for i in range(n):
        t = i / n
        squash = 1.0 + 0.14 * math.sin(t * 2 * math.pi * 2)   # two "ha-ha" per loop
        bob = -5 * abs(math.sin(t * 2 * math.pi * 2))
        img = canvas(); d = ImageDraw.Draw(img)
        rx, ry = 47, 46 * squash
        cy = CY + bob
        # thick arc: outer dome minus inner dome minus lower half
        E(d, CX, cy, rx, ry, BLOB)
        E(d, CX, cy + 12, rx - 22, ry - 16, (0, 0, 0, 0))
        d.rectangle([0, (cy + ry * 0.30) * SS, W * SS, H * SS], fill=(0, 0, 0, 0))
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- funny
# Playful eye with a little satellite dot orbiting around it.
def gen_funny(n=16):
    frames = []
    for i in range(n):
        t = i / n
        img = canvas(); d = ImageDraw.Draw(img)
        E(d, 70, 80, 38, 38, BLOB)
        a = t * 2 * math.pi
        ox = 70 + math.cos(a) * 32
        oy = 80 + math.sin(a) * 46
        E(d, ox, oy, 9, 9, BLOB)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- loving
# The whole eye becomes a heart, pulsing like a heartbeat (two quick thumps).
def heart_pts(cx, cy, size):
    pts = []
    for deg in range(0, 360, 3):
        a = math.radians(deg)
        x = 16 * math.sin(a) ** 3
        y = -(13 * math.cos(a) - 5 * math.cos(2 * a) - 2 * math.cos(3 * a) - math.cos(4 * a))
        pts.append(((cx + x * size / 16) * SS, (cy + y * size / 16) * SS))
    return pts


def gen_loving(n=20):
    frames = []
    for i in range(n):
        t = i / n
        # heartbeat: thump-thump then rest
        beat = math.exp(-((t - 0.15) ** 2) / 0.004) + 0.8 * math.exp(-((t - 0.40) ** 2) / 0.004)
        size = 42 + 6 * beat
        img = canvas(); d = ImageDraw.Draw(img)
        d.polygon(heart_pts(70, 76, size), fill=BLOB)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- embarrassed
# Shy eye glancing down, with a pink blush on the cheek fading in and out.
def gen_embarrassed(n=20):
    frames = []
    for i in range(n):
        t = i / n
        shy = ease(t)
        img = canvas(); d = ImageDraw.Draw(img)
        E(d, 72, 72 + 5 * shy, 46, 46 - 5 * shy, BLOB)
        # encoder alpha is binary, so pulse the blush via brightness (bg is black)
        dim = 0.45 + 0.55 * shy
        blush = (int(PINK[0] * dim), int(PINK[1] * dim), int(PINK[2] * dim), 255)
        E(d, 70, 130, 29, 11, blush)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- confident
# Smug half-lidded eye: a heavy flat lid sits low over the circle.
def gen_confident(n=20):
    frames = []
    for i in range(n):
        t = i / n
        lid = 34 + 8 * ease(t)   # lid slides a little lower, then back up
        img = canvas(); d = ImageDraw.Draw(img)
        E(d, 73, 82, 48, 48, BLOB)
        d.rectangle([0, 0, W * SS, (82 - 48 + lid) * SS], fill=(0, 0, 0, 0))
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- delicious
# "Stars in the eyes": a dark four-point star pupil twinkles (pulses and
# slowly rotates) inside a gently wobbling delighted eye.
def star_pts(cx, cy, r_out, r_in, rot):
    pts = []
    for k in range(8):
        a = rot + k * math.pi / 4
        r = r_out if k % 2 == 0 else r_in
        pts.append(((cx + math.cos(a) * r) * SS, (cy + math.sin(a) * r) * SS))
    return pts


def gen_delicious(n=20):
    frames = []
    for i in range(n):
        t = i / n
        wob = 1.0 + 0.04 * math.sin(t * 2 * math.pi * 2)
        twinkle = 1.0 + 0.22 * math.sin(t * 2 * math.pi * 2)
        rot = -math.pi / 2 + 0.25 * math.sin(t * 2 * math.pi)
        img = canvas(); d = ImageDraw.Draw(img)
        E(d, 72, 78, 46 * wob, 46 / wob, BLOB)
        d.polygon(star_pts(72, 78, 26 * twinkle, 9 * twinkle, rot), fill=DARK)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- silly
# Googly eye: dark pupil rolling a full loop around the inside edge.
def gen_silly(n=16):
    frames = []
    for i in range(n):
        t = i / n
        img = canvas(); d = ImageDraw.Draw(img)
        E(d, 73, 78, 48, 48, BLOB)
        a = t * 2 * math.pi - math.pi / 2
        px = 73 + math.cos(a) * 29
        py = 78 + math.sin(a) * 29
        E(d, px, py, 15, 15, DARK)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- surprised
# Eye pops wide while the pupil shrinks to a point, with a tiny tremble.
def gen_surprised(n=16):
    frames = []
    for i in range(n):
        t = i / n
        pop = math.exp(-t * 3.5)            # strong at loop start, relaxes
        jit = 1.5 * math.sin(t * 2 * math.pi * 4) * pop
        img = canvas(); d = ImageDraw.Draw(img)
        r = 42 + 7 * pop
        E(d, 72 + jit, 78, r, r, BLOB)
        pr = 16 - 8 * pop
        E(d, 72 + jit, 78, pr, pr, DARK)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- relaxed
# Heavy-lidded calm eye slowly "breathing" — almost closed, soft and wide.
def gen_relaxed(n=24):
    frames = []
    for i in range(n):
        t = i / n
        breathe = ease(t)
        img = canvas(); d = ImageDraw.Draw(img)
        h = 21 + 7 * breathe
        cy = 92 - 3 * breathe
        d.rounded_rectangle([(72 - 49) * SS, int((cy - h) * SS), (72 + 49) * SS, int((cy + h) * SS)],
                            radius=int(h * SS) - 2, fill=BLOB)
        # domed top: soft bulge in the middle
        E(d, 72, cy - h + 4, 34, 12 + 4 * breathe, BLOB)
        frames.append(down(img))
    return frames


# ---------------------------------------------------------------- dozing
# Standby loop (~10s): awake blinks, eyelid slowly droops, fights sleep once,
# dozes off (breathing slit + rising dream bubbles), then startles awake.
# Bubbles are plain circles: letterforms like "Z" would mirror into garbage
# on the right eye.
def smoothstep(a, b, t):
    if t <= a:
        return 0.0
    if t >= b:
        return 1.0
    x = (t - a) / (b - a)
    return x * x * (3 - 2 * x)


def gen_dozing(n=200):
    frames = []
    for i in range(n):
        t = i / n
        # eyelid openness: 1 = wide awake, 0 = shut
        open_f = 1.0
        open_f -= 0.55 * smoothstep(0.22, 0.40, t)   # slow droop to half-lidded
        open_f += 0.30 * smoothstep(0.40, 0.46, t)   # catches itself, lifts
        open_f -= 0.75 * smoothstep(0.50, 0.62, t)   # gives in, closes
        # startle awake near the loop end
        open_f += 1.0 * smoothstep(0.90, 0.94, t)
        open_f = max(0.0, min(1.0, open_f))
        # awake blinks at t~0.08 and t~0.16
        for bt in (0.08, 0.16):
            if abs(t - bt) < 0.022:
                open_f *= abs(t - bt) / 0.022
        # sleeping breath
        if 0.62 <= t < 0.90:
            open_f += 0.03 * math.sin((t - 0.62) * 2 * math.pi * 3)
        # small overshoot right after startle
        if 0.94 <= t < 1.0:
            open_f = 1.0 + 0.08 * math.sin((t - 0.94) * math.pi / 0.06)

        drowse = 1.0 - smoothstep(0.90, 0.94, t)  # how far into the doze we are
        sink = 10 * smoothstep(0.22, 0.62, t) * drowse
        cy = 78 + sink
        ry = max(6.0, 44 * open_f)
        img = canvas(); d = ImageDraw.Draw(img)
        E(d, 72, cy, 46, ry, BLOB)

        # dream bubbles while asleep: two circles rising and fading
        if 0.64 <= t < 0.90:
            for k, phase in enumerate((0.0, 0.5)):
                bp = ((t - 0.64) / 0.26 + phase) % 1.0
                bx = 95 + 6 * math.sin(bp * math.pi * 2 + k)
                by = (cy - 22) - 44 * bp
                br = 5 + 4 * bp
                # encoder alpha is binary, so fade via brightness (bg is black)
                dim = 1 - 0.8 * bp
                E(d, bx, by, br, br, (int(BLOB[0] * dim), int(BLOB[1] * dim), int(BLOB[2] * dim), 255))
        frames.append(down(img))
    return frames


GENERATORS = {
    "happy_soft": gen_happy_soft,
    "dozing": gen_dozing,
    "laughing": gen_laughing,
    "funny": gen_funny,
    "loving": gen_loving,
    "embarrassed": gen_embarrassed,
    "confident": gen_confident,
    "delicious": gen_delicious,
    "silly": gen_silly,
    "surprised": gen_surprised,
    "relaxed": gen_relaxed,
}

if __name__ == "__main__":
    import numpy as np
    for name, fn in GENERATORS.items():
        frames = fn()
        # bounds check: no content may touch the outer 2px ring (would look cut)
        minx, miny, maxx, maxy = 999, 999, -1, -1
        for fr in frames:
            arr = np.array(fr.convert("L"))
            ys, xs = (arr > 3).nonzero()
            if len(xs):
                minx = min(minx, xs.min()); maxx = max(maxx, xs.max())
                miny = min(miny, ys.min()); maxy = max(maxy, ys.max())
        clipped = maxx >= W - 1 or minx <= 0 or maxy >= H - 1 or miny <= 0
        rgb_frames = []
        for fr in frames:
            rgb_frames.append(fr)
        data = encode_eaf(frames, fps=FPS)
        out_path = OUT_DIR / f"{name}.eaf"
        out_path.write_bytes(data)
        flag = "  !! TOUCHES EDGE" if clipped else ""
        print(f"{name:12s} {len(frames):2d} frames  bbox x:[{minx},{maxx}] y:[{miny},{maxy}]  {len(data):6d} bytes{flag}")
