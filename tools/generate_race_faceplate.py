"""Generate the 1970s weathered Vintage Racing GT faceplate & weathered knobs for Bass Fuzz Deluxe.

Uses a physically accurate multi-layer paint pipeline:
1. Seamless matte olive-green enamel base plate (Assets/source/olive_base_plate.png)
2. Painted diagonal cream + burnt-orange racing stripes modulated by base grain
3. Directional fibrous scuff/abrasion masks (Assets/source/paint_scuff_masks.png)
   that ONLY erase stripe paint + connected-component pruning of stray scraps
4. Grounded contact AO + directional cast shadows under both chrome footswitches
5. Authentic silkscreened typography (no heavy black outlines, no label wells,
   no 13 roundel, no lines/tagline under DELUXE BASS FUZZ)
6. Weathered vintage molded Bakelite/ABS knobs (knob_cap, knob_lighting, knob_skirt_strip)
"""

from __future__ import annotations

import io
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
UI_DIR = ROOT / "Assets" / "ui"
OLIVE_BASE_PATH = ROOT / "Assets" / "source" / "olive_base_plate.png"
SCUFF_MASKS_PATH = ROOT / "Assets" / "source" / "paint_scuff_masks.png"
CHASSIS_PATH = UI_DIR / "pedal_chassis.png"
FS_LEFT_PATH = UI_DIR / "footswitch_left_pressed.png"
FS_RIGHT_PATH = UI_DIR / "footswitch_right_pressed.png"
SCRATCH_DIR = Path(r"C:\Users\sonic\.gemini\antigravity\brain\a8314f55-065d-4ef1-b9c3-023d33c45ce1\scratch")
ASSEMBLED_PREVIEW_PATH = SCRATCH_DIR / "assembled_preview.png"
QA_CROPS_PATH = SCRATCH_DIR / "qa_crops.png"

FONTS_DIR = Path(r"C:\Windows\Fonts")

W, H = 1720, 1436
FACE_L, FACE_R = 152, 1568
FACE_T, FACE_B = 110, 1256
FACE_RADIUS = 38

FS_LEFT = (int(round(0.220930 * W)), int(round(0.768802 * H)))   # (380, 1104)
FS_RIGHT = (int(round(0.770930 * W)), int(round(0.767409 * H)))  # (1326, 1102)

COL_STRIPE_CREAM = np.array([234.0, 224.0, 200.0])  # #EAE0C8
COL_STRIPE_ORANGE = np.array([200.0, 75.0, 49.0])   # #C84B31
COL_LOGO_CREAM = (239, 231, 212)                    # #EFE7D4
COL_LBL = (244, 239, 224, 255)                      # #F4EFE0
COL_ACCENT_LBL = (242, 118, 61, 255)                # #F2763D


def load_git_head_image(rel_path: str, mode: str = "RGB") -> Image.Image:
    data = subprocess.check_output(["git", "show", f"HEAD:{rel_path}"], cwd=ROOT)
    return Image.open(io.BytesIO(data)).convert(mode)


def load_font(name: str, size: int, weight: int | None = None) -> ImageFont.FreeTypeFont:
    for base in (ROOT / "Assets" / "fonts", FONTS_DIR):
        p = base / name
        if p.exists():
            font = ImageFont.truetype(str(p), size)
            if weight is not None:
                try:
                    font.set_variation_by_axes([weight, 100])
                except Exception:
                    pass
            return font
    return ImageFont.load_default()


def rounded_rect_sdf(xx: np.ndarray, yy: np.ndarray, l: float, t: float, r: float, b: float, rad: float) -> np.ndarray:
    cx, cy = (l + r) * 0.5, (t + b) * 0.5
    hx, hy = (r - l) * 0.5 - rad, (b - t) * 0.5 - rad
    qx = np.abs(xx - cx) - hx
    qy = np.abs(yy - cy) - hy
    return np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0)) + np.minimum(np.maximum(qx, qy), 0.0) - rad


