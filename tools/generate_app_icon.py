"""Generate 1970s Vintage Racing GT application icons for Bass Fuzz Deluxe.

Renders 512x512 (ICON_BIG) and 128x128 (ICON_SMALL) cast-aluminum squircle
icons with matte olive racing green, dual cream racing stripes, burnt-orange
pinstripe, and a bold "13" racing roundel (matching the pedal faceplate).
"""

from __future__ import annotations

from pathlib import Path
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "Assets" / "icon"
ICON_512_PATH = ICON_DIR / "app_icon_512.png"
ICON_128_PATH = ICON_DIR / "app_icon_128.png"
FONTS_DIR = Path(r"C:\Windows\Fonts")


def rounded_rect_sdf(xx: np.ndarray, yy: np.ndarray, cx: float, cy: float, half: float, radius: float) -> np.ndarray:
    qx = np.abs(xx - cx) - (half - radius)
    qy = np.abs(yy - cy) - (half - radius)
    return np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0)) + np.minimum(np.maximum(qx, qy), 0.0) - radius


def render_icon(out_size: int, small_variant: bool) -> Image.Image:
    """Render a 1024x1024 master icon and downsample crisply to out_size."""
    S = 1024
    cx = cy = (S - 1) * 0.5
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float64)

    outer_half = 446.0
    outer_r = 178.0
    bezel_w = 46.0 if small_variant else 42.0
    inner_half = outer_half - bezel_w
    inner_r = outer_r - bezel_w * 0.85

    sdf_outer = rounded_rect_sdf(xx, yy, cx, cy, outer_half, outer_r)
    sdf_inner = rounded_rect_sdf(xx, yy, cx, cy, inner_half, inner_r)

    rng = np.random.default_rng(1973)
    canvas = np.zeros((S, S, 4), dtype=np.float64)

    # 1. Soft studio drop shadow outside enclosure
    sh_sdf = rounded_rect_sdf(xx - 4.0, yy - 14.0, cx, cy, outer_half, outer_r)
    shadow_a = np.clip(1.0 - sh_sdf / 46.0, 0.0, 1.0) ** 1.8 * 0.52

    # 2. Brushed cast-aluminum bezel
    brushed = ndimage.gaussian_filter(rng.normal(0.0, 1.0, size=(S, S)), sigma=(0.8, 7.0))
    light_dir = ((xx - cx) * (-0.55) + (yy - cy) * (-0.83)) / outer_half
    bevel_edge = np.exp(-((sdf_outer + 7.0) / 5.5) ** 2) * (18.0 + 18.0 * np.clip(light_dir, -1.0, 1.0))
    inner_lip_ao = np.exp(-((sdf_inner) / 4.5) ** 2) * 26.0
    alum_lum = 194.0 + 18.0 * light_dir + 5.5 * brushed + bevel_edge - inner_lip_ao
    alum_rgb = np.stack([
        np.clip(alum_lum * 1.00, 110.0, 245.0),
        np.clip(alum_lum * 1.01, 110.0, 246.0),
        np.clip(alum_lum * 1.03, 112.0, 248.0),
    ], axis=-1)

    # 3. Matte 1970s Olive / Racing Green interior
    grain = ndimage.gaussian_filter(rng.normal(0.0, 1.0, size=(S, S)), sigma=1.4) * 2.2
    face_rgb = np.empty((S, S, 3), dtype=np.float64)
    face_rgb[:, :, 0] = np.clip(43.0 + grain, 20.0, 75.0)
    face_rgb[:, :, 1] = np.clip(63.0 + grain, 30.0, 95.0)
    face_rgb[:, :, 2] = np.clip(46.0 + grain, 22.0, 78.0)

    # 4. Diagonal Dual Cream Racing Stripes + Burnt-Orange Pinstripe (matching pedal livery)
    gfx = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(gfx)
    # Diagonal stripes sweeping bottom-left to top-right
    draw.polygon([(140, 960), (245, 960), (760, 64), (655, 64)], fill=(234, 224, 200, 242))
    draw.polygon([(275, 960), (380, 960), (895, 64), (790, 64)], fill=(234, 224, 200, 242))
    draw.polygon([(404, 960), (434, 960), (949, 64), (919, 64)], fill=(200, 75, 49, 245))

    # 5. Bold, high-contrast pedal-knob + glowing green LED (instantly legible at 16px-512px)
    kcx, kcy, krad = 610, 560, 215 if small_variant else 200
    # Knob drop shadow
    draw.ellipse([kcx - krad - 14, kcy - krad - 8, kcx + krad + 22, kcy + krad + 28], fill=(14, 18, 15, 190))
    # Dark Bakelite knob body + crisp cream/silver rim
    draw.ellipse([kcx - krad, kcy - krad, kcx + krad, kcy + krad], fill=(32, 34, 36, 255), outline=(210, 214, 218, 220), width=10)
    draw.ellipse([kcx - krad + 24, kcy - krad + 24, kcx + krad - 24, kcy + krad - 24], fill=(42, 44, 48, 255))
    # Bold cream pointer line pointing up-right (2 o'clock)
    ang = math.radians(-42.0)
    px0, py0 = kcx + math.cos(ang) * (krad * 0.25), kcy + math.sin(ang) * (krad * 0.25)
    px1, py1 = kcx + math.cos(ang) * (krad * 0.88), kcy + math.sin(ang) * (krad * 0.88)
    draw.line([(px0, py0), (px1, py1)], fill=(234, 224, 200, 255), width=26 if small_variant else 24)
    # Glowing green status LED at top-left
    lx, ly, lrad = 290, 290, 56 if small_variant else 50
    draw.ellipse([lx - lrad - 18, ly - lrad - 18, lx + lrad + 18, ly + lrad + 18], fill=(40, 220, 110, 95))
    draw.ellipse([lx - lrad, ly - lrad, lx + lrad, ly + lrad], fill=(90, 255, 150, 255), outline=(22, 30, 24, 230), width=8)
    draw.ellipse([lx - lrad * 0.45, ly - lrad * 0.45, lx + lrad * 0.15, ly + lrad * 0.15], fill=(235, 255, 242, 240))

    gfx_arr = np.array(gfx, dtype=np.float64)
    ga = (gfx_arr[:, :, 3] / 255.0)[:, :, None]
    face_rgb = face_rgb * (1.0 - ga) + gfx_arr[:, :, :3] * ga

    # 6. Subtle edge weathering chips along bezel
    edge_noise = ndimage.gaussian_filter(rng.normal(0.0, 1.0, (S, S)), sigma=5.0)
    chips = ((-sdf_inner < 14.0) & (edge_noise > 0.85)).astype(np.float64)
    face_rgb = face_rgb * (1.0 - chips[:, :, None] * 0.8) + np.array([178.0, 186.0, 190.0]) * (chips[:, :, None] * 0.8)

    # Composite faceplate inside aluminum enclosure, and enclosure over shadow
    inner_mask = np.clip((-sdf_inner + 1.2) / 2.4, 0.0, 1.0)
    outer_mask = np.clip((-sdf_outer + 1.2) / 2.4, 0.0, 1.0)
    body_rgb = alum_rgb * (1.0 - inner_mask[:, :, None]) + face_rgb * inner_mask[:, :, None]

    out_a = outer_mask + shadow_a * (1.0 - outer_mask)
    safe_a = np.maximum(out_a, 1e-6)
    canvas[:, :, :3] = (body_rgb * outer_mask[:, :, None]) / safe_a[:, :, None]
    canvas[:, :, 3] = out_a * 255.0

    im = Image.fromarray(np.clip(np.round(canvas), 0, 255).astype(np.uint8), mode="RGBA")
    return im.resize((out_size, out_size), Image.Resampling.LANCZOS)


def main() -> None:
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    icon_512 = render_icon(out_size=512, small_variant=False)
    icon_128 = render_icon(out_size=128, small_variant=True)
    icon_512.save(ICON_512_PATH, optimize=True)
    icon_128.save(ICON_128_PATH, optimize=True)
    print(f"Saved {ICON_512_PATH} ({icon_512.size})")
    print(f"Saved {ICON_128_PATH} ({icon_128.size})")


if __name__ == "__main__":
    main()
