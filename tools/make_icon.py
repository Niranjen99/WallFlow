"""Builds assets/wallflow.ico from assets/wallflow-logo.png.

    python -m pip install pillow
    python tools\\make_icon.py

Large sizes (48 px and up) show the whole logo. At 16 to 32 px (tray, title bars, small
taskbar) the word "WallFlow" would be an unreadable smudge, so those sizes show only the W
wave on the same rounded square. Each size is drawn from the big picture separately (better than
letting Windows shrink one image) and stored as PNG inside the .ico, which Windows Vista and
later read.
"""
import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "wallflow-logo.png"
TARGET = ROOT / "assets" / "wallflow.ico"

# Measured on the 1254 x 1254 source: the rounded square spans x 151..1103, y 141..1096.
SQUARE = (147, 138, 1108, 1099)      # crop box, slightly outside the border glow
RADIUS = 0.19                         # corner radius, as a fraction of the square's size
TEXT_BAND = (760, 905)                # source rows holding the word "WallFlow"
LARGE_SIZES = [256, 128, 64, 48]
SMALL_SIZES = [40, 32, 24, 20, 16]


def rounded(image):
    """Makes everything outside the rounded square transparent."""
    size = image.size[0]
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=int(size * RADIUS), fill=255)
    out = image.convert("RGBA")
    out.putalpha(mask)
    return out


def without_text(source):
    """The source with the word painted over by the square's own background colours: each
    column of the text band is filled with the colour just above it."""
    image = source.copy()
    pixels = image.load()
    top, bottom = TEXT_BAND
    for x in range(SQUARE[0] + 40, SQUARE[2] - 40):
        above = pixels[x, top - 12]
        for y in range(top, bottom):
            pixels[x, y] = above
    return image


def centred_wave(square):
    """For small sizes: the W (with the background around it) cut from the square, enlarged
    and pasted back over the centre, so it fills most of the icon. The square's border glow is
    kept as it is."""
    size = square.size[0]
    # The W with some background around it, in the cropped square's coordinates.
    box = (int(size * 0.07), int(size * 0.25), int(size * 0.93), int(size * 0.60))
    wave = square.crop(box)
    scale = 1.12
    wave = wave.resize((int(wave.width * scale), int(wave.height * scale)), Image.LANCZOS)
    # Soften the pasted patch's edges so no seam shows against the background.
    fade = Image.new("L", wave.size, 0)
    margin = int(size * 0.05)
    ImageDraw.Draw(fade).rounded_rectangle((margin, margin, wave.width - margin, wave.height - margin),
                                           radius=margin, fill=255)
    fade = fade.filter(ImageFilter.GaussianBlur(margin / 2))
    out = square.copy()
    out.paste(wave, ((size - wave.width) // 2, (size - wave.height) // 2), fade)
    return rounded(out.convert("RGB"))


def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return buffer.getvalue()


def main():
    source = Image.open(SOURCE).convert("RGB")
    large = rounded(source.crop(SQUARE))
    small = centred_wave(without_text(source).crop(SQUARE))

    entries = []
    for size in LARGE_SIZES + SMALL_SIZES:
        base = large if size in LARGE_SIZES else small
        entries.append((size, png_bytes(base.resize((size, size), Image.LANCZOS))))

    # ICO file: a 6-byte header, one 16-byte directory entry per image, then the images.
    header = struct.pack("<HHH", 0, 1, len(entries))  # reserved, type 1 = icon, count
    offset = len(header) + 16 * len(entries)
    directory = b""
    data = b""
    for size, png in entries:
        # Width and height are stored in one byte each; 0 means 256.
        side = 0 if size == 256 else size
        directory += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(png), offset)
        data += png
        offset += len(png)
    TARGET.write_bytes(header + directory + data)
    print(f"wrote {TARGET} ({TARGET.stat().st_size} bytes, sizes {[s for s, _ in entries]})")

    # Previews for checking by eye (not committed).
    preview = Image.new("RGBA", (16 + 32 + 48 + 256 + 50, 256), (128, 128, 128, 255))
    x = 10
    for size, base in ((16, small), (32, small), (48, large), (256, large)):
        preview.alpha_composite(base.resize((size, size), Image.LANCZOS), (x, 0))
        x += size + 10
    return preview


if __name__ == "__main__":
    main()
