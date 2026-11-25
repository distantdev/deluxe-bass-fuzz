"""Reproducible asset generator for the photorealistic Deluxe Bass Fuzz UI.

Loads the full-resolution orthographic pedal photo, extracts exact hardware
coordinates, generates clean-plated chassis artwork and physically shaded
multi-layer interactive hardware sprites under Assets/ui/, writes
Source/ui/PedalLayout.h, and outputs a visual QA sheet.
"""

from __future__ import annotations

import argparse
import math
import os
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
SOURCE_IMAGE_PATH = ROOT / "Assets" / "source" / "deluxe_bass_fuzz_top.jpg"
DEFAULT_BG_PATH = ROOT / "Assets" / "source" / "wood_background.png"
SOURCE_URL = (
    "https://media.sweetwater.com/api/i/"
    "version-d99ed7f478010740fbe528cf867ab59f7d954241__q-82__w-1500__"
    "hmac-44b4b8e4ce060309da03b4c09877091aecf9c89d/"
    "images/items/1800/BigFuzzBassDlx-xlarge.jpg"
)
FALLBACK_SCREENSHOT = Path(
    r"C:\Users\sonic\.gemini\antigravity\brain\6401f4f9-875b-437a-add5-5f9774f2a660"
    r"\browser_sandbox\screenshots\step_63_shot_1.png"
)

OUT_UI_DIR = ROOT / "Assets" / "ui"
LAYOUT_HEADER_PATH = ROOT / "Source" / "ui" / "PedalLayout.h"
QA_PREVIEW_PATH = Path(
    r"C:\Users\sonic\.gemini\antigravity\brain\618efe7c-f2b1-488b-99da-ac72608d6577"
    r"\scratch\ui_preview_sheet.png"
)

# Output canvas dimensions (2x Retina for 860x718 default logical window, generous woodgrain framing)
CANVAS_W = 1720
CANVAS_H = 1436
DEFAULT_W = 860
DEFAULT_H = 718
PAD_X = 110
PAD_TOP = 84