def extract_scuff_masks() -> list[Image.Image]:
    """Extract 4 directional fibrous scuff masks (black on white) with stray specks pruned."""
    sheet = np.array(Image.open(SCUFF_MASKS_PATH).convert("L"), dtype=np.float64)
    sh, sw = sheet.shape
    mh, mw = sh // 2, sw // 2
    quads = [sheet[0:mh, 0:mw], sheet[0:mh, mw:sw], sheet[mh:sh, 0:mw], sheet[mh:sh, mw:sw]]

    masks: list[Image.Image] = []
    for q in quads:
        dark = 1.0 - q / 255.0
        alpha = np.clip((dark - 0.16) * 2.4, 0.0, 1.0)
        # Prune tiny isolated 1-2px specks outside main scuff
        lbl, n_lbl = ndimage.label(ndimage.binary_dilation(alpha > 0.25, iterations=3))
        if n_lbl > 1:
            sizes = np.bincount(lbl.ravel())
            sizes[0] = 0
            keep = lbl == int(np.argmax(sizes))
            alpha *= ndimage.binary_dilation(keep, iterations=2).astype(np.float64)

        ys, xs = np.where(alpha > 0.03)
        y0, y1 = max(0, int(ys.min()) - 2), min(q.shape[0], int(ys.max()) + 3)
        x0, x1 = max(0, int(xs.min()) - 2), min(q.shape[1], int(xs.max()) + 3)
        masks.append(Image.fromarray(np.clip(alpha[y0:y1, x0:x1] * 255.0, 0, 255).astype(np.uint8), mode="L"))
    return masks


def prune_small_islands(mask: np.ndarray, min_area: int) -> np.ndarray:
    """Remove disconnected tiny leftover paint scraps below min_area px^2."""
    lbl, n_lbl = ndimage.label(mask > 0.25)
    if n_lbl <= 1:
        return mask
    sizes = np.bincount(lbl.ravel())
    keep = sizes >= min_area
    keep[0] = False
    return mask * keep[lbl].astype(np.float64)


def draw_silkscreen_text(
    draw: ImageDraw.ImageDraw,
    xy: tuple[float, float],
    text: str,
    font: ImageFont.FreeTypeFont,
    fill: tuple[int, int, int, int],
    tracking: float = 0.0,
    anchor_x: str = "center",
    shadow_offset: tuple[int, int] = (1, 1),
    shadow_alpha: int = 120,
) -> None:
    widths = [draw.textlength(ch, font=font) for ch in text]
    total_w = sum(widths) + tracking * max(0, len(text) - 1)
    x = xy[0] - total_w * 0.5 if anchor_x == "center" else (xy[0] - total_w if anchor_x == "right" else xy[0])
    y = xy[1]
    sx, sy = shadow_offset
    cx = x
    for i, ch in enumerate(text):
        if shadow_alpha > 0 and (sx != 0 or sy != 0):
            draw.text((cx + sx, y + sy), ch, font=font, fill=(12, 16, 13, shadow_alpha))
        draw.text((cx, y), ch, font=font, fill=fill)
        cx += widths[i] + tracking


def draw_9v_icon(draw: ImageDraw.ImageDraw, cx: int, cy: int, col: tuple[int, int, int, int]) -> None:
    font_sm = load_font("arialbd.ttf", 22)
    draw.text((cx - 46, cy - 13), "9V", font=font_sm, fill=col)
    draw.arc([cx - 10, cy - 12, cx + 14, cy + 12], start=35, end=325, fill=col, width=3)
    draw.ellipse([cx - 1, cy - 3, cx + 5, cy + 3], fill=col)
    draw.line([(cx - 18, cy), (cx - 10, cy)], fill=col, width=3)
    draw.line([(cx + 14, cy), (cx + 26, cy)], fill=col, width=3)
    draw.line([(cx + 20, cy - 6), (cx + 20, cy + 6)], fill=col, width=3)


