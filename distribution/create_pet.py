"""Generate AuraPet's original bundled animation and app icon."""
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent / "assets"
TAU = 2 * math.pi


def paint(size, phase, icon=False):
    image = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(image)
    scale = size / 192

    def box(bounds):
        return tuple(round(n * scale) for n in bounds)

    def outline(points, color, width=1):
        draw.line([box(p) for p in points], fill=color, width=max(1, round(width * scale)), joint="curve")

    def oval(bounds, color):
        draw.ellipse(box(bounds), fill=color)

    if icon:
        draw.rounded_rectangle(box((9, 9, 183, 183)), radius=round(39 * scale), fill="#302648")
        draw.rounded_rectangle(box((13, 13, 179, 179)), radius=round(37 * scale), outline="#61557c",
                               width=max(1, round(1.5 * scale)))
        oval((49, 137, 145, 153), "#27213a")

    shift = round(5 * math.sin(phase))
    points = []
    for index in range(144):
        angle = TAU * index / 144
        bend = 1 + .046 * math.cos(3 * angle + .3) + .035 * math.sin(2 * angle + phase)
        points.append(box((96 + 56 * bend * math.cos(angle), 98 + shift + 53 * bend * math.sin(angle))))
    draw.polygon(points, fill="#ba7a9c")
    mask = Image.new("L", (size, size))
    ImageDraw.Draw(mask).polygon(points, fill=255)
    fill = Image.new("RGBA", (size, size))
    colors = [(255, 227, 181), (252, 173, 155), (195, 124, 170)]
    bands = ImageDraw.Draw(fill)
    for y in range(size):
        t = min(1, max(0, (y / scale - 44 - shift) / 103)) * 2
        band = min(1, int(t))
        a, b = colors[band], colors[band + 1]
        bands.line((0, y, size, y), fill=tuple(round(x + (z - x) * (t - band)) for x, z in zip(a, b)))
    image.paste(fill, (0, 0), mask)
    oval((61, 62 + shift, 92, 71 + shift), "#ffecce")
    oval((65, 103 + shift, 79, 113 + shift), "#f58b98")
    oval((114, 103 + shift, 128, 113 + shift), "#f58b98")
    blink = phase > TAU * .88
    eye = 2 if blink else 8
    for x in (82, 111):
        oval((x - 3, 94 + shift - eye / 2, x + 3, 94 + shift + eye / 2), "#3d324b")
    outline([(92, 108 + shift), (96, 111 + shift), (100, 108 + shift)], "#613d58", 2)
    for x, y, radius in ((144, 52, 11), (50, 127, 6)):
        y += shift / 2
        star = [(x, y-radius), (x+radius/4, y-radius/4), (x+radius, y), (x+radius/4, y+radius/4),
                (x, y+radius), (x-radius/4, y+radius/4), (x-radius, y), (x-radius/4, y-radius/4)]
        draw.polygon([box(p) for p in star], fill="#fff2b2" if icon else "#ffd571")
    return image


def make_assets():
    ROOT.mkdir(parents=True, exist_ok=True)
    frames = []
    for index in range(30):
        rgba = paint(576, TAU * index / 30).resize((192, 192), Image.Resampling.LANCZOS)
        ordered = rgba.convert("RGB").quantize(colors=255)
        ordered.paste(255, mask=rgba.getchannel("A").point(lambda value: 255 if value < 127 else 0))
        frames.append(ordered)
    frames[0].save(ROOT / "Glow.gif", save_all=True, append_images=frames[1:], transparency=255,
                   duration=75, loop=0, disposal=2, optimize=False)
    paint(1024, 0, icon=True).save(ROOT / "Glow.icns")


if __name__ == "__main__":
    make_assets()
