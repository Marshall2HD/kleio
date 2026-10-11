"""Generates Kleio's MSIX logo assets and the app .ico from a single vector-ish drawing.

Run from the repository root:  python tools/generate_assets.py
Requires Pillow.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "src" / "Kleio" / "Assets"
S = 1024  # master canvas size


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def glyph(size, plate=True):
    """Draws the Kleio mark: a clipboard holding a short stack of history lines."""
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if plate:
        grad = Image.new("RGBA", (S, S))
        top, bottom = (0x3A, 0x8D, 0xFF), (0x6A, 0x3D, 0xE8)
        gd = ImageDraw.Draw(grad)
        for y in range(S):
            gd.line([(0, y), (S, y)], fill=lerp(top, bottom, y / S) + (255,))
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=220, fill=255)
        img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    accent = (0x4A, 0x6C, 0xF7, 255)
    # Board
    d.rounded_rectangle([250, 210, 774, 860], radius=70, fill=white)
    # Clip
    d.rounded_rectangle([392, 150, 632, 290], radius=48, fill=white)
    d.rounded_rectangle([432, 190, 592, 250], radius=26, fill=accent)
    # History lines
    for i, w in enumerate([400, 330, 370, 250]):
        y = 380 + i * 115
        d.rounded_rectangle([312, y, 312 + w, y + 52], radius=26, fill=accent if i == 0 else (0xB8, 0xC4, 0xF5, 255))
    return img.resize((size, size), Image.LANCZOS)


def save_png(name, w, h, scale_fill=0.8):
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    side = int(min(w, h) * scale_fill)
    g = glyph(side)
    canvas.paste(g, ((w - side) // 2, (h - side) // 2), g)
    canvas.save(ASSETS / name)


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    for scale in (100, 200):
        f = scale / 100
        suffix = f".scale-{scale}"
        save_png(f"Square150x150Logo{suffix}.png", int(150 * f), int(150 * f), 0.66)
        save_png(f"Square44x44Logo{suffix}.png", int(44 * f), int(44 * f), 1.0)
        save_png(f"Wide310x150Logo{suffix}.png", int(310 * f), int(150 * f), 0.66)
        save_png(f"SplashScreen{suffix}.png", int(620 * f), int(300 * f), 0.5)
        save_png(f"StoreLogo{suffix}.png", int(50 * f), int(50 * f), 1.0)
    for size in (16, 24, 32, 48, 256):
        save_png(f"Square44x44Logo.targetsize-{size}_altform-unplated.png", size, size, 1.0)
    big = glyph(256)
    big.save(ASSETS / "Kleio.ico", sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
    glyph(512).save(ROOT / "docs" / "logo.png")


if __name__ == "__main__":
    main()