def generate_weathered_knobs() -> None:
    """Weather knob_cap, knob_lighting, and knob_skirt_strip from pristine git HEAD originals."""
    rng = np.random.default_rng(1974)

    # 1. knob_cap.png: vintage molded Bakelite/ABS grain + recessed aged-ivory indicator
    cap_img = load_git_head_image("Assets/ui/knob_cap.png", mode="RGBA")
    cap = np.array(cap_img, dtype=np.float64)
    ch, cw, _ = cap.shape
    cy, cx = (ch - 1) * 0.5, (cw - 1) * 0.5
    yy, xx = np.mgrid[0:ch, 0:cw].astype(np.float64)
    r = np.hypot(xx - cx, yy - cy)
    alpha = cap[:, :, 3] / 255.0

    # Identify pointer arrow (brighter/greyish pixels on top) vs cap body
    lum = cap[:, :, :3].mean(axis=2)
    is_pointer = (alpha > 0.3) & (lum > 90.0)
    is_body = (alpha > 0.3) & (~is_pointer)

    # Machined concentric lathe-turned brushed-silver aluminum face
    pebble = ndimage.gaussian_filter(rng.normal(0.0, 1.0, (ch, cw)), sigma=0.6) * 2.4
    lathe = np.sin(r * 1.45) * 3.8
    chamfer = np.exp(-((r - 92.0) / 4.0) ** 2) * 14.0
    silver = np.clip(188.0 + pebble + lathe + chamfer, 130.0, 238.0)
    for c, mult in enumerate((0.99, 1.00, 1.02)):
        cap[:, :, c] = np.where(is_body, np.clip(silver * mult, 120.0, 240.0), cap[:, :, c])

    # Crisp recessed dark-gunmetal indicator notch
    ind_rgb = np.array([34.0, 42.0, 52.0])
    ptr_erode = ndimage.binary_erosion(is_pointer, iterations=1)
    ptr_rim = is_pointer & (~ptr_erode)
    for c in range(3):
        val = np.where(ptr_rim, ind_rgb[c] * 0.72, ind_rgb[c])
        cap[:, :, c] = np.where(is_pointer, val, cap[:, :, c])

    Image.fromarray(np.clip(cap, 0, 255).astype(np.uint8), mode="RGBA").save(UI_DIR / "knob_cap.png", optimize=True)

    # 2. knob_lighting.png: high-contrast stationary anisotropic X-sheen (shiny machined aluminum)
    light_img = load_git_head_image("Assets/ui/knob_lighting.png", mode="RGBA")
    lh, lw = light_img.height, light_img.width
    lyy, lxx = np.mgrid[0:lh, 0:lw].astype(np.float64)
    ldx, ldy = lxx - (lw - 1) * 0.5, lyy - (lh - 1) * 0.5
    lr = np.hypot(ldx, ldy)
    lR = lw * 0.37
    inside = np.clip((0.97 * lR - lr) / (0.02 * lR), 0.0, 1.0)
    theta = np.arctan2(ldy, ldx)
    aniso = np.cos(2.0 * (theta + math.radians(38.0)))
    tl_bias = (ldx * (-0.707) + ldy * (-0.707)) / max(lR, 1.0)
    light = np.zeros((lh, lw, 4), dtype=np.float64)
    bright_a = np.clip(aniso, 0.0, 1.0) ** 1.2 * 0.42 + np.clip(tl_bias, 0.0, 1.0) * 0.14
    dark_a = np.clip(-aniso, 0.0, 1.0) ** 1.2 * 0.24 + np.clip(-tl_bias, 0.0, 1.0) * 0.10
    tot_a = np.clip(bright_a + dark_a * (1.0 - bright_a), 0.0, 1.0)
    w_frac = bright_a / np.maximum(tot_a, 1e-6)
    for c, v in enumerate([252.0, 253.0, 255.0]):
        light[:, :, c] = v * w_frac
    light[:, :, 3] = tot_a * inside * 255.0
    Image.fromarray(np.clip(light, 0, 255).astype(np.uint8), mode="RGBA").save(UI_DIR / "knob_lighting.png", optimize=True)

    # 3. knob_skirt_strip.png & knob_wall_strip.png: smooth polished-chrome bevel & wall (no knurl teeth)
    for fname, is_wall in (("knob_skirt_strip.png", False), ("knob_wall_strip.png", True)):
        strip_img = load_git_head_image(f"Assets/ui/{fname}", mode="RGBA")
        sh, sw = strip_img.height, strip_img.width
        frame_sz = sw
        n_frames = max(1, sh // frame_sz)
        fyy, fxx = np.mgrid[0:frame_sz, 0:frame_sz].astype(np.float64)
        fdx, fdy = fxx - (frame_sz - 1) * 0.5, fyy - (frame_sz - 1) * 0.5
        fr = np.hypot(fdx, fdy)
        fR = frame_sz * 0.37
        ftheta = np.arctan2(fdy, fdx)
        # Smooth outer rim & inner bevel transition
        outer_a = np.clip((1.00 * fR - fr) / (0.018 * fR), 0.0, 1.0)
        inner_a = np.clip((fr - 0.80 * fR) / (0.018 * fR), 0.0, 1.0)
        bevel_a = outer_a * inner_a
        # Polished chrome bevel lighting: bright top-left highlight + bottom-right bounce
        tl = np.cos(ftheta + math.radians(135.0))
        bevel_lum = 158.0 + 68.0 * np.clip(tl, 0.0, 1.0) + 28.0 * np.clip(-tl, 0.0, 1.0) - 32.0 * (1.0 - np.abs(tl))
        if is_wall:
            bevel_lum *= 0.85
        frame = np.zeros((frame_sz, frame_sz, 4), dtype=np.float64)
        frame[:, :, 0] = np.clip(bevel_lum * 0.99, 60.0, 250.0)
        frame[:, :, 1] = np.clip(bevel_lum * 1.00, 60.0, 252.0)
        frame[:, :, 2] = np.clip(bevel_lum * 1.02, 62.0, 255.0)
        frame[:, :, 3] = bevel_a * 255.0
        tile = np.tile(np.clip(frame, 0, 255).astype(np.uint8), (n_frames, 1, 1))
        Image.fromarray(tile, mode="RGBA").save(UI_DIR / fname, optimize=True)
    print("Updated machined silver knobs -> knob_cap.png, knob_lighting.png, knob_skirt_strip.png, knob_wall_strip.png")


def composite_studio_cables(bg_rgb: np.ndarray) -> np.ndarray:
    """Composite true-to-scale photorealistic 1/4-inch plug sleeves and 9V DC plug seated flush."""
    H_img, W_img, _ = bg_rgb.shape
    res = bg_rgb.copy()

    def extract_plug(path: Path) -> tuple[np.ndarray, np.ndarray]:
        arr = np.array(Image.open(path).convert("RGB"), dtype=np.float64)
        lum = arr.mean(axis=2)
        chroma = arr.max(axis=2) - arr.min(axis=2)
        fg = ndimage.binary_fill_holes((lum < 228.0) | (chroma > 16.0))
        # Keep largest connected component (the plug itself)
        lbl, n = ndimage.label(fg)
        if n > 1:
            sizes = np.bincount(lbl.ravel())
            sizes[0] = 0
            fg = lbl == int(np.argmax(sizes))
        ys, xs = np.where(fg)
        y0, y1 = int(ys.min()), int(ys.max()) + 1
        x0, x1 = int(xs.min()), int(xs.max()) + 1
        alpha = ndimage.gaussian_filter(fg[y0:y1, x0:x1].astype(np.float64), sigma=0.8)
        return arr[y0:y1, x0:x1], alpha

    def paste_with_shadow(plug_rgb: np.ndarray, plug_a: np.ndarray, x0: int, y0: int) -> None:
        nonlocal res
        ph, pw = plug_a.shape
        # Clip to canvas
        sx0, sx1 = max(0, -x0), min(pw, W_img - x0)
        sy0, sy1 = max(0, -y0), min(ph, H_img - y0)
        if sx1 <= sx0 or sy1 <= sy0:
            return
        dx0, dx1 = x0 + sx0, x0 + sx1
        dy0, dy1 = y0 + sy0, y0 + sy1

        pa = plug_a[sy0:sy1, sx0:sx1]
        pr = plug_rgb[sy0:sy1, sx0:sx1]

        # Soft directional studio shadow on wood (offset +8, +10)
        sh_a = ndimage.gaussian_filter(pa, sigma=6.0) * 0.52
        sh_y0, sh_y1 = min(H_img, dy0 + 10), min(H_img, dy1 + 10)
        sh_x0, sh_x1 = min(W_img, dx0 + 8), min(W_img, dx1 + 8)
        sh_h, sh_w = sh_y1 - sh_y0, sh_x1 - sh_x0
        if sh_h > 0 and sh_w > 0:
            res[sh_y0:sh_y1, sh_x0:sh_x1] *= (1.0 - sh_a[:sh_h, :sh_w, None])

        res[dy0:dy1, dx0:dx1] = res[dy0:dy1, dx0:dx1] * (1.0 - pa[:, :, None]) + pr * pa[:, :, None]

    # 1. 1/4" TS Plugs (crop off the inserted tip: keep housing from 35% width to 100% width)
    jack_path = ROOT / "Assets" / "source" / "jack_plug.jpg"
    if jack_path.exists():
        j_rgb, j_a = extract_plug(jack_path)
        j_start = int(j_rgb.shape[1] * 0.355)
        j_rgb, j_a = j_rgb[:, j_start:], j_a[:, j_start:]
        # Scale housing to ~54px tall (true scale relative to ~113px jack nut)
        target_h = 54
        scale = target_h / j_rgb.shape[0]
        target_w = max(1, int(round(j_rgb.shape[1] * scale)))
        j_rgb_s = np.array(Image.fromarray(j_rgb.astype(np.uint8)).resize((target_w, target_h), Image.Resampling.LANCZOS), dtype=np.float64)
        j_a_s = np.array(Image.fromarray((j_a * 255).astype(np.uint8)).resize((target_w, target_h), Image.Resampling.LANCZOS), dtype=np.float64) / 255.0

        # Right INPUT jack (x=1588, y=866)
        paste_with_shadow(j_rgb_s, j_a_s, 1588, 866 - target_h // 2)
        # Left OUTPUT jack (flipped horizontally, seated flush at x=132, y=866)
        paste_with_shadow(j_rgb_s[:, ::-1], j_a_s[:, ::-1], 132 - target_w, 866 - target_h // 2)

    # 2. 9V DC Barrel Plug (crop off the inserted metal tip: keep molded black housing from 0% to 78% height)
    dc_path = ROOT / "Assets" / "source" / "dc_plug.jpg"
    if dc_path.exists():
        d_rgb, d_a = extract_plug(dc_path)
        d_end = int(d_rgb.shape[0] * 0.77)
        d_rgb, d_a = d_rgb[:d_end, :], d_a[:d_end, :]
        # Scale molded housing to ~34px wide, seated flush at y=88
        target_w = 34
        scale = target_w / d_rgb.shape[1]
        target_h = max(1, int(round(d_rgb.shape[0] * scale)))
        d_rgb_s = np.array(Image.fromarray(d_rgb.astype(np.uint8)).resize((target_w, target_h), Image.Resampling.LANCZOS), dtype=np.float64)
        d_a_s = np.array(Image.fromarray((d_a * 255).astype(np.uint8)).resize((target_w, target_h), Image.Resampling.LANCZOS), dtype=np.float64) / 255.0
        paste_with_shadow(d_rgb_s, d_a_s, 860 - target_w // 2, 88 - target_h)

    return res


def generate_faceplate() -> None:
    orig = np.array(load_git_head_image("Assets/ui/pedal_chassis.png"), dtype=np.float64)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)

    sdf_face = rounded_rect_sdf(xx, yy, FACE_L, FACE_T, FACE_R, FACE_B, FACE_RADIUS)
    face_alpha = np.clip(0.5 - sdf_face, 0.0, 1.0)

    # Layer 1: Seamless matte olive enamel base plate
    tex_w, tex_h = FACE_R - FACE_L, FACE_B - FACE_T
    olive_img = Image.open(OLIVE_BASE_PATH).convert("RGB").resize((tex_w, tex_h), Image.Resampling.LANCZOS)
    olive_full = np.zeros((H, W, 3), dtype=np.float64)
    olive_full[FACE_T:FACE_B, FACE_L:FACE_R] = np.array(olive_img, dtype=np.float64)

    olive_smooth = ndimage.gaussian_filter(olive_full, sigma=(28.0, 28.0, 0.0))
    grain_ratio = np.clip(olive_full / np.maximum(olive_smooth, 1.0), 0.80, 1.22)

    # Layer 2: Painted diagonal racing stripes + directional fibrous scuff masks
    ang = math.radians(34.0)
    u = (xx - 775.0) * math.cos(ang) + (yy - 700.0) * math.sin(ang)
    s1 = np.clip((54.0 - np.abs(u + 125.0)) * 0.9, 0.0, 1.0)
    s2 = np.clip((54.0 - np.abs(u - 10.0)) * 0.9, 0.0, 1.0)
    pin = np.clip((13.0 - np.abs(u - 92.0)) * 0.9, 0.0, 1.0)

    scuffs = extract_scuff_masks()
    wear_canvas = Image.new("L", (W, H), 0)
    # (scuff_idx, cx, cy, scale_x, scale_y, angle_deg, flip_x)
    placements = [
        # Wide directional scuff behind DELUXE BASS FUZZ with generous clearance under X and E
        (0, 625, 724, 1.72, 1.15, -3.0, False),
        (1, 885, 718, 0.72, 0.62, 16.0, False),
        # Directional scuffs clearing TONE & SUSTAIN on stripes
        (1, 738, 480, 0.56, 0.46, 18.0, True),
        (1, 971, 480, 0.66, 0.54, -12.0, False),
        # Compact scuff clearing green CROSSOVER LED (650, 967)
        (2, 650, 967, 0.50, 0.44, 14.0, False),
        # Rugged boot-drag scuff clearing bottom-left CROSSOVER footswitch & label
        (3, 380, 1155, 1.22, 1.05, 4.0, False),
        # Extra wear at top-left of left footswitch so it doesn't sit on the stripe
        (2, 326, 1048, 0.56, 0.48, -14.0, False),
        # Natural accent scuff on upper-right stripe
        (2, 1215, 255, 0.46, 0.40, 10.0, True),
    ]
    for idx, cx, cy, sx, sy, rot_ang, flip_x in placements:
        sp = scuffs[idx]
        if flip_x:
            sp = sp.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        sp = sp.resize((max(1, int(round(sp.width * sx))), max(1, int(round(sp.height * sy)))), Image.Resampling.LANCZOS)
        if abs(rot_ang) > 0.1:
            sp = sp.rotate(rot_ang, resample=Image.Resampling.BICUBIC, expand=True)
        wear_canvas.paste(sp, (int(round(cx - sp.width * 0.5)), int(round(cy - sp.height * 0.5))), sp)

    wear_mask = np.clip((np.array(wear_canvas, dtype=np.float64) / 255.0 - 0.35) * 4.5, 0.0, 1.0)
    # Ensure left footswitch never touches the stripe & remove tiny bottom-left stripe sliver
    d_fs_clear = np.hypot(xx - 355.0, yy - 1072.0)
    wear_mask = np.maximum(wear_mask, np.clip((66.0 - d_fs_clear) / 8.0, 0.0, 1.0))
    wear_mask = np.where((xx < 350.0) & (yy > 1205.0), 1.0, wear_mask)

    s1_w = s1 * (1.0 - wear_mask)
    s2_w = s2 * (1.0 - wear_mask)
    pin_w = pin * (1.0 - wear_mask)

    # Prune stray floating paint scraps
    cream_a = prune_small_islands(np.clip(s1_w + s2_w, 0.0, 1.0), min_area=450)
    pin_w = prune_small_islands(pin_w, min_area=350)
    stripe_a = np.clip(cream_a + pin_w, 0.0, 1.0)

    stripe_rgb = (
        COL_STRIPE_CREAM[None, None, :] * cream_a[:, :, None]
        + COL_STRIPE_ORANGE[None, None, :] * pin_w[:, :, None]
    ) * grain_ratio

    painted_rgb = olive_full * (1.0 - stripe_a[:, :, None]) + stripe_rgb

    bezel_ao = np.exp(-np.maximum(-sdf_face, 0.0) / 10.0) * 0.34
    painted_rgb *= (1.0 - bezel_ao[:, :, None])

    # Layer 3: Authentic silkscreened enamel typography
    rng = np.random.default_rng(1973)
    hero_gfx = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    hdraw = ImageDraw.Draw(hero_gfx)
    font_logo = load_font("impact.ttf", 104)
    draw_silkscreen_text(
        hdraw, (635, 666), "DELUXE BASS FUZZ", font_logo, (*COL_LOGO_CREAM, 245),
        tracking=3.0, shadow_offset=(2, 2), shadow_alpha=125,
    )

    hero_chips = ndimage.gaussian_filter(rng.random((H, W)), sigma=1.1) > 0.665
    hero_arr = np.array(hero_gfx, dtype=np.float64)
    hero_a = (hero_arr[:, :, 3] / 255.0) * (1.0 - hero_chips.astype(np.float64) * 0.65)
    hero_rgb = np.clip(hero_arr[:, :, :3] * grain_ratio, 0.0, 255.0)
    painted_rgb = painted_rgb * (1.0 - hero_a[:, :, None]) + hero_rgb * hero_a[:, :, None]

    gfx = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(gfx)
    font_lbl = load_font("Michroma-Regular.ttf", 19)
    for kx, lbl, col in [
        (0.156395 * W, "VOLUME", COL_LBL),
        (0.293895 * W, "BLEND", COL_LBL),
        (0.429070 * W, "TONE", COL_LBL),
        (0.564826 * W, "SUSTAIN", COL_LBL),
        (0.698837 * W, "GATE", COL_LBL),
        (0.836337 * W, "CROSSOVER", COL_ACCENT_LBL),
    ]:
        draw_silkscreen_text(draw, (kx, 468), lbl, font_lbl, col, tracking=2.2)

    font_xover = load_font("Michroma-Regular.ttf", 19)
    draw_silkscreen_text(draw, (1439, 152), "HPF", font_xover, COL_LBL, tracking=3.0)
    draw_silkscreen_text(draw, (1439, 716), "LPF", font_xover, COL_LBL, tracking=3.0)

    font_pad = load_font("Michroma-Regular.ttf", 17)
    draw_silkscreen_text(draw, (1145, 526), "-10 dB", font_pad, COL_LBL, tracking=1.0)
    draw_silkscreen_text(draw, (1105, 583), "INPUT", font_pad, COL_ACCENT_LBL, tracking=1.8)
    draw_silkscreen_text(draw, (1148, 638), "0 dB", font_pad, COL_LBL, tracking=1.0)

    draw_silkscreen_text(draw, (FS_LEFT[0], 1196), "CROSSOVER", font_lbl, COL_LBL, tracking=2.2)
    draw_silkscreen_text(draw, (FS_RIGHT[0], 1196), "BYPASS", font_lbl, COL_LBL, tracking=2.2)

    font_maker = load_font("Michroma-Regular.ttf", 21)
    draw_silkscreen_text(draw, (853, 1102), "DISTANT NEBULA", font_maker, COL_LBL, tracking=5.5)

    draw_9v_icon(draw, 860, 148, COL_LBL)

    font_jack = load_font("Michroma-Regular.ttf", 14)
    for text, x, y in [
        ("DI OUTPUT", 174, 628),
        ("OUTPUT", 174, 866),
        ("DIRECT OUT", 1528, 628),
        ("INPUT", 1528, 866),
    ]:
        tmp = Image.new("RGBA", (180, 40), (0, 0, 0, 0))
        td = ImageDraw.Draw(tmp)
        bbox = td.textbbox((0, 0), text, font=font_jack)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        tx = 90.0 - tw * 0.5 - bbox[0]
        ty = 20.0 - th * 0.5 - bbox[1]
        td.text((tx + 1, ty + 1), text, font=font_jack, fill=(12, 16, 13, 120))
        td.text((tx, ty), text, font=font_jack, fill=COL_LBL)
        gfx.alpha_composite(tmp.rotate(90, expand=True), (x, y - 90))

    gfx_arr = np.array(gfx, dtype=np.float64)
    gfx_a = (gfx_arr[:, :, 3] / 255.0)[:, :, None]
    painted_rgb = painted_rgb * (1.0 - gfx_a) + gfx_arr[:, :, :3] * gfx_a

    # Ground both chrome footswitches with tight contact AO ring + two-stage directional cast shadow (top-left key light)
    for fx, fy in (FS_LEFT, FS_RIGHT):
        d_base = np.hypot(xx - fx, yy - fy)
        # Tight, deep crevice AO right where the washer/hex nut meets the painted faceplate
        contact_ao = np.exp(-((d_base - 56.5) / 3.8) ** 2) * 0.82
        # Inner under-nut shadow
        under_ao = np.clip((60.0 - d_base) / 6.0, 0.0, 1.0) * 0.65
        # Crisp-ish umbra cast shadow toward bottom-right
        d_umbra = np.hypot((xx - (fx + 12.0)) / 1.04, (yy - (fy + 16.0)) / 1.08)
        umbra = ndimage.gaussian_filter(np.clip((58.0 - d_umbra) / 8.0, 0.0, 1.0), sigma=5.5) * 0.56
        # Softer, wider penumbra cast shadow
        d_penumbra = np.hypot((xx - (fx + 20.0)) / 1.08, (yy - (fy + 26.0)) / 1.12)
        penumbra = ndimage.gaussian_filter(np.clip((62.0 - d_penumbra) / 16.0, 0.0, 1.0), sigma=13.0) * 0.34
        total_sh = np.clip(np.maximum(contact_ao, under_ao) + umbra * 0.72 + penumbra * 0.45, 0.0, 0.86)
        painted_rgb *= (1.0 - total_sh[:, :, None])

    # Preserve pristine 3D chrome footswitches (slightly tighter mask to eliminate bright cutout fringe)
    fs_mask = np.zeros((H, W), dtype=np.float64)
    lum, chroma = orig.mean(axis=2), orig.max(axis=2) - orig.min(axis=2)
    for fx, fy in (FS_LEFT, FS_RIGHT):
        d = np.hypot(xx - fx, yy - fy)
        chrome = ndimage.binary_fill_holes((d < 66.0) & ((lum > 95.0) | (d < 52.0)) & (chroma < 45.0))
        chrome_eroded = ndimage.binary_erosion(chrome, iterations=1)
        fs_mask = np.maximum(fs_mask, ndimage.gaussian_filter(chrome_eroded.astype(np.float64), sigma=0.9))

    final_face_mask = np.clip(face_alpha * (1.0 - fs_mask), 0.0, 1.0)[:, :, None]
    out_rgb = orig * (1.0 - final_face_mask) + painted_rgb * final_face_mask
    out_img = Image.fromarray(np.clip(out_rgb, 0, 255).astype(np.uint8), mode="RGB")
    out_img.save(CHASSIS_PATH, optimize=True)
    print(f"Saved new faceplate -> {CHASSIS_PATH}")

    for fs_path, git_rel, (fx, fy) in (
        (FS_LEFT_PATH, "Assets/ui/footswitch_left_pressed.png", FS_LEFT),
        (FS_RIGHT_PATH, "Assets/ui/footswitch_right_pressed.png", FS_RIGHT),
    ):
        old_patch = np.array(load_git_head_image(git_rel), dtype=np.float64)
        ph, pw, _ = old_patch.shape
        x0, y0 = fx - pw // 2, fy - ph // 2
        new_bg = out_rgb[y0:y0 + ph, x0:x0 + pw]
        pyy, pxx = np.mgrid[0:ph, 0:pw].astype(np.float64)
        keep = np.clip((64.0 - np.hypot(pxx - pw * 0.5, pyy - ph * 0.5)) / 8.0, 0.0, 1.0)[:, :, None]
        Image.fromarray(np.clip(new_bg * (1.0 - keep) + old_patch * keep, 0, 255).astype(np.uint8), mode="RGB").save(fs_path, optimize=True)

    render_qa_outputs(out_img)


def render_qa_outputs(chassis_img: Image.Image) -> None:
    canvas = chassis_img.convert("RGBA")

    def paste_centered(sprite: Image.Image, cx_f: float, cy_f: float, w_f: float, h_f: float) -> None:
        bw, bh = int(round(w_f * W)), int(round(h_f * H))
        canvas.alpha_composite(sprite.resize((bw, bh), Image.Resampling.LANCZOS), (int(round(cx_f * W - bw * 0.5)), int(round(cy_f * H - bh * 0.5))))

    k_sh = Image.open(UI_DIR / "knob_shadow.png").convert("RGBA")
    k_sk = Image.open(UI_DIR / "knob_skirt_strip.png").convert("RGBA")
    k_cp = Image.open(UI_DIR / "knob_cap.png").convert("RGBA")
    k_lt = Image.open(UI_DIR / "knob_lighting.png").convert("RGBA")
    kw = k_sk.width
    knob_comp = Image.alpha_composite(Image.alpha_composite(Image.alpha_composite(k_sh, k_sk.crop((0, 0, kw, kw))), k_cp), k_lt)

    for cx_f, cy_f in [(0.156395, 0.240251), (0.293895, 0.240251), (0.429070, 0.240251), (0.564826, 0.240251), (0.698837, 0.240251), (0.836337, 0.240251), (0.836628, 0.417827)]:
        paste_centered(knob_comp, cx_f, cy_f, 0.139535, 0.167131)

    paste_centered(Image.open(UI_DIR / "toggle_up.png").convert("RGBA"), 0.702035, 0.415042, 0.069767, 0.104457)
    paste_centered(Image.open(UI_DIR / "led_green_on.png").convert("RGBA"), 0.378141, 0.673408, 0.111628, 0.133705)
    paste_centered(Image.open(UI_DIR / "led_red_on.png").convert("RGBA"), 0.615613, 0.670301, 0.111628, 0.133705)

    SCRATCH_DIR.mkdir(parents=True, exist_ok=True)
    assembled = canvas.convert("RGB")
    assembled.save(ASSEMBLED_PREVIEW_PATH, optimize=True)

    cw, ch = 640, 420
    sheet = Image.new("RGB", (cw * 2, ch * 2), (20, 20, 20))
    crops = [
        (160, 540),   # (a) DELUXE BASS FUZZ + X/E clearance
        (600, 180),   # (b) Weathered knobs + TONE/SUSTAIN scuffs
        (180, 920),   # (c) Grounded CROSSOVER footswitch + boot wear
        (1010, 900),  # (d) Grounded BYPASS footswitch + right side
    ]
    for i, (x0, y0) in enumerate(crops):
        sheet.paste(assembled.crop((x0, y0, x0 + cw, y0 + ch)), ((i % 2) * cw, (i // 2) * ch))
    sheet.save(QA_CROPS_PATH, optimize=True)
    print(f"Saved QA outputs -> {ASSEMBLED_PREVIEW_PATH}, {QA_CROPS_PATH}")


if __name__ == "__main__":
    generate_faceplate()