def ensure_source_image() -> Image.Image:
    """Load the 1500x1247 studio photo, downloading it if missing."""
    if SOURCE_IMAGE_PATH.exists():
        img = Image.open(SOURCE_IMAGE_PATH).convert("RGB")
        if img.size == (1500, 1247):
            return img

    SOURCE_IMAGE_PATH.parent.mkdir(parents=True, exist_ok=True)
    try:
        req = urllib.request.Request(SOURCE_URL, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = resp.read()
        SOURCE_IMAGE_PATH.write_bytes(data)
        img = Image.open(SOURCE_IMAGE_PATH).convert("RGB")
        if img.size == (1500, 1247):
            print(f"Downloaded native {img.size} photo -> {SOURCE_IMAGE_PATH}")
            return img
    except Exception as exc:
        print(f"Warning: download failed ({exc}), checking fallback screenshot...")

    if FALLBACK_SCREENSHOT.exists():
        img = Image.open(FALLBACK_SCREENSHOT).convert("RGB")
        return img.resize((1500, 1247), Image.Resampling.LANCZOS)

    raise FileNotFoundError("Could not obtain Deluxe Bass Fuzz reference photo.")


def detect_controls(arr: np.ndarray) -> dict[str, tuple[float, float]]:
    """Measure exact pixel centers of all knobs, switch, LEDs, and footswitches."""
    H, W, _ = arr.shape

    def find_led(x0_f: float, x1_f: float, ch: int) -> tuple[float, float]:
        x0, x1 = int(W * x0_f), int(W * x1_f)
        y0, y1 = int(H * 0.66), int(H * 0.76)
        sub = arr[y0:y1, x0:x1]
        m = (sub[:, :, ch] > 240) & (sub[:, :, 0] > 180) & (sub[:, :, 1] > 180)
        ys, xs = np.where(m)
        return float(xs.mean() + x0), float(ys.mean() + y0)

    def find_fs(x0_f: float, x1_f: float) -> tuple[float, float]:
        x0, x1 = int(W * x0_f), int(W * x1_f)
        y0, y1 = int(H * 0.75), int(H * 0.89)
        sub = arr[y0:y1, x0:x1]
        ys, xs = np.where(sub.mean(axis=2) > 175)
        mx, my = np.median(xs), np.median(ys)
        d = np.hypot(xs - mx, ys - my)
        inner = d < 42.0
        return float(xs[inner].mean() + x0), float(ys[inner].mean() + y0)

    green_led = find_led(0.30, 0.42, 1)
    red_led = find_led(0.58, 0.68, 0)
    # Exact hex-nut bore centers verified on 1500x1247 macro grid
    left_fs = (270.0, 1020.0)
    right_fs = (1216.0, 1018.0)

    # Toggle switch mounting nut center (exact midpoint between -10 dB and 0 dB labels)
    toggle = (1097.5, 512.0)

    # Top row 6 knobs: locate white indicator lines
    top_band = arr[int(H * 0.14):int(H * 0.185), :, :]
    bright = (top_band[:, :, 0] > 150) & (top_band[:, :, 1] > 150) & (top_band[:, :, 2] > 150)
    xs_all = np.where(bright.sum(axis=0) >= 8)[0]
    splits = np.where(np.diff(xs_all) > 30)[0] + 1
    clusters = [c for c in np.split(xs_all, splits) if len(c) >= 2 and 60 < c.mean() < 1440]
    top_names = ["volume", "blend", "tone", "sustain", "gate", "hpf"]
    if len(clusters) != 6:
        raise RuntimeError(f"Expected 6 top knobs, detected {len(clusters)}")

    knobs: dict[str, tuple[float, float]] = {}
    ys_detected = []
    for name, c in zip(top_names, clusters):
        kx = float(c.mean())
        y0, y1 = 152, 240
        col = arr[y0:y1, int(kx) - 1:int(kx) + 2].mean(axis=(1, 2))
        ind = np.where(col > 130)[0]
        y_ind_top = float(ind[0] + y0)
        y_ind_bot = float(ind[-1] + y0)
        # Indicator stripe spans r in [0.43R .. 0.93R] from cap center
        est_r = max(72.0, (y_ind_bot - y_ind_top) / 0.50)
        est_cy = y_ind_bot + 0.43 * est_r
        # Refine horizontal center using cap skirt edges at est_cy
        x_lo = max(0, int(kx) - 95)
        x_hi = min(W, int(kx) + 96)
        row = arr[int(est_cy) - 2:int(est_cy) + 3, x_lo:x_hi].mean(axis=(0, 2))
        mid = int(kx) - x_lo
        lx, rx = mid, mid
        while lx > 5 and row[lx - 3:lx + 1].max() > 26:
            lx -= 1
        while rx < len(row) - 5 and row[rx:rx + 4].max() > 26:
            rx += 1
        cx = x_lo + 0.5 * (lx + rx)
        # Blend detected shaft X with indicator X for slightly foreshortened edge knobs
        if name in ("volume", "hpf"):
            cx = 0.65 * kx + 0.35 * cx
        else:
            cx = 0.5 * (kx + cx)
        knobs[name] = (float(cx), float(est_cy))
        if name in ("blend", "tone", "sustain", "gate"):
            ys_detected.append(est_cy)

    # Align top row cleanly to average Y of the central four knobs (~257 px)
    row_y = float(np.mean(ys_detected))
    for name in top_names:
        knobs[name] = (knobs[name][0], row_y)

    # LPF knob below HPF
    lpf_band = arr[int(H * 0.34):int(H * 0.395), int(W * 0.80):int(W * 0.95)]
    lb = (lpf_band[:, :, 0] > 150) & (lpf_band[:, :, 1] > 150) & (lpf_band[:, :, 2] > 150)
    _, lx_pts = np.where(lb)
    kx = float(lx_pts.mean() + int(W * 0.80))
    col = arr[400:540, int(kx) - 1:int(kx) + 2].mean(axis=(1, 2))
    ind = np.where(col > 130)[0]
    y_ind_top = float(ind[0] + 400)
    y_ind_bot = float(ind[-1] + 400)
    est_r = max(72.0, (y_ind_bot - y_ind_top) / 0.50)
    est_cy = y_ind_bot + 0.43 * est_r
    knobs["lpf"] = (0.5 * (kx + knobs["hpf"][0] + 1.5), float(est_cy))

    return {
        **knobs,
        "toggle": toggle,
        "green_led": green_led,
        "red_led": red_led,
        "left_fs": left_fs,
        "right_fs": right_fs,
    }


def extract_pedal_alpha_and_defringe(arr: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Segment the aluminum pedal + side jacks cleanly with zero white halo."""
    H, W, _ = arr.shape
    gray = arr.mean(axis=2)
    chroma = arr.max(axis=2) - arr.min(axis=2)

    # 1. Measure outer rounded-rectangle boundary on the clean spans (away from jacks)
    # Top edge across x=180..1320
    top_ys = []
    for x in range(180, 1321, 10):
        hits = np.where(gray[:40, x] < 236)[0]
        if len(hits):
            top_ys.append(hits[0])
    y_top = float(np.median(top_ys))  # ~7.5

    # Left & right body walls outside jack zones (y=120..380 and y=860..1120)
    left_xs, right_xs = [], []
    for y in list(range(120, 380, 8)) + list(range(860, 1120, 8)):
        hl = np.where(gray[y, :40] < 235)[0]
        hr = np.where(gray[y, W - 40:] < 235)[0]
        if len(hl):
            left_xs.append(hl[0])
        if len(hr):
            right_xs.append(W - 40 + hr[-1])
    x_left = float(np.median(left_xs))    # ~19.5
    x_right = float(np.median(right_xs))  # ~1475.5
    y_bot = 1195.8                        # just above floor shadow (1197+)

    # Build smooth rounded-rectangle SDF for the main cast enclosure
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    rc = 36.0
    cx_box = 0.5 * (x_left + x_right)
    cy_box = 0.5 * (y_top + y_bot)
    half_w = 0.5 * (x_right - x_left) - rc
    half_h = 0.5 * (y_bot - y_top) - rc
    qx = np.abs(xx - cx_box) - half_w
    qy = np.abs(yy - cy_box) - half_h
    sdf = np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0)) + np.minimum(np.maximum(qx, qy), 0.0) - rc

    # Refine outer contour using local studio-white flood-fill outside the inner body
    # Core safe interior (definitely inside aluminum): sdf < -14
    white_bg = (arr.min(axis=2) > 236.0) & (chroma < 18.0) & (sdf > -10.0)
    # Seed corners & edges
    seeds = np.zeros((H, W), dtype=bool)
    seeds[0, :] = True
    seeds[-1, :] = True
    seeds[:, 0] = True
    seeds[:, -1] = True
    # Also remove floor shadow strictly below y_bot when outside rounded box
    floor_band = (sdf > 0.8) & (yy > 1194.0)
    bg_connected = ndimage.binary_propagation(seeds & white_bg, mask=white_bg)
    bg_mask = bg_connected | floor_band

    # Clip top corners cleanly outside sdf > 2.0 where no jacks exist
    non_jack_zone = (yy < 395.0) | (yy > 860.0)
    bg_mask |= (sdf > 1.2) & non_jack_zone

    # Also protect against any tiny stray dots far outside jacks
    fg_mask = ~bg_mask
    fg_mask = ndimage.binary_fill_holes(fg_mask)
    # Keep only largest connected component
    lbl, n_lbl = ndimage.label(fg_mask)
    if n_lbl > 1:
        sizes = np.bincount(lbl.ravel())
        sizes[0] = 0
        fg_mask = lbl == int(np.argmax(sizes))

    # Build clean smooth anti-aliased alpha (1.1px feather inward on outer edge)
    dist_in = ndimage.distance_transform_edt(fg_mask)
    alpha = np.clip(dist_in / 1.4, 0.0, 1.0)
    # Soften just the outer 1px
    alpha = np.where(fg_mask, alpha, 0.0)

    # Defringe semi-transparent boundary pixels (0 < alpha < 0.99) from nearest interior RGB
    solid = alpha >= 0.98
    _, idx = ndimage.distance_transform_edt(~solid, return_indices=True)
    defringed = arr.copy()
    edge = (alpha > 0.0) & (~solid)
    defringed[edge] = arr[idx[0][edge], idx[1][edge]]

    return defringed, alpha


def sample_faceplate_texture(shape_hw: tuple[int, int], seed: int = 42) -> np.ndarray:
    """Generate authentic matte-black OEM powder-coat texture matching RGB ~(18, 18, 20)."""
    rng = np.random.default_rng(seed)
    h, w = shape_hw
    base = np.empty((h, w, 3), dtype=np.float64)
    base[:, :, 0] = 17.5
    base[:, :, 1] = 17.0
    base[:, :, 2] = 19.5
    grain = rng.normal(0.0, 1.6, size=(h, w))
    # Tiny sparse micro-specks matching faceplate matte coat
    specks = (rng.random((h, w)) > 0.992).astype(np.float64) * rng.uniform(5.0, 13.0, size=(h, w))
    for c in range(3):
        base[:, :, c] = np.clip(base[:, :, c] + grain + specks, 10.0, 40.0)
    return base


def sample_crinkle_patch(arr: np.ndarray, shape_hw: tuple[int, int]) -> np.ndarray:
    """Tile real matte-black crinkle faceplate texture sampled between the LEDs."""
    H, W = shape_hw
    # Clean untouched black faceplate strip between green and red LEDs (y=845..930, x=660..830)
    src = arr[845:930, 660:830].copy()
    # Replace rare bright green/white paint flecks with local median so tiling stays spotless
    lum = src.mean(axis=2)
    chroma = src.max(axis=2) - src.min(axis=2)
    bad = (lum > 56.0) | (chroma > 16.0)
    if bad.any():
        med = np.median(src[~bad], axis=0) if (~bad).any() else np.array([32.0, 30.0, 35.0])
        src[bad] = med
    # Mirror-tile across HxW for seamless texture continuity
    tile_ud = np.concatenate([src, src[::-1, :, :]], axis=0)
    tile = np.concatenate([tile_ud, tile_ud[:, ::-1, :]], axis=1)
    rep_y = (H // tile.shape[0]) + 1
    rep_x = (W // tile.shape[1]) + 1
    tiled = np.tile(tile, (rep_y, rep_x, 1))[:H, :W, :]
    return tiled


def clean_plate_chassis(arr: np.ndarray, controls: dict[str, tuple[float, float]]) -> np.ndarray:
    """Remove baked-in knobs, lit LEDs + bloom halos, and toggle switch cleanly."""
    out = arr.copy()
    H, W, _ = out.shape
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    tex = sample_faceplate_texture((H, W))
    led_tex = sample_crinkle_patch(arr, (H, W))

    # Detect crisp green/white silkscreen & crackle pixels so we never overwrite labels or borders
    r, g, b = out[:, :, 0], out[:, :, 1], out[:, :, 2]
    is_green_art = (g > 72.0) & ((g - r) > 15.0) & ((g - b) > 10.0)

    # 1. Clean-plate all 7 knob footprints cleanly without clipping green borders or labels
    for name in ["volume", "blend", "tone", "sustain", "gate", "hpf", "lpf"]:
        cx, cy = controls[name]
        dist = np.hypot(xx - cx, yy - cy)
        hard_mask = dist <= 88.0
        border_guard = (xx <= 57.0) | (xx >= 1431.0)
        if name == "hpf":
            border_guard |= (yy <= 160.0) | (yy >= 360.0)
        elif name == "lpf":
            border_guard |= (yy <= 405.0) | (yy >= 608.0)
        else:
            border_guard |= (yy >= 360.0)

        preserve = border_guard | ((dist > 82.0) & is_green_art)
        patch_weight = np.clip((98.0 - dist) / 10.0, 0.0, 1.0)
        patch_weight = np.where(hard_mask, 1.0, patch_weight)
        patch_weight = np.where(preserve, 0.0, patch_weight)
        patch_weight = ndimage.gaussian_filter(patch_weight, sigma=0.9)
        patch_weight = np.where(border_guard, 0.0, patch_weight)
        for c in range(3):
            out[:, :, c] = out[:, :, c] * (1.0 - patch_weight) + tex[:, :, c] * patch_weight

    # 2. Clean-plate toggle switch cleanly, staying strictly clear of -10 dB, 0 dB, and INPUT
    d_tog = np.hypot((xx - 1098.0) / 38.0, (yy - 510.5) / 36.0)
    tog_safe = (yy >= 474.0) & (yy <= 550.0) & (xx >= 1056.0) & (xx <= 1138.0)
    tog_w = np.clip((1.12 - d_tog) / 0.22, 0.0, 1.0)
    tog_w = np.where(tog_safe, tog_w, 0.0)
    tog_w = ndimage.gaussian_filter(tog_w, sigma=1.1)
    tog_w = np.where(tog_safe, tog_w, 0.0)
    for c in range(3):
        out[:, :, c] = out[:, :, c] * (1.0 - tog_w) + tex[:, :, c] * tog_w

    # 3. Clean-plate both lit LEDs + neutralize radial glow spill using real crinkle texture
    for name in ["green_led", "red_led"]:
        lx, ly = controls[name]
        dist = np.hypot(xx - lx, yy - ly)

        # Protect crisp DELUXE BASS FUZZ lime-green border above LEDs
        if name == "green_led":
            lime_stroke = (out[:, :, 0] > 55.0) & (out[:, :, 1] > 105.0) & (yy <= ly - 38.0)
        else:
            lime_stroke = (out[:, :, 0] > 80.0) & (out[:, :, 1] > 95.0) & (yy <= ly - 35.0)

        # Match local faceplate brightness at r ~ 60..75 so cloned patch blends invisibly
        annulus = (dist >= 62.0) & (dist <= 78.0) & (~lime_stroke) & (~is_green_art)
        local_med = np.median(out[annulus], axis=0) if annulus.any() else np.array([33.0, 31.0, 36.0])
        patch_med = np.median(led_tex[annulus], axis=0) if annulus.any() else np.array([33.0, 31.0, 36.0])
        matched_tex = np.clip(led_tex + (local_med - patch_med)[None, None, :], 12.0, 62.0)

        core_w = np.clip((52.0 - dist) / 14.0, 0.0, 1.0)
        halo_falloff = np.clip((84.0 - dist) / 38.0, 0.0, 1.0) ** 1.25
        w_total = np.maximum(core_w, halo_falloff)
        w_total = np.where(lime_stroke, 0.0, w_total)
        w_total = ndimage.gaussian_filter(w_total, sigma=1.2)
        w_total = np.where(lime_stroke, 0.0, w_total)
        for c in range(3):
            out[:, :, c] = out[:, :, c] * (1.0 - w_total) + matched_tex[:, :, c] * w_total

    return out


def load_wood_surface(bg_path: Path, rotate90: bool = False, oil_warmth: bool = False) -> np.ndarray:
    """Load, orient, cover-crop, and apply subtle studio lighting grade to a hardwood texture."""
    im = Image.open(bg_path).convert("RGB")
    if rotate90:
        im = im.transpose(Image.Transpose.ROTATE_90)

    src_w, src_h = im.size
    target_ratio = CANVAS_W / float(CANVAS_H)
    src_ratio = src_w / float(src_h)
    if src_ratio > target_ratio:
        crop_h = src_h
        crop_w = int(round(src_h * target_ratio))
    else:
        crop_w = src_w
        crop_h = int(round(src_w / target_ratio))
    x0 = (src_w - crop_w) // 2
    y0 = (src_h - crop_h) // 2
    im = im.crop((x0, y0, x0 + crop_w, y0 + crop_h)).resize((CANVAS_W, CANVAS_H), Image.Resampling.LANCZOS)

    arr = np.array(im).astype(np.float64) / 255.0
    if oil_warmth:
        # Transform raw unfinished veneer scan into rich hand-rubbed oiled walnut
        arr = np.power(np.clip(arr, 1e-4, 1.0), 1.52)
        arr *= np.array([0.82, 0.68, 0.54])[None, None, :]
        luma = arr.mean(axis=2, keepdims=True)
        arr = luma + (arr - luma) * 1.24
    else:
        # Subtle studio depth + saturation balance for already-finished wood scans
        arr = np.power(np.clip(arr, 1e-4, 1.0), 1.08)
        luma = arr.mean(axis=2, keepdims=True)
        arr = luma + (arr - luma) * 1.08

    # Gentle overhead studio softbox falloff (brighter around pedal center, warmer deep corners)
    yy, xx = np.mgrid[0:CANVAS_H, 0:CANVAS_W].astype(np.float64)
    nx = (xx - CANVAS_W * 0.5) / (CANVAS_W * 0.56)
    ny = (yy - CANVAS_H * 0.44) / (CANVAS_H * 0.56)
    rad = np.clip(np.hypot(nx, ny), 0.0, 1.35)
    vignette = 0.64 + 0.40 * np.exp(-1.25 * (rad ** 1.9))
    arr *= vignette[:, :, None]

    rng = np.random.default_rng(101)
    dither = rng.normal(0.0, 0.6, size=(CANVAS_H, CANVAS_W, 1))
    return np.clip(arr * 255.0 + dither, 0.0, 255.0)


def composite_chassis_on_studio(
    pedal_rgb: np.ndarray,
    pedal_alpha: np.ndarray,
    bg_image_path: Path | None = None,
    bg_rotate90: bool = False,
    oil_warmth: bool = False,
) -> Image.Image:
    """Place the cut-out, defringed pedal on woodgrain (or dark slate) with studio shadows."""
    H, W, _ = pedal_rgb.shape
    if bg_image_path is not None and bg_image_path.exists():
        canvas = load_wood_surface(bg_image_path, rotate90=bg_rotate90, oil_warmth=oil_warmth)
    else:
        canvas = np.zeros((CANVAS_H, CANVAS_W, 3), dtype=np.float64)
        yy, xx = np.mgrid[0:CANVAS_H, 0:CANVAS_W].astype(np.float64)
        nx = (xx - CANVAS_W * 0.5) / (CANVAS_W * 0.58)
        ny = (yy - CANVAS_H * 0.45) / (CANVAS_H * 0.58)
        rad = np.clip(np.hypot(nx, ny), 0.0, 1.35)
        falloff = np.exp(-1.55 * (rad ** 1.85))
        rng = np.random.default_rng(101)
        dither = rng.normal(0.0, 0.9, size=(CANVAS_H, CANVAS_W))

        bg_center = np.array([31.0, 34.0, 38.0])
        bg_edge = np.array([11.0, 13.0, 15.0])
        for c in range(3):
            canvas[:, :, c] = np.clip(bg_edge[c] + (bg_center[c] - bg_edge[c]) * falloff + dither, 0.0, 255.0)

    # Full-canvas pedal alpha mask
    alpha_full = np.zeros((CANVAS_H, CANVAS_W), dtype=np.float64)
    alpha_full[PAD_TOP:PAD_TOP + H, PAD_X:PAD_X + W] = pedal_alpha

    # Multi-layer physical studio drop shadow (multiplicative so wood grain remains visible inside penumbra)
    def add_shadow(dx: int, dy: int, sigma: float, opacity: float) -> None:
        shifted = np.zeros_like(alpha_full)
        y_src0 = max(0, -dy)
        y_src1 = min(CANVAS_H, CANVAS_H - dy)
        x_src0 = max(0, -dx)
        x_src1 = min(CANVAS_W, CANVAS_W - dx)
        shifted[y_src0 + dy:y_src1 + dy, x_src0 + dx:x_src1 + dx] = alpha_full[y_src0:y_src1, x_src0:x_src1]
        blur = ndimage.gaussian_filter(shifted, sigma=sigma)
        att = 1.0 - opacity * np.clip(blur, 0.0, 1.0)
        for c in range(3):
            canvas[:, :, c] *= att

    add_shadow(dx=0, dy=6, sigma=5.5, opacity=0.88)    # tight contact AO
    add_shadow(dx=5, dy=18, sigma=17.0, opacity=0.66)  # primary elevation shadow
    add_shadow(dx=10, dy=32, sigma=36.0, opacity=0.40) # wide ambient studio shadow

    # Composite pedal over studio surface + shadows
    rgb_full = np.zeros((CANVAS_H, CANVAS_W, 3), dtype=np.float64)
    rgb_full[PAD_TOP:PAD_TOP + H, PAD_X:PAD_X + W] = pedal_rgb
    for c in range(3):
        canvas[:, :, c] = canvas[:, :, c] * (1.0 - alpha_full) + rgb_full[:, :, c] * alpha_full

    return Image.fromarray(np.clip(np.round(canvas), 0, 255).astype(np.uint8), mode="RGB")


def generate_knob_layers() -> tuple[Image.Image, Image.Image, Image.Image, Image.Image, Image.Image]:
    """Render 256x256 shadow, top skirt strip, 3D fluted side wall strip, rotating cap, and cap lighting."""
    S = 1024
    OUT = 256
    NUM_FRAMES = 12
    TEETH = 64
    cx = cy = (S - 1) * 0.5
    R = 366.3  # -> 91.575 px in 256x256; maps 240x240 canvas box -> R_phys = 85.85 px (extra margin for 3D parallax!)

    yy, xx = np.mgrid[0:S, 0:S].astype(np.float64)
    dx = xx - cx
    dy = yy - cy
    r = np.hypot(dx, dy)
    rn = r / R
    theta = np.arctan2(dy, dx)  # -pi/2 is straight UP (12 o'clock)
    cos_t = np.where(r > 1e-6, dx / np.maximum(r, 1e-6), 0.0)
    sin_t = np.where(r > 1e-6, dy / np.maximum(r, 1e-6), 0.0)

    # --- 1. STATIONARY DROP SHADOW (sized for 1.035x tapered 3D skirt base) ---
    R_base = 1.035 * R
    ao_r = np.exp(-((np.maximum(0.0, r - 0.960 * R_base)) / (0.085 * R)) ** 2) * (r <= 1.18 * R_base)
    sh_dx, sh_dy = 0.042 * R, 0.095 * R
    r_sh = np.hypot(dx - sh_dx, dy - sh_dy)
    cast_mask = np.clip((1.01 * R_base - r_sh) / (0.08 * R), 0.0, 1.0)
    cast_blur = ndimage.gaussian_filter(cast_mask, sigma=0.072 * R)
    shadow_alpha = np.clip(np.maximum(0.84 * ao_r, 0.62 * cast_blur), 0.0, 0.92)
    shadow = np.zeros((S, S, 4), dtype=np.float64)
    shadow[:, :, 3] = shadow_alpha * 255.0
    shadow_img = Image.fromarray(np.round(shadow).astype(np.uint8), mode="RGBA").resize(
        (OUT, OUT), Image.Resampling.LANCZOS
    )

    # --- 2. 64-TOOTH 3D PBR KNURLED SKIRT + CONICAL BEVEL (12-frame cyclic sprite strip) ---
    def unit_vec(az_deg: float, el_deg: float) -> np.ndarray:
        az = math.radians(az_deg)
        el = math.radians(el_deg)
        return np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)])

    # Calibrated to real TONE photo: key softbox at -30 deg, bounce fill at +90/+150, soft top fill
    L1 = unit_vec(-30.0, 36.0)
    L2 = unit_vec(-148.0, 44.0)
    L3 = unit_vec(88.0, 48.0)
    V = np.array([0.0, 0.0, 1.0])
    H1 = L1 + V; H1 /= np.linalg.norm(H1)
    H2 = L2 + V; H2 /= np.linalg.norm(H2)
    H3 = L3 + V; H3 /= np.linalg.norm(H3)

    # Smooth conical bevel normals (rn in [0.805 .. 0.885]): seamless match to cap RGB ~(35, 33, 38)
    bevel_nx = 0.52 * cos_t
    bevel_ny = 0.52 * sin_t
    bevel_nz = np.full_like(rn, 0.854)

    rng = np.random.default_rng(7)
    bevel_micro = ndimage.gaussian_filter(rng.normal(0.0, 1.8, size=(S, S)), sigma=1.4)

    t_bevel_band = np.clip((rn - 0.800) / 0.015, 0.0, 1.0)
    t_teeth_start = np.clip((rn - 0.862) / 0.024, 0.0, 1.0)

    ndotl1_b = np.maximum(0.0, bevel_nx * L1[0] + bevel_ny * L1[1] + bevel_nz * L1[2])
    ndotl2_b = np.maximum(0.0, bevel_nx * L2[0] + bevel_ny * L2[1] + bevel_nz * L2[2])
    ndotl3_b = np.maximum(0.0, bevel_nx * L3[0] + bevel_ny * L3[1] + bevel_nz * L3[2])
    ndoth1_b = np.maximum(0.0, bevel_nx * H1[0] + bevel_ny * H1[1] + bevel_nz * H1[2])
    ndoth2_b = np.maximum(0.0, bevel_nx * H2[0] + bevel_ny * H2[1] + bevel_nz * H2[2])
    ndoth3_b = np.maximum(0.0, bevel_nx * H3[0] + bevel_ny * H3[1] + bevel_nz * H3[2])

    # Seamless transition from flat cap (~35) into softly lit bevel shoulder (~36..95)
    cap_match_t = np.clip((rn - 0.815) / 0.045, 0.0, 1.0)
    bevel_raw = (
        28.0
        + 16.0 * ndotl1_b
        + 8.0 * ndotl2_b
        + 7.0 * ndotl3_b
        + 72.0 * (ndoth1_b ** 18.0)
        + 16.0 * (ndoth2_b ** 12.0)
        + 12.0 * (ndoth3_b ** 10.0)
    )
    bevel_lum = 35.0 * (1.0 - cap_match_t) + bevel_raw * cap_match_t + bevel_micro

    strip = Image.new("RGBA", (OUT, OUT * NUM_FRAMES), (0, 0, 0, 0))
    wall_strip = Image.new("RGBA", (OUT, OUT * NUM_FRAMES), (0, 0, 0, 0))
    for frame in range(NUM_FRAMES):
        phase_off = frame / float(NUM_FRAMES)
        tooth_u = (TEETH * (theta / (2.0 * math.pi)) + phase_off) % 1.0
        # Realistic rounded molded knurl tooth profile (subtle ~2.3% outer ripple, NOT gear spikes!)
        crest_tri = 1.0 - 2.0 * np.abs(tooth_u - 0.5)
        crest_cos = 0.5 - 0.5 * np.cos(2.0 * math.pi * tooth_u)
        crest = 0.45 * crest_tri + 0.55 * crest_cos
        slope_sin = np.sin(2.0 * math.pi * tooth_u)  # smooth +/- tangential slope across molded knurl

        r_outer = R * (0.977 + 0.023 * crest)

        # 3D normal on molded knurled collar: slopes outward across r, rolls gently at outer wall
        wall_roll = np.clip((rn - 0.87) / 0.12, 0.0, 1.0)
        rad_comp = 0.56 + 0.36 * wall_roll
        z_comp = np.maximum(0.18, 0.72 - 0.50 * wall_roll)
        tan_comp = 0.44 * slope_sin * (0.45 + 0.55 * wall_roll)
        tx = -sin_t
        ty = cos_t
        nx_f = rad_comp * cos_t + tan_comp * tx
        ny_f = rad_comp * sin_t + tan_comp * ty
        nz_f = np.full_like(rn, 0.0) + z_comp
        norm_len = np.sqrt(nx_f * nx_f + ny_f * ny_f + nz_f * nz_f)
        nx_f /= norm_len
        ny_f /= norm_len
        nz_f /= norm_len

        ndotl1 = np.maximum(0.0, nx_f * L1[0] + ny_f * L1[1] + nz_f * L1[2])
        ndotl2 = np.maximum(0.0, nx_f * L2[0] + ny_f * L2[1] + nz_f * L2[2])
        ndotl3 = np.maximum(0.0, nx_f * L3[0] + ny_f * L3[1] + nz_f * L3[2])
        ndoth1 = np.maximum(0.0, nx_f * H1[0] + ny_f * H1[1] + nz_f * H1[2])
        ndoth2 = np.maximum(0.0, nx_f * H2[0] + ny_f * H2[1] + nz_f * H2[2])
        ndoth3 = np.maximum(0.0, nx_f * H3[0] + ny_f * H3[1] + nz_f * H3[2])

        # Natural subtle valley AO + soft outer-wall shadow falloff matching real photo
        valley_ao = 0.56 + 0.44 * (crest ** 0.85)
        outer_falloff = 1.0 - 0.62 * (np.clip((rn - 0.925) / 0.075, 0.0, 1.0) ** 1.35)
        teeth_diff = (24.0 + 34.0 * ndotl1 + 20.0 * ndotl2 + 22.0 * ndotl3) * valley_ao
        teeth_spec = (66.0 * (ndoth1 ** 16.0) + 22.0 * (ndoth2 ** 12.0) + 18.0 * (ndoth3 ** 10.0)) * (0.5 + 0.5 * crest)
        teeth_lum = (teeth_diff + teeth_spec) * outer_falloff + bevel_micro * 0.85

        skirt_lum = bevel_lum * (1.0 - t_teeth_start) + teeth_lum * t_teeth_start
        frame_arr = np.zeros((S, S, 4), dtype=np.float64)
        frame_arr[:, :, 0] = np.clip(skirt_lum * 0.99, 8.0, 210.0)
        frame_arr[:, :, 1] = np.clip(skirt_lum * 0.94, 8.0, 202.0)
        frame_arr[:, :, 2] = np.clip(skirt_lum * 1.06, 10.0, 218.0)

        outer_alpha = np.clip((r_outer - r) / 2.8, 0.0, 1.0)
        frame_arr[:, :, 3] = t_bevel_band * outer_alpha * 255.0

        frame_img = Image.fromarray(np.clip(np.round(frame_arr), 0, 255).astype(np.uint8), mode="RGBA").resize(
            (OUT, OUT), Image.Resampling.LANCZOS
        )
        strip.paste(frame_img, (0, frame * OUT))

        # --- Vertical cylindrical fluted side wall slice (for off-center 3D perspective extrusion) ---
        tan_w = 0.46 * slope_sin
        wx = 0.88 * cos_t + tan_w * tx
        wy = 0.88 * sin_t + tan_w * ty
        wz = np.full_like(rn, 0.14)
        w_len = np.sqrt(wx * wx + wy * wy + wz * wz)
        wx /= w_len
        wy /= w_len
        wz /= w_len

        wl1 = np.maximum(0.0, wx * L1[0] + wy * L1[1] + wz * L1[2])
        wl2 = np.maximum(0.0, wx * L2[0] + wy * L2[1] + wz * L2[2])
        wl3 = np.maximum(0.0, wx * L3[0] + wy * L3[1] + wz * L3[2])
        wh1 = np.maximum(0.0, wx * H1[0] + wy * H1[1] + wz * H1[2])
        wh2 = np.maximum(0.0, wx * H2[0] + wy * H2[1] + wz * H2[2])
        wh3 = np.maximum(0.0, wx * H3[0] + wy * H3[1] + wz * H3[2])

        wall_ao = 0.54 + 0.46 * (crest ** 0.85)
        wall_diff = (20.0 + 28.0 * wl1 + 17.0 * wl2 + 19.0 * wl3) * wall_ao
        wall_spec = (48.0 * (wh1 ** 18.0) + 16.0 * (wh2 ** 12.0) + 14.0 * (wh3 ** 10.0)) * (0.5 + 0.5 * crest)
        wall_lum = (wall_diff + wall_spec) * 0.76 + bevel_micro * 0.75

        wall_arr = np.zeros((S, S, 4), dtype=np.float64)
        wall_arr[:, :, 0] = np.clip(wall_lum * 0.99, 8.0, 185.0)
        wall_arr[:, :, 1] = np.clip(wall_lum * 0.94, 8.0, 178.0)
        wall_arr[:, :, 2] = np.clip(wall_lum * 1.06, 10.0, 192.0)
        # Solid opaque matte-black ABS core inside r <= r_outer so stacked slices have zero see-through seams
        wall_arr[:, :, 3] = np.clip((r_outer - r) / 2.2, 0.0, 1.0) * 255.0

        wall_img = Image.fromarray(np.clip(np.round(wall_arr), 0, 255).astype(np.uint8), mode="RGBA").resize(
            (OUT, OUT), Image.Resampling.LANCZOS
        )
        wall_strip.paste(wall_img, (0, frame * OUT))

    # --- 3. ROTATING FLAT MATTE CAP + POINTED V-TIP WHITE INDICATOR ---
    cap = np.zeros((S, S, 4), dtype=np.float64)
    stipple = ndimage.gaussian_filter(rng.normal(0.0, 2.0, size=(S, S)), sigma=1.2)
    cap_rgb = np.array([35.0, 33.0, 38.0])
    for c in range(3):
        cap[:, :, c] = np.clip(cap_rgb[c] + stipple, 18.0, 56.0)
    # Feather smoothly across 0.805..0.825R directly over the cap-matched bevel inner ring (zero dark seam!)
    cap[:, :, 3] = np.clip((0.822 * R - r) / (0.018 * R), 0.0, 1.0) * 255.0

    # Authentic OEM tapered pointed V-tip indicator stripe pointing straight UP (-pi/2)
    in_up_half = dy < 0.0
    t_bevel_tip = np.clip((r - 0.815 * R) / (0.972 * R - 0.815 * R), 0.0, 1.0)
    half_w = np.where(
        r <= 0.815 * R,
        0.032 * R,
        (0.038 * R) * (1.0 - t_bevel_tip ** 1.15),
    )
    dist_x = np.abs(dx)
    stripe_r_mask = (
        np.clip((r - 0.370 * R) / (0.016 * R), 0.0, 1.0)
        * np.clip((0.970 * R - r) / (0.012 * R), 0.0, 1.0)
    )
    stripe_x_mask = np.clip((half_w - dist_x) / (0.008 * R), 0.0, 1.0)
    stripe_mask = np.where(in_up_half, stripe_r_mask * stripe_x_mask, 0.0)

    # Subtle 1px groove shadow framing the recessed paint stripe
    recess_x = np.clip((half_w + 0.010 * R - dist_x) / (0.008 * R), 0.0, 1.0)
    recess_mask = np.where(
        in_up_half,
        recess_x * np.clip((r - 0.358 * R) / (0.014 * R), 0.0, 1.0) * np.clip((0.974 * R - r) / (0.012 * R), 0.0, 1.0) * (1.0 - stripe_mask),
        0.0,
    )
    for c in range(3):
        cap[:, :, c] *= (1.0 - 0.38 * recess_mask)

    stripe_rgb = np.array([196.0, 194.0, 204.0])
    # Subtle bevel tilt brightness on pointed bevel tip (r > 0.785R)
    stripe_shade = 0.95 + 0.05 * np.clip(1.0 - (dist_x / np.maximum(half_w, 1.0)) ** 2, 0.0, 1.0)
    for c in range(3):
        cap[:, :, c] = cap[:, :, c] * (1.0 - stripe_mask) + stripe_rgb[c] * stripe_shade * stripe_mask

    # Include solid indicator stripe alpha along the conical bevel (0.785R..0.970R)
    cap_alpha = np.maximum(cap[:, :, 3] / 255.0, np.maximum(stripe_mask, recess_mask * 0.5))
    cap[:, :, 3] = np.clip(cap_alpha, 0.0, 1.0) * 255.0
    cap_img = Image.fromarray(np.clip(np.round(cap), 0, 255).astype(np.uint8), mode="RGBA").resize(
        (OUT, OUT), Image.Resampling.LANCZOS
    )

    # --- 4. STATIONARY STUDIO CAP LIGHTING OVERLAY ---
    lighting = np.zeros((S, S, 4), dtype=np.float64)
    inside_cap = np.clip((0.965 * R - r) / (0.02 * R), 0.0, 1.0)
    # Soft directional gradient toward upper-right studio softbox (-32 deg)
    key_proj = (dx * math.cos(math.radians(-32.0)) + dy * math.sin(math.radians(-32.0))) / R
    white_a = np.clip(key_proj, 0.0, 1.0) * 0.075
    black_a = np.clip(-key_proj, 0.0, 1.0) * 0.115
    total_a = np.clip(white_a + black_a * (1.0 - white_a), 0.0, 1.0)
    safe_a = np.maximum(total_a, 1e-6)
    white_frac = white_a / safe_a
    for c, v in enumerate([238.0, 240.0, 248.0]):
        lighting[:, :, c] = v * white_frac
    lighting[:, :, 3] = total_a * inside_cap * 255.0
    lighting_img = Image.fromarray(np.clip(np.round(lighting), 0, 255).astype(np.uint8), mode="RGBA").resize(
        (OUT, OUT), Image.Resampling.LANCZOS
    )

    return shadow_img, strip, wall_strip, cap_img, lighting_img


def generate_toggle_assets() -> tuple[Image.Image, Image.Image]:
    """Render 128x160 photorealistic chrome mini-toggle switch (DOWN=0dB, UP=-10dB)."""
    OUT_W, OUT_H = 128, 160
    SW, SH = OUT_W * 4, OUT_H * 4  # 512 x 640
    cx, cy = SW * 0.5, SH * 0.5    # (256, 320)

    yy, xx = np.mgrid[0:SH, 0:SW].astype(np.float64)
    dx = xx - cx
    dy = yy - cy
    r = np.hypot(dx, dy)
    ang = np.arctan2(dy, dx)

    def render_state(up: bool) -> Image.Image:
        canvas = np.zeros((SH, SW, 4), dtype=np.float64)

        def over(layer_rgba: np.ndarray) -> None:
            la = layer_rgba[:, :, 3:4] / 255.0
            ca = canvas[:, :, 3:4] / 255.0
            out_a = la + ca * (1.0 - la)
            safe = np.maximum(out_a, 1e-6)
            canvas[:, :, :3] = (layer_rgba[:, :, :3] * la + canvas[:, :, :3] * ca * (1.0 - la)) / safe
            canvas[:, :, 3:4] = out_a * 255.0

        # 1. Base mounting shadow on faceplate
        base_sh = np.zeros((SH, SW, 4), dtype=np.float64)
        r_sh = np.hypot(dx - 9.0, dy - 14.0)
        base_sh[:, :, 3] = np.clip((200.0 - r_sh) / 52.0, 0.0, 1.0) * 195.0
        over(base_sh)

        # 2. Chrome circular mounting washer (R = 142 -> 66.6 px in photo space)
        washer_r = 142.0
        w_layer = np.zeros((SH, SW, 4), dtype=np.float64)
        brushed = 158.0 + 48.0 * np.cos(ang * 2.0 + 0.7) + 22.0 * np.sin(r * 0.35)
        bevel_rim = np.exp(-((r - (washer_r - 5.5)) / 5.5) ** 2) * 45.0
        w_layer[:, :, 0] = np.clip(brushed + bevel_rim, 40, 245)
        w_layer[:, :, 1] = np.clip(brushed + bevel_rim - 2, 40, 243)
        w_layer[:, :, 2] = np.clip(brushed + bevel_rim + 4, 42, 248)
        w_layer[:, :, 3] = np.clip((washer_r - r) / 3.2, 0.0, 1.0) * 255.0
        over(w_layer)

        # 3. Hex mounting nut
        hex_layer = np.zeros((SH, SW, 4), dtype=np.float64)
        hex_d = np.maximum(
            np.abs(dx),
            0.5 * np.abs(dx) + 0.8660254 * np.abs(dy),
        )
        nut_r = 108.0
        chamfer = np.clip((nut_r - hex_d) / 15.0, 0.0, 1.0)
        facet_shade = 132.0 + 75.0 * np.cos(ang + 2.1) + 28.0 * np.cos(ang * 6.0)
        nut_ring = np.exp(-((r - 90.0) / 8.0) ** 2) * 58.0
        hex_val = facet_shade * (0.72 + 0.28 * chamfer) + nut_ring
        hex_layer[:, :, 0] = np.clip(hex_val, 35, 250)
        hex_layer[:, :, 1] = np.clip(hex_val - 3, 35, 248)
        hex_layer[:, :, 2] = np.clip(hex_val + 2, 35, 252)
        hex_layer[:, :, 3] = np.clip((nut_r - hex_d) / 2.8, 0.0, 1.0) * 255.0
        over(hex_layer)

        # 4. Threaded inner collar (R = 76) + dark pivot cavity (R = 48)
        collar = np.zeros((SH, SW, 4), dtype=np.float64)
        c_val = 128.0 + 62.0 * np.cos(ang + 1.8) + 18.0 * np.sin(r * 0.9)
        cavity = np.clip((49.0 - r) / 4.5, 0.0, 1.0)
        c_col = c_val * (1.0 - cavity) + 22.0 * cavity
        collar[:, :, 0] = np.clip(c_col, 15, 242)
        collar[:, :, 1] = np.clip(c_col - 2, 15, 240)
        collar[:, :, 2] = np.clip(c_col + 2, 16, 245)
        collar[:, :, 3] = np.clip((76.0 - r) / 2.8, 0.0, 1.0) * 255.0
        over(collar)

        # 5. Cast shadow of bat handle onto nut & faceplate
        sign = -1.0 if up else 1.0
        # Scaled so tip reaches cleanly between -10 dB above and 0 dB below
        bat_len = 132.0
        t_seg = np.clip(dy / (sign * bat_len), 0.0, 1.0)
        rad_t = 21.0 + 19.0 * t_seg  # 21px shaft -> 40px tip radius

        sh_t = np.clip((dy - 12.0) / (sign * bat_len), 0.0, 1.0)
        sh_rad = 21.0 + 20.0 * sh_t
        sh_dist = np.hypot(dx - 16.0, (dy - 12.0) - sh_t * sign * bat_len) - sh_rad
        bat_sh = np.zeros((SH, SW, 4), dtype=np.float64)
        sh_alpha = np.clip((9.0 - sh_dist) / 24.0, 0.0, 1.0) * 0.65
        bat_sh[:, :, 3] = ndimage.gaussian_filter(sh_alpha, sigma=9.0) * 255.0
        over(bat_sh)

        # 6. Mirror-chrome tapered bat handle + spherical tip
        bat_dist = np.hypot(dx, dy - t_seg * sign * bat_len) - rad_t
        bat_layer = np.zeros((SH, SW, 4), dtype=np.float64)
        u = np.clip(dx / np.maximum(rad_t, 1.0), -1.0, 1.0)
        z_cyl = np.sqrt(np.maximum(0.0, 1.0 - u * u))
        sky_band = np.exp(-((u + 0.28) / 0.24) ** 2) * 145.0
        horizon_dark = np.exp(-((u - 0.14) / 0.22) ** 2) * 95.0
        rim_right = np.exp(-((u - 0.72) / 0.18) ** 2) * 85.0
        base_chrome = 128.0 + sky_band - horizon_dark + rim_right
        tip_boost = (55.0 if up else 42.0) * (t_seg ** 1.4)
        root_ao = (1.0 - 0.42 * np.exp(-(t_seg / 0.22) ** 2))
        tip_cx = -0.26 * 40.0
        tip_cy = sign * bat_len - 10.0
        tip_spec = np.exp(-((dx - tip_cx) ** 2 + (dy - tip_cy) ** 2) / (17.0 ** 2)) * 115.0

        lum = (base_chrome + tip_boost) * root_ao + tip_spec
        # Crisp dark silhouette edge at extreme cylinder grazing angle
        grazing = 0.78 + 0.22 * z_cyl
        lum *= grazing

        bat_layer[:, :, 0] = np.clip(lum, 24, 255)
        bat_layer[:, :, 1] = np.clip(lum - 2, 24, 254)
        bat_layer[:, :, 2] = np.clip(lum + 4, 26, 255)
        bat_layer[:, :, 3] = np.clip((-bat_dist) / 2.4, 0.0, 1.0) * 255.0
        over(bat_layer)

        return Image.fromarray(np.clip(np.round(canvas), 0, 255).astype(np.uint8), mode="RGBA").resize(
            (OUT_W, OUT_H), Image.Resampling.LANCZOS
        )

    return render_state(up=False), render_state(up=True)


def generate_led_assets() -> dict[str, Image.Image]:
    """Render 192x192 OFF and ON photorealistic 5mm domed glass LEDs matching real macro photo."""
    OUT = 192
    S = OUT * 4  # 768x768 supersampled (4x) -> 1 px on 1500px photo == 4 px in S
    cx = cy = (S - 1) * 0.5
    R_dome = 102.0  # 25.5 px in 192px (matches real ~25px glowing epoxy dome radius!)
    R_hole = 116.0  # 29.0 px drilled faceplate hole/bezel edge

    yy, xx = np.mgrid[0:S, 0:S].astype(np.float64)
    dx = xx - cx
    dy = yy - cy
    r = np.hypot(dx, dy)
    theta = np.arctan2(dy, dx)

    rng = np.random.default_rng(19)
    crinkle_grain = ndimage.gaussian_filter(rng.normal(0.0, 1.0, size=(S, S)), sigma=2.5)

    def make_led(color: str, lit: bool) -> Image.Image:
        img = np.zeros((S, S, 4), dtype=np.float64)

        hole_mask = np.clip((R_hole - r) / 4.5, 0.0, 1.0)
        dome_mask = np.clip((R_dome - r) / 3.5, 0.0, 1.0)

        u = np.clip(dx / R_dome, -1.0, 1.0)
        v = np.clip(dy / R_dome, -1.0, 1.0)
        rr_n = np.clip(r / R_dome, 0.0, 1.0)
        z = np.sqrt(np.maximum(0.0, 1.0 - rr_n * rr_n))

        if color == "green":
            edge_off = np.array([5.0, 22.0, 10.0])
            mid_off = np.array([11.0, 56.0, 24.0])
            rim_lit = np.array([18.0, 188.0, 104.0])
            spill_rgb = np.array([22.0, 212.0, 118.0])
            core_rgb = np.array([250.0, 255.0, 252.0])
        else:
            edge_off = np.array([22.0, 4.0, 5.0])
            mid_off = np.array([66.0, 11.0, 13.0])
            rim_lit = np.array([208.0, 26.0, 28.0])
            spill_rgb = np.array([218.0, 28.0, 24.0])
            core_rgb = np.array([255.0, 251.0, 220.0])

        if not lit:
            # 1. Drilled chassis hole & dark satin nylon mounting grommet lip (r = 102..116)
            # Subtle top-right catch light on grommet lip + dark AO inside hole bottom
            lip_hi = np.clip(np.cos(theta - (-0.6)), 0.0, 1.0) * 18.0 * np.exp(-((r - 109.0) / 4.5) ** 2)
            hole_ao = np.exp(-((r - 104.0) / 5.5) ** 2) * 10.0
            bezel_col = np.array([22.0, 21.0, 24.0])[None, None, :] + lip_hi[:, :, None] - hole_ao[:, :, None]

            # 2. Translucent tinted epoxy dome with visible internal reflector cup & post
            cup_dist = np.hypot(u - 0.03, v - 0.05)
            cup_shape = np.clip((0.46 - cup_dist) / 0.14, 0.0, 1.0) * np.clip((cup_dist - 0.14) / 0.10, 0.0, 1.0)
            anode_pin = np.clip((0.10 - np.abs(u + 0.04)) / 0.04, 0.0, 1.0) * np.clip((v - 0.05) / 0.2, 0.0, 1.0)
            metal_internal = np.clip(cup_shape * 0.55 + anode_pin * 0.45, 0.0, 1.0)

            depth_t = np.clip(0.25 + 0.75 * z - 0.18 * v, 0.0, 1.0)
            dome_col = np.empty((S, S, 3), dtype=np.float64)
            for c in range(3):
                base_c = edge_off[c] * (1.0 - depth_t) + mid_off[c] * depth_t
                dome_col[:, :, c] = base_c + metal_internal * (26.0 if c != 1 else 28.0)

            # 3. Internal Fresnel caustic refraction crescent on lower-right epoxy wall
            caustic = (
                np.exp(-((rr_n - 0.83) / 0.09) ** 2)
                * np.clip(np.cos(theta - 0.85), 0.0, 1.0) ** 2.0
                * 0.85
            )
            caustic_col = mid_off * 1.9 + np.array([18.0, 18.0, 18.0])
            for c in range(3):
                dome_col[:, :, c] += caustic * caustic_col[c]

            # 4. Crisp curved studio softbox window reflection (upper-left & upper-right)
            # Curved rectangular softbox reflection warped along spherical dome surface
            win_u = u + 0.34
            win_v = v + 0.36
            glint_main = np.exp(-((win_u / 0.22) ** 4 + (win_v / 0.16) ** 4)) * 165.0
            glint_pin = np.exp(-(((u - 0.28) / 0.07) ** 2 + ((v + 0.32) / 0.07) ** 2)) * 110.0
            rim_sheen = np.exp(-((rr_n - 0.91) / 0.06) ** 2) * np.clip(np.cos(theta + 2.2), 0.0, 1.0) ** 2.0 * 48.0
            for c in range(3):
                dome_col[:, :, c] = np.clip(dome_col[:, :, c] + glint_main + glint_pin + rim_sheen, 0.0, 255.0)
                img[:, :, c] = bezel_col[:, :, c] * (1.0 - dome_mask) + dome_col[:, :, c] * dome_mask

            # Soft outer contact shadow ring on faceplate (r = 114..136)
            outer_ao = np.exp(-((np.maximum(0.0, r - 114.0)) / 12.0) ** 2) * 0.45
            final_a = np.maximum(hole_mask, outer_ao * (r > 110.0))
            img[:, :, 3] = np.clip(final_a, 0.0, 1.0) * 255.0
        else:
            # ON state calibrated directly to real_macro_ref.png!
            # 1. Incandescent saturated core inside r <= 82..88 px (20.5..22 px on 1500px photo)
            core_r = 85.0 if color == "green" else 81.0
            core_t = 1.0 - np.clip((r - (core_r - 12.0)) / 18.0, 0.0, 1.0) ** 1.4

            # For Red LED, add subtle warm amber transition halo right around core perimeter (real_macro_ref!)
            amber_band = np.exp(-((r - core_r) / 8.0) ** 2) if color == "red" else np.zeros_like(r)

            dome_col = np.empty((S, S, 3), dtype=np.float64)
            for c in range(3):
                c_val = rim_lit[c] * (1.0 - core_t) + core_rgb[c] * core_t
                if color == "red":
                    amber_target = [255.0, 165.0, 72.0][c]
                    c_val = c_val * (1.0 - 0.45 * amber_band) + amber_target * (0.45 * amber_band)
                dome_col[:, :, c] = np.clip(c_val, 0.0, 255.0)

            # Physical dark epoxy rim bevel right inside drilled hole edge (r = 98..112)
            rim_dark = np.clip((r - 97.0) / 14.0, 0.0, 1.0) * 0.42
            for c in range(3):
                dome_col[:, :, c] *= (1.0 - rim_dark)

            # Subtle glassy refraction ring on outer lip of lit dome (r ~ 96)
            refract_lip = np.exp(-((r - 96.0) / 4.5) ** 2) * (0.5 + 0.5 * np.cos(theta - 0.9))
            for c in range(3):
                dome_col[:, :, c] = np.clip(dome_col[:, :, c] + refract_lip * rim_lit[c] * 0.32, 0.0, 255.0)

            # 2. Tight realistic surface spill on crinkle faceplate (falls swiftly to 0 by r=245, ~61px on photo!)
            spill_dist = np.maximum(0.0, r - 104.0)
            spill_a = 0.42 * np.exp(-spill_dist / 42.0) * np.clip(1.0 - spill_dist / 148.0, 0.0, 1.0) ** 1.15
            # Modulate spill lightly with crinkle texture so it sits ON the paint rather than glowing fog
            spill_a *= np.clip(1.0 + 0.14 * crinkle_grain, 0.72, 1.18)

            # Crisp drilled hole rim shadow separating dome from faceplate
            hole_rim_ao = np.exp(-((r - 113.0) / 4.5) ** 2) * 0.45
            for c in range(3):
                spill_c = spill_rgb[c] * (1.0 - hole_rim_ao)
                img[:, :, c] = spill_c * (1.0 - dome_mask) + dome_col[:, :, c] * dome_mask

            img[:, :, 3] = np.clip(np.maximum(dome_mask, spill_a), 0.0, 1.0) * 255.0

        return Image.fromarray(np.clip(np.round(img), 0, 255).astype(np.uint8), mode="RGBA").resize(
            (OUT, OUT), Image.Resampling.LANCZOS
        )

    return {
        "led_green_off.png": make_led("green", False),
        "led_green_on.png": make_led("green", True),
        "led_red_off.png": make_led("red", False),
        "led_red_on.png": make_led("red", True),
    }


def generate_footswitch_pressed_overlays(
    photo_arr: np.ndarray, controls: dict[str, tuple[float, float]]
) -> tuple[Image.Image, Image.Image]:
    """Create 180x180 realistic 3PDT plunge overlays from the real chrome footswitches."""
    OUT = 180
    half = OUT // 2

    def build_pressed(
        overlay_cx: float,
        overlay_cy: float,
        cap_abs_x: float,
        cap_abs_y: float,
        cap_rx: float,
        cap_ry: float,
        bore_abs_x: float,
        bore_abs_y: float,
        bore_rx: float,
        bore_ry: float,
        shift_x: float,
        shift_y: float,
        scale: float = 0.985,
        collar_min: float = 0.81,
    ) -> Image.Image:
        x0 = int(round(overlay_cx)) - half
        y0 = int(round(overlay_cy)) - half
        patch = photo_arr[y0:y0 + OUT, x0:x0 + OUT].copy()

        yy, xx = np.mgrid[0:OUT, 0:OUT].astype(np.float64)
        cap_cx = cap_abs_x - x0
        cap_cy = cap_abs_y - y0
        bore_cx = bore_abs_x - x0
        bore_cy = bore_abs_y - y0

        bore_norm = np.hypot((xx - bore_cx) / bore_rx, (yy - bore_cy) / bore_ry)
        old_cap_norm = np.hypot((xx - cap_cx) / cap_rx, (yy - cap_cy) / cap_ry)

        # Sample genuine bright chrome collar angles right outside the unpressed cap
        num_bins = 180
        thetas = np.linspace(-math.pi, math.pi, num_bins, endpoint=False)
        r_mults = np.linspace(1.03, 1.12, 6)
        sampled_rgb = np.zeros((num_bins, 3), dtype=np.float64)
        valid_mask = np.zeros(num_bins, dtype=bool)

        for ti, ang in enumerate(thetas):
            sx = np.clip(cap_cx + r_mults * cap_rx * math.cos(ang), 0.0, OUT - 1.0)
            sy = np.clip(cap_cy + r_mults * cap_ry * math.sin(ang), 0.0, OUT - 1.0)
            cols = np.column_stack([
                ndimage.map_coordinates(patch[:, :, c], [sy, sx], order=1) for c in range(3)
            ])
            cols_chroma = cols.max(axis=1) - cols.min(axis=1)
            bright = (cols.mean(axis=1) > 160.0) & (cols_chroma < 30.0)
            if np.any(bright):
                sampled_rgb[ti] = np.percentile(cols[bright], 70.0, axis=0)
                valid_mask[ti] = True

        if np.any(valid_mask):
            valid_t = thetas[valid_mask]
            t_ext = np.concatenate([valid_t - 2.0 * math.pi, valid_t, valid_t + 2.0 * math.pi])
            for c in range(3):
                v_ext = np.tile(sampled_rgb[valid_mask, c], 3)
                sampled_rgb[:, c] = np.interp(thetas, t_ext, v_ext)
        else:
            sampled_rgb[:] = np.array([196.0, 193.0, 196.0])

        sampled_rgb = ndimage.gaussian_filter1d(sampled_rgb, sigma=2.0, axis=0, mode="wrap")

        # Map each pixel's angle around cap center via periodic linear interpolation
        cap_ang = np.arctan2(yy - cap_cy, xx - cap_cx)
        thetas_wrap = np.append(thetas, math.pi)
        collar_base = np.empty_like(patch)
        for c in range(3):
            vals_wrap = np.append(sampled_rgb[:, c], sampled_rgb[0, c])
            collar_base[:, :, c] = np.interp(cap_ang, thetas_wrap, vals_wrap)

        new_cx = cap_cx + shift_x
        new_cy = cap_cy + shift_y
        new_cap_norm = np.hypot((xx - new_cx) / (cap_rx * scale), (yy - new_cy) / (cap_ry * scale))

        # Smooth directional reach: 1.010 at 12 o'clock (preserves upper threads), 1.030 toward bottom/sides
        lower_t = 0.5 + 0.5 * np.sin(cap_ang)
        cover_lim = 1.010 + 0.020 * lower_t

        # Synthesize uncovered brushed-chrome collar fading seamlessly to 1.00x at outer boundary
        d_in = np.maximum(0.0, new_cap_norm - 1.00)
        d_out = np.maximum(0.0, (cover_lim - 0.005) - old_cap_norm)
        t_crescent = np.clip(d_in / np.maximum(d_in + d_out, 1e-6), 0.0, 1.0)
        lathe_sheen = 1.0 - (1.0 - t_crescent) * (0.04 * (0.5 - 0.5 * np.sin(bore_norm * 46.0)))
        collar_shade = (collar_min + (1.0 - collar_min) * t_crescent) * lathe_sheen
        collar_synth = np.clip(collar_base * collar_shade[:, :, None], 0.0, 255.0)

        cover_old = np.clip((cover_lim - old_cap_norm) / 0.035, 0.0, 1.0)
        bg_layer = patch * (1.0 - cover_old[:, :, None]) + collar_synth * cover_old[:, :, None]

        # Sample perspective-depressed cap cleanly via cubic spline from real brushed steel top
        src_x = np.clip(cap_cx + (xx - new_cx) / scale, 0.0, OUT - 1.0)
        src_y = np.clip(cap_cy + (yy - new_cy) / scale, 0.0, OUT - 1.0)
        warped_cap = np.empty_like(patch)
        for c in range(3):
            warped_cap[:, :, c] = np.clip(
                ndimage.map_coordinates(patch[:, :, c], [src_y, src_x], order=3),
                0.0,
                255.0,
            )

        # Recessed lighting: crisp bright brushed steel (0.93) + soft upper bushing lip shadow + lower bevel glint
        top_in = np.clip(-(yy - new_cy) / (cap_ry * scale), 0.0, 1.0)
        rim_prox = np.clip((new_cap_norm - 0.50) / 0.50, 0.0, 1.0)
        cap_shade = 0.93 * (1.0 - 0.13 * (top_in ** 1.25) * (0.35 + 0.65 * rim_prox))
        bottom_edge = np.clip((yy - new_cy) / (cap_ry * scale), 0.0, 1.0)
        cap_bevel = np.exp(-((new_cap_norm - 0.96) / 0.030) ** 2) * (0.3 + 0.7 * bottom_edge) * 8.0
        warped_cap = np.clip(warped_cap * cap_shade[:, :, None] + cap_bevel[:, :, None], 0.0, 255.0)

        cap_mask = np.clip((1.00 - new_cap_norm) / 0.035, 0.0, 1.0)

        # Crisp machined sliding-fit seam strictly outside cap without any dark crater moat
        seam_ao = np.exp(-((np.maximum(0.0, new_cap_norm - 0.995)) / 0.032) ** 2) * 0.22 * (1.0 - cap_mask)

        out = np.zeros((OUT, OUT, 4), dtype=np.float64)
        for c in range(3):
            col_bg = bg_layer[:, :, c] * (1.0 - seam_ao)
            out[:, :, c] = col_bg * (1.0 - cap_mask) + warped_cap[:, :, c] * cap_mask

        # Clean achromatic hex-nut metal mask (excluding green CROSSOVER & white BYPASS text below nut)
        chroma = patch.max(axis=2) - patch.min(axis=2)
        metal_bin = (patch.mean(axis=2) > 80.0) & (chroma < 35.0)
        metal_bin[164:, :] = False
        metal_bin = ndimage.binary_fill_holes(metal_bin)
        lbl, n_lbl = ndimage.label(metal_bin)
        if n_lbl > 1:
            sizes = np.bincount(lbl.ravel())
            sizes[0] = 0
            metal_bin = lbl == int(np.argmax(sizes))
        metal_eroded = ndimage.binary_erosion(metal_bin, iterations=1)
        metal_mask = np.clip(ndimage.gaussian_filter(metal_eroded.astype(np.float64), sigma=0.8), 0.0, 1.0)

        # Tight overlay alpha mask covering unpressed & pressed cap ellipses strictly inside metal nut
        union_mask = np.maximum(
            np.clip((cover_lim + 0.01 - old_cap_norm) / 0.04, 0.0, 1.0),
            np.clip((1.05 - new_cap_norm) / 0.04, 0.0, 1.0),
        )
        out[:, :, 3] = np.clip(union_mask * metal_mask, 0.0, 1.0) * 255.0
        return Image.fromarray(np.clip(np.round(out), 0, 255).astype(np.uint8), mode="RGBA")

    # Exact 1px-measured cap and hex-nut shaft-axis plunge parameters:
    left_overlay = build_pressed(
        *controls["left_fs"],
        cap_abs_x=259.0, cap_abs_y=1028.0, cap_rx=49.0, cap_ry=45.5,
        bore_abs_x=275.5, bore_abs_y=1015.5, bore_rx=57.0, bore_ry=54.0,
        shift_x=+3.0, shift_y=-2.8, scale=0.988, collar_min=0.86,
    )
    right_overlay = build_pressed(
        *controls["right_fs"],
        cap_abs_x=1216.5, cap_abs_y=1025.0, cap_rx=49.0, cap_ry=45.0,
        bore_abs_x=1215.5, bore_abs_y=1014.5, bore_rx=58.0, bore_ry=54.5,
        shift_x=-0.4, shift_y=-4.0, scale=0.980, collar_min=0.81,
    )
    return left_overlay, right_overlay


KNOB_BOX_SIZE = 240.0
KNOB_CAM_X = 1210.0
KNOB_CAM_Y = 580.0
KNOB_CAM_K = 0.0138

# Label-aligned physical base centers (1500x1247 photo space)
KNOB_BASE_POSITIONS: dict[str, tuple[float, float]] = {
    "volume":  (159.0, 261.0),
    "blend":   (395.5, 261.0),
    "tone":    (628.0, 261.0),
    "sustain": (861.5, 261.0),
    "gate":    (1092.0, 261.0),
    "hpf":     (1328.5, 261.0),
    "lpf":     (1329.0, 516.0),
}


def knob_parallax_px(name: str) -> tuple[float, float]:
    bx, by = KNOB_BASE_POSITIONS[name]
    return ((bx - KNOB_CAM_X) * KNOB_CAM_K, (by - KNOB_CAM_Y) * KNOB_CAM_K)


def write_layout_header(controls: dict[str, tuple[float, float]]) -> None:
    """Emit Source/ui/PedalLayout.h with exact normalized control coordinates and 3D knob parallax."""
    LAYOUT_HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)

    def norm_rect(px: float, py: float, pw: float, ph: float) -> tuple[float, float, float, float]:
        cx = (PAD_X + px) / CANVAS_W
        cy = (PAD_TOP + py) / CANVAS_H
        w = pw / CANVAS_W
        h = ph / CANVAS_H
        return cx, cy, w, h

    # Knob box 240x240 -> with R = 366.3/1024, physical radius = 85.85 px (+~20px headroom for 3D parallax)
    entries = {
        "volume": norm_rect(*KNOB_BASE_POSITIONS["volume"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "blend": norm_rect(*KNOB_BASE_POSITIONS["blend"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "tone": norm_rect(*KNOB_BASE_POSITIONS["tone"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "sustain": norm_rect(*KNOB_BASE_POSITIONS["sustain"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "gate": norm_rect(*KNOB_BASE_POSITIONS["gate"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "hpf": norm_rect(*KNOB_BASE_POSITIONS["hpf"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "lpf": norm_rect(*KNOB_BASE_POSITIONS["lpf"], KNOB_BOX_SIZE, KNOB_BOX_SIZE),
        "inputPadToggle": norm_rect(*controls["toggle"], 120.0, 150.0),
        "crossoverLed": norm_rect(*controls["green_led"], 192.0, 192.0),
        "bypassLed": norm_rect(*controls["red_led"], 192.0, 192.0),
        "crossoverFootswitch": norm_rect(*controls["left_fs"], 180.0, 180.0),
        "bypassFootswitch": norm_rect(*controls["right_fs"], 180.0, 180.0),
    }

    lines = [
        "#pragma once",
        "",
        "#include <JuceHeader.h>",
        "",
        "// Auto-generated by tools/generate_ui_assets.py. Do not edit manually.",
        "namespace PedalLayout",
        "{",
        f"    inline constexpr int canvasWidth   = {CANVAS_W};",
        f"    inline constexpr int canvasHeight  = {CANVAS_H};",
        f"    inline constexpr int defaultWidth  = {DEFAULT_W};",
        f"    inline constexpr int defaultHeight = {DEFAULT_H};",
        "",
        "    struct ControlRect",
        "    {",
        "        float cx;",
        "        float cy;",
        "        float w;",
        "        float h;",
        "",
        "        juce::Rectangle<int> toBounds (juce::Rectangle<int> area) const noexcept",
        "        {",
        "            const float afW = static_cast<float> (area.getWidth());",
        "            const float afH = static_cast<float> (area.getHeight());",
        "            const int bw = juce::roundToInt (w * afW);",
        "            const int bh = juce::roundToInt (h * afH);",
        "            const int bx = area.getX() + juce::roundToInt (cx * afW - static_cast<float> (bw) * 0.5f);",
        "            const int by = area.getY() + juce::roundToInt (cy * afH - static_cast<float> (bh) * 0.5f);",
        "            return { bx, by, bw, bh };",
        "        }",
        "    };",
        "",
        "    struct KnobParallax",
        "    {",
        "        float dx;",
        "        float dy;",
        "    };",
        "",
    ]
    for name, (cx, cy, w, h) in entries.items():
        lines.append(
            f"    inline constexpr ControlRect {name:<20s} {{ {cx:.6f}f, {cy:.6f}f, {w:.6f}f, {h:.6f}f }};"
        )
    lines.append("")
    for name in KNOB_BASE_POSITIONS:
        px_dx, px_dy = knob_parallax_px(name)
        nx = px_dx / KNOB_BOX_SIZE
        ny = px_dy / KNOB_BOX_SIZE
        lines.append(
            f"    inline constexpr KnobParallax {name + 'Parallax':<20s} {{ {nx:.6f}f, {ny:.6f}f }};"
        )
    lines.append("} // namespace PedalLayout")
    lines.append("")

    LAYOUT_HEADER_PATH.write_text("\n".join(lines), encoding="utf-8")


def compose_qa_preview(controls: dict[str, tuple[float, float]]) -> None:
    """Create a comprehensive visual QA sheet with side-by-side real macro photo comparisons."""
    real_photo = Image.open(SOURCE_IMAGE_PATH).convert("RGB")
    chassis = Image.open(OUT_UI_DIR / "pedal_chassis.png").convert("RGBA")
    knob_sh = Image.open(OUT_UI_DIR / "knob_shadow.png").convert("RGBA")
    knob_strip = Image.open(OUT_UI_DIR / "knob_skirt_strip.png").convert("RGBA")
    knob_wall = Image.open(OUT_UI_DIR / "knob_wall_strip.png").convert("RGBA")
    knob_cap = Image.open(OUT_UI_DIR / "knob_cap.png").convert("RGBA")
    knob_lit = Image.open(OUT_UI_DIR / "knob_lighting.png").convert("RGBA")
    tog_up = Image.open(OUT_UI_DIR / "toggle_up.png").convert("RGBA")
    tog_dn = Image.open(OUT_UI_DIR / "toggle_down.png").convert("RGBA")
    g_on = Image.open(OUT_UI_DIR / "led_green_on.png").convert("RGBA")
    g_off = Image.open(OUT_UI_DIR / "led_green_off.png").convert("RGBA")
    r_on = Image.open(OUT_UI_DIR / "led_red_on.png").convert("RGBA")
    r_off = Image.open(OUT_UI_DIR / "led_red_off.png").convert("RGBA")
    fs_l_pr = Image.open(OUT_UI_DIR / "footswitch_left_pressed.png").convert("RGBA")
    fs_r_pr = Image.open(OUT_UI_DIR / "footswitch_right_pressed.png").convert("RGBA")

    def paste_centered(base: Image.Image, sprite: Image.Image, px: float, py: float, size: tuple[int, int]) -> None:
        rs = sprite.resize(size, Image.Resampling.LANCZOS)
        x = int(round(PAD_X + px - size[0] * 0.5))
        y = int(round(PAD_TOP + py - size[1] * 0.5))
        base.alpha_composite(rs, dest=(x, y))

    def paste_knob(base: Image.Image, name: str, angle_deg: float) -> None:
        px, py = KNOB_BASE_POSITIONS[name]
        dx, dy = knob_parallax_px(name)
        box = int(round(KNOB_BOX_SIZE))
        paste_centered(base, knob_sh, px, py, (box, box))
        wrapped = math.radians(angle_deg) % (2.0 * math.pi / 64.0)
        frame_idx = min(11, int(math.floor((wrapped / (2.0 * math.pi / 64.0)) * 12.0)))
        fw = knob_strip.width
        wall_frame = knob_wall.crop((0, frame_idx * fw, fw, (frame_idx + 1) * fw))
        shift_len = math.hypot(dx, dy)
        if shift_len > 0.25:
            steps = max(4, min(20, int(math.ceil(shift_len * 2.0))))
            wf_arr = np.array(wall_frame, dtype=np.float64)
            for i in range(steps):
                t = i / float(steps)
                taper = 1.036 - 0.036 * t
                ao_darken = 0.34 * (1.0 - t) * (1.0 - t)
                sl_arr = wf_arr.copy()
                sl_arr[:, :, :3] *= (1.0 - ao_darken)
                sl_im = Image.fromarray(np.clip(np.round(sl_arr), 0, 255).astype(np.uint8), mode="RGBA")
                sz = max(1, int(round(KNOB_BOX_SIZE * taper)))
                paste_centered(base, sl_im, px + t * dx, py + t * dy, (sz, sz))
        skirt_frame = knob_strip.crop((0, frame_idx * fw, fw, (frame_idx + 1) * fw))
        paste_centered(base, skirt_frame, px + dx, py + dy, (box, box))
        rot_cap = knob_cap.rotate(-angle_deg, resample=Image.Resampling.BICUBIC)
        paste_centered(base, rot_cap, px + dx, py + dy, (box, box))
        paste_centered(base, knob_lit, px + dx, py + dy, (box, box))

    # Scene A: Active + Crossover ON + varied angles + Toggle UP
    scene_a = chassis.copy()
    for name, ang in [
        ("volume", 60.0), ("blend", 0.0), ("tone", -45.0),
        ("sustain", 120.0), ("gate", -150.0), ("hpf", 0.0), ("lpf", 40.0),
    ]:
        paste_knob(scene_a, name, ang)
    paste_centered(scene_a, tog_up, *controls["toggle"], (120, 150))
    paste_centered(scene_a, g_on, *controls["green_led"], (192, 192))
    paste_centered(scene_a, r_on, *controls["red_led"], (192, 192))

    # Scene B: Bypassed + Crossover OFF + all at 12:00 + Toggle DOWN + Footswitches Pressed
    scene_b = chassis.copy()
    for name in ["volume", "blend", "tone", "sustain", "gate", "hpf", "lpf"]:
        paste_knob(scene_b, name, 0.0)
    paste_centered(scene_b, tog_dn, *controls["toggle"], (120, 150))
    paste_centered(scene_b, g_off, *controls["green_led"], (192, 192))
    paste_centered(scene_b, r_off, *controls["red_led"], (192, 192))
    paste_centered(scene_b, fs_l_pr, *controls["left_fs"], (180, 180))
    paste_centered(scene_b, fs_r_pr, *controls["right_fs"], (180, 180))

    # Build QA sheet: top row = Scene A & Scene B, bottom rows = real vs rendered macro comparisons
    sheet = Image.new("RGB", (1660, 1120), (14, 16, 18))
    sheet.paste(scene_a.convert("RGB").resize((810, 684), Image.Resampling.LANCZOS), (10, 10))
    sheet.paste(scene_b.convert("RGB").resize((810, 684), Image.Resampling.LANCZOS), (840, 10))

    def crop_canvas(im: Image.Image, cx: float, cy: float, half_span: int, out_sz: int) -> Image.Image:
        x0 = max(0, int(round(PAD_X + cx - half_span)))
        y0 = max(0, int(round(PAD_TOP + cy - half_span)))
        return im.convert("RGB").crop((x0, y0, x0 + half_span * 2, y0 + half_span * 2)).resize(
            (out_sz, out_sz), Image.Resampling.LANCZOS
        )

    def crop_real(cx: float, cy: float, half_span: int, out_sz: int) -> Image.Image:
        x0 = max(0, int(round(cx - half_span)))
        y0 = max(0, int(round(cy - half_span)))
        return real_photo.crop((x0, y0, x0 + half_span * 2, y0 + half_span * 2)).resize(
            (out_sz, out_sz), Image.Resampling.LANCZOS
        )

    # Bottom row (y=730, 260x260 crops): side-by-side macro checks!
    macro_crops = [
        ("REAL PHOTO TONE", crop_real(*controls["tone"], 108, 250)),
        ("RENDERED 12:00", crop_canvas(scene_b, *controls["tone"], 108, 250)),
        ("RENDERED ROTATED", crop_canvas(scene_a, *controls["sustain"], 108, 250)),
        ("REAL LEDS (ON)", crop_real(744.0, 881.0, 260, 250)),
        ("RENDERED LEDS ON", crop_canvas(scene_a, 744.0, 881.0, 260, 250)),
        ("RENDERED LEDS OFF", crop_canvas(scene_b, 744.0, 881.0, 260, 250)),
    ]
    for i, (lbl, im) in enumerate(macro_crops):
        x = 15 + i * 270
        sheet.paste(im, (x, 740))
        ImageDraw.Draw(sheet).text((x + 4, 718), lbl, fill=(195, 225, 180))

    # Footswitch UP vs PRESSED strip along very bottom (y=1005..1110 -> 100x100 each)
    fs_crops = [
        ("L-FS UP", crop_canvas(scene_a, *controls["left_fs"], 80, 96)),
        ("L-FS DOWN", crop_canvas(scene_b, *controls["left_fs"], 80, 96)),
        ("R-FS UP", crop_canvas(scene_a, *controls["right_fs"], 80, 96)),
        ("R-FS DOWN", crop_canvas(scene_b, *controls["right_fs"], 80, 96)),
    ]
    for i, (lbl, im) in enumerate(fs_crops):
        x = 20 + i * 165
        sheet.paste(im, (x, 1012))
        ImageDraw.Draw(sheet).text((x + 102, 1052), lbl, fill=(210, 210, 215))

    draw = ImageDraw.Draw(sheet)
    draw.text((20, 696), "SCENE A: Active + Crossover ON + Varied Knobs + Toggle UP", fill=(180, 230, 160))
    draw.text((850, 696), "SCENE B: Bypassed + LEDs OFF + Toggle DOWN + Footswitches Pressed", fill=(210, 210, 215))

    QA_PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(QA_PREVIEW_PATH)
    print(f"Saved QA preview -> {QA_PREVIEW_PATH}")


def render_default_assembled_preview(chassis_img: Image.Image, controls: dict[str, tuple[float, float]], out_path: Path) -> None:
    """Assemble a clean default-state pedal preview (VOLUME 70%, GATE 0%, Red LED ON) onto chassis_img."""
    base = chassis_img.convert("RGBA")
    knob_sh = Image.open(OUT_UI_DIR / "knob_shadow.png").convert("RGBA")
    knob_strip = Image.open(OUT_UI_DIR / "knob_skirt_strip.png").convert("RGBA")
    knob_wall = Image.open(OUT_UI_DIR / "knob_wall_strip.png").convert("RGBA")
    knob_cap = Image.open(OUT_UI_DIR / "knob_cap.png").convert("RGBA")
    knob_lit = Image.open(OUT_UI_DIR / "knob_lighting.png").convert("RGBA")
    tog_dn = Image.open(OUT_UI_DIR / "toggle_down.png").convert("RGBA")
    g_off = Image.open(OUT_UI_DIR / "led_green_off.png").convert("RGBA")
    r_on = Image.open(OUT_UI_DIR / "led_red_on.png").convert("RGBA")

    def paste_centered(sprite: Image.Image, px: float, py: float, size: tuple[int, int]) -> None:
        rs = sprite.resize(size, Image.Resampling.LANCZOS)
        x = int(round(PAD_X + px - size[0] * 0.5))
        y = int(round(PAD_TOP + py - size[1] * 0.5))
        base.alpha_composite(rs, dest=(x, y))

    def paste_knob(name: str, angle_deg: float) -> None:
        px, py = KNOB_BASE_POSITIONS[name]
        dx, dy = knob_parallax_px(name)
        box = int(round(KNOB_BOX_SIZE))
        paste_centered(knob_sh, px, py, (box, box))
        wrapped = math.radians(angle_deg) % (2.0 * math.pi / 64.0)
        frame_idx = min(11, int(math.floor((wrapped / (2.0 * math.pi / 64.0)) * 12.0)))
        fw = knob_strip.width
        wall_frame = knob_wall.crop((0, frame_idx * fw, fw, (frame_idx + 1) * fw))
        shift_len = math.hypot(dx, dy)
        if shift_len > 0.25:
            steps = max(4, min(20, int(math.ceil(shift_len * 2.0))))
            wf_arr = np.array(wall_frame, dtype=np.float64)
            for i in range(steps):
                t = i / float(steps)
                taper = 1.036 - 0.036 * t
                ao_darken = 0.34 * (1.0 - t) * (1.0 - t)
                sl_arr = wf_arr.copy()
                sl_arr[:, :, :3] *= (1.0 - ao_darken)
                sl_im = Image.fromarray(np.clip(np.round(sl_arr), 0, 255).astype(np.uint8), mode="RGBA")
                sz = max(1, int(round(KNOB_BOX_SIZE * taper)))
                paste_centered(sl_im, px + t * dx, py + t * dy, (sz, sz))
        skirt_frame = knob_strip.crop((0, frame_idx * fw, fw, (frame_idx + 1) * fw))
        paste_centered(skirt_frame, px + dx, py + dy, (box, box))
        rot_cap = knob_cap.rotate(-angle_deg, resample=Image.Resampling.BICUBIC)
        paste_centered(rot_cap, px + dx, py + dy, (box, box))
        paste_centered(knob_lit, px + dx, py + dy, (box, box))

    # Default parameter angles (300 deg sweep: -150..+150): VOLUME 70% (+60), GATE 0% (-150), others 12:00
    for name, ang in [
        ("volume", 60.0), ("blend", 0.0), ("tone", 0.0),
        ("sustain", 0.0), ("gate", -150.0), ("hpf", 0.0), ("lpf", 0.0),
    ]:
        paste_knob(name, ang)
    paste_centered(tog_dn, *controls["toggle"], (120, 150))
    paste_centered(g_off, *controls["green_led"], (192, 192))
    paste_centered(r_on, *controls["red_led"], (192, 192))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    # Save crisp 1290x1077 preview (1.5x logical) so artifact carousel stays fast and sharp
    base.convert("RGB").resize((1290, 1077), Image.Resampling.LANCZOS).save(out_path, optimize=True)
    print(f"Saved preview -> {out_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate DeluxeBassFuzz UI hardware assets.")
    parser.add_argument("--bg-image", type=Path, default=None, help="Optional hardwood/studio background image path.")
    parser.add_argument("--bg-rotate90", action="store_true", help="Rotate background image 90 degrees counter-clockwise.")
    parser.add_argument("--oil-warmth", action="store_true", help="Apply hand-rubbed walnut oil grade for unfinished veneer scans.")
    parser.add_argument("--preview-only", type=Path, default=None, help="Only render assembled default preview to this path without updating Assets/ui or PedalLayout.h.")
    args = parser.parse_args()

    bg_path = args.bg_image
    if bg_path is None and DEFAULT_BG_PATH.exists():
        bg_path = DEFAULT_BG_PATH

    OUT_UI_DIR.mkdir(parents=True, exist_ok=True)

    photo = ensure_source_image()
    arr = np.array(photo).astype(np.float64)
    controls = detect_controls(arr)
    print("Detected controls (1500x1247 photo space):")
    for k, (vx, vy) in controls.items():
        print(f"  {k:10s}: ({vx:7.2f}, {vy:7.2f})")

    defringed_rgb, pedal_alpha = extract_pedal_alpha_and_defringe(arr)
    cleaned_rgb = clean_plate_chassis(defringed_rgb, controls)
    chassis_img = composite_chassis_on_studio(
        cleaned_rgb,
        pedal_alpha,
        bg_image_path=bg_path,
        bg_rotate90=args.bg_rotate90,
        oil_warmth=args.oil_warmth,
    )

    if args.preview_only is not None:
        render_default_assembled_preview(chassis_img, controls, args.preview_only)
        return

    chassis_img.save(OUT_UI_DIR / "pedal_chassis.png", optimize=True)
    print(f"Saved {OUT_UI_DIR / 'pedal_chassis.png'} ({chassis_img.size})")

    knob_sh, knob_strip, knob_wall, knob_cap, knob_lit = generate_knob_layers()
    knob_sh.save(OUT_UI_DIR / "knob_shadow.png", optimize=True)
    knob_strip.save(OUT_UI_DIR / "knob_skirt_strip.png", optimize=True)
    knob_wall.save(OUT_UI_DIR / "knob_wall_strip.png", optimize=True)
    knob_cap.save(OUT_UI_DIR / "knob_cap.png", optimize=True)
    knob_lit.save(OUT_UI_DIR / "knob_lighting.png", optimize=True)
    old_body = OUT_UI_DIR / "knob_body.png"
    if old_body.exists():
        old_body.unlink()

    tog_dn, tog_up = generate_toggle_assets()
    tog_dn.save(OUT_UI_DIR / "toggle_down.png", optimize=True)
    tog_up.save(OUT_UI_DIR / "toggle_up.png", optimize=True)

    for fname, im in generate_led_assets().items():
        im.save(OUT_UI_DIR / fname, optimize=True)

    fs_l_pr, fs_r_pr = generate_footswitch_pressed_overlays(defringed_rgb, controls)
    fs_l_pr.save(OUT_UI_DIR / "footswitch_left_pressed.png", optimize=True)
    fs_r_pr.save(OUT_UI_DIR / "footswitch_right_pressed.png", optimize=True)

    write_layout_header(controls)
    print(f"Saved {LAYOUT_HEADER_PATH}")

    compose_qa_preview(controls)


if __name__ == "__main__":
    main()
