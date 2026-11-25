#!/usr/bin/env python3
"""Compare ngspice reference results with the DSP render (SPICE-vs-DSP validation).

Inputs  (produced by tools/run_spice.py and the DeluxeBassFuzzRender target):
  tests/ReferenceData/spice/ac_<name>.csv, tran_<name>.csv
  tests/ReferenceData/dsp/ir_<name>.csv,  tran_<name>.csv
Outputs:
  docs/validation/*.png                plots
  docs/validation/results.json         all metrics + check verdicts
  docs/validation.md                   summary section (between AUTO markers)

Usage:
  python tools/compare_spice.py                 # full report
  python tools/compare_spice.py --check tone    # exit 0/1 for one check (used by ctest)

Checks (thresholds are targets, not tuned to make the current model pass):
  tone        tone stack magnitude within 1 dB of SPICE, 20 Hz-20 kHz, all 5 settings
  input_ac    Q1+sustain small-signal magnitude within 3 dB, 50 Hz-10 kHz
  clip_ac     clipping stage small-signal magnitude within 3 dB, 50 Hz-10 kHz
  output_ac   Q4 small-signal magnitude within 3 dB, 50 Hz-10 kHz
  core_ac     full core small-signal magnitude within 3 dB, 50 Hz-10 kHz
  clip_tran   clipping stage: fundamental gain within 2 dB and THD within 3 dB
  core_tran   full core: fundamental gain within 2 dB and THD within 3 dB
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "tests" / "ReferenceData"
SPICE = REF / "spice"
DSP = REF / "dsp"
OUTDIR = ROOT / "docs" / "validation"
MD = ROOT / "docs" / "validation.md"
RESULTS = OUTDIR / "results.json"

N_HARM = 9
TONE_TOL_DB = 1.0
AC_TOL_DB = 3.0
GAIN_TOL_DB = 2.0
THD_TOL_DB = 3.0
HARM_FLOOR_DBC = -100.0
HARM_SIGNIFICANT_DBC = -60.0


def load_csv(path):
    return np.genfromtxt(path, delimiter=",", names=True)


def cfg():
    return json.loads((REF / "cases.json").read_text())


# ----------------------------------------------------------------------------- AC
def dsp_response(name, fs, freqs):
    ir = load_csv(DSP / f"ir_{name}.csv")["h"]
    H = np.fft.rfft(ir)
    f = np.fft.rfftfreq(len(ir), 1.0 / fs)
    mag = 20 * np.log10(np.maximum(np.abs(H), 1e-12))
    return np.interp(np.log(freqs), np.log(np.maximum(f, 1e-9)), mag)


def ac_case(name, fs, band):
    sp = load_csv(SPICE / f"ac_{name}.csv")
    f = sp["freq_hz"]
    spice_db = sp["mag_db"]
    dsp_db = dsp_response(name, fs, f)
    err = dsp_db - spice_db
    sel = (f >= band[0]) & (f <= band[1])
    offset = float(np.mean(err[sel]))
    shape = err - offset
    return {
        "freq": f, "spice": spice_db, "dsp": dsp_db, "err": err,
        "band": band,
        "max_abs_db": float(np.max(np.abs(err[sel]))),
        "mean_abs_db": float(np.mean(np.abs(err[sel]))),
        "mean_offset_db": offset,
        "shape_max_abs_db": float(np.max(np.abs(shape[sel]))),
        "spice_1k_db": float(np.interp(1000, f, spice_db)),
        "dsp_1k_db": float(np.interp(1000, f, dsp_db)),
    }


# --------------------------------------------------------------------------- TRAN
def harmonics(x, fs, f0):
    n = len(x)
    X = np.fft.rfft(x - np.mean(x)) * 2.0 / n
    bin0 = f0 * n / fs
    amps = []
    for k in range(1, N_HARM + 1):
        b = int(round(k * bin0))
        lo, hi = max(b - 1, 0), min(b + 2, len(X))
        amps.append(float(np.max(np.abs(X[lo:hi]))) if b < len(X) else 0.0)
    amps = np.array(amps)
    thd = float(np.sqrt(np.sum(amps[1:] ** 2)) / max(amps[0], 1e-15))
    return amps, thd


def aligned_waveform_error(a, b):
    """RMS error between unit-RMS waveforms after best circular alignment (lag search)."""
    a = (a - a.mean()) / max(a.std(), 1e-15)
    b = (b - b.mean()) / max(b.std(), 1e-15)
    c = np.fft.irfft(np.fft.rfft(a) * np.conj(np.fft.rfft(b)), len(a))
    lag = int(np.argmax(c))
    b2 = np.roll(b, lag)
    return float(np.sqrt(np.mean((a - b2) ** 2))), lag, b2


def tran_case(name, fs, f0, window_s):
    sp = load_csv(SPICE / f"tran_{name}.csv")
    ds = load_csv(DSP / f"tran_{name}.csv")
    n = int(round(window_s * fs))
    # analyse the last `window_s` seconds (integer number of cycles for 100 Hz / 1 kHz)
    sp_in, sp_out = sp["vin"][-n:], sp["vout"][-n:]
    ds_in, ds_out = ds["vin"][-n:], ds["vout"][-n:]
    a_in, _ = harmonics(sp_in, fs, f0)
    a_sp, thd_sp = harmonics(sp_out, fs, f0)
    a_ds, thd_ds = harmonics(ds_out, fs, f0)
    gain_sp = 20 * np.log10(a_sp[0] / a_in[0])
    gain_ds = 20 * np.log10(a_ds[0] / a_in[0])
    # harmonic levels relative to the fundamental, floored at HARM_FLOOR_DBC (numerical noise)
    dbc = lambda a: np.maximum(20 * np.log10(np.maximum(a[1:], 1e-15) / a[0]), HARM_FLOOR_DBC)
    wave_err, lag, ds_aligned = aligned_waveform_error(sp_out, ds_out)
    return {
        "f0": f0,
        "gain_spice_db": float(gain_sp), "gain_dsp_db": float(gain_ds),
        "gain_delta_db": float(gain_ds - gain_sp),
        "thd_spice_pct": 100 * thd_sp, "thd_dsp_pct": 100 * thd_ds,
        "thd_delta_db": float(20 * np.log10(max(thd_ds, 1e-9) / max(thd_sp, 1e-9))),
        "harm_spice_dbc": dbc(a_sp).tolist(), "harm_dsp_dbc": dbc(a_ds).tolist(),
        "harm_delta_db": (dbc(a_ds) - dbc(a_sp)).tolist(),
        # only harmonics that matter (above HARM_SIGNIFICANT_DBC in either result) are scored
        "harm_significant": ((dbc(a_ds) > HARM_SIGNIFICANT_DBC) | (dbc(a_sp) > HARM_SIGNIFICANT_DBC)).tolist(),
        "wave_rms_err": wave_err, "align_lag": lag,
        "_t": np.arange(n) / fs, "_sp": sp_out, "_ds": ds_out, "_ds_al": ds_aligned,
    }


def tran_names(c):
    for g in c["tran"]:
        for s in g.get("sustains", [None]):
            for f in g["freqs"]:
                for a in g["amps"]:
                    tag = g["block"] + (f"_s{s:.2f}" if s is not None else "")
                    yield g["block"], f"{tag}_f{int(f)}_a{a:g}", f, a, s


# -------------------------------------------------------------------------- PLOTS
def plots(ac, tran):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    OUTDIR.mkdir(parents=True, exist_ok=True)
    made = []

    def ac_plot(names, title, fname):
        fig, (ax, ax2) = plt.subplots(2, 1, figsize=(9, 7), sharex=True, gridspec_kw={"height_ratios": [3, 1.3]})
        for i, n in enumerate(names):
            r = ac[n]
            col = f"C{i}"
            ax.semilogx(r["freq"], r["spice"], "-", color=col, lw=2, alpha=0.45, label=f"{n} SPICE")
            ax.semilogx(r["freq"], r["dsp"], "--", color=col, lw=1.2, label=f"{n} DSP")
            ax2.semilogx(r["freq"], r["err"], color=col, lw=1)
        ax.set_ylabel("magnitude (dB)")
        ax.set_title(title)
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=7, ncol=2)
        ax2.set_ylabel("DSP - SPICE (dB)")
        ax2.set_xlabel("frequency (Hz)")
        ax2.grid(True, which="both", alpha=0.3)
        fig.tight_layout()
        p = OUTDIR / fname
        fig.savefig(p, dpi=110)
        plt.close(fig)
        made.append(p.name)

    ac_plot([n for n in ac if n.startswith("tone_")], "Tone stack: SPICE (solid) vs DSP (dashed)", "tone_stack_ac.png")
    ac_plot([n for n in ac if n.startswith("input_")] + ["clip_ac"] + [n for n in ac if n.startswith("output_")],
            "Linear stages, small signal: SPICE vs DSP", "stages_ac.png")
    ac_plot([n for n in ac if n.startswith("core_")], "Full core small-signal (tone noon): SPICE vs DSP", "core_ac.png")

    def wave_plot(name, fname):
        r = tran[name]
        t, n = r["_t"], int(round(3 * 192000 / r["f0"]))  # 3 cycles
        fig, ax = plt.subplots(figsize=(9, 4))
        ax.plot(t[:n] * 1e3, r["_sp"][:n], lw=2, alpha=0.6, label="SPICE")
        ax.plot(t[:n] * 1e3, r["_ds"][:n], "--", lw=1.2, label="DSP")
        ax.set_xlabel("time (ms, analysis window)")
        ax.set_ylabel("output (V / DSP units)")
        ax.set_title(f"{name}: gain {r['gain_spice_db']:.1f} vs {r['gain_dsp_db']:.1f} dB, "
                     f"THD {r['thd_spice_pct']:.0f}% vs {r['thd_dsp_pct']:.0f}%")
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        p = OUTDIR / fname
        fig.savefig(p, dpi=110)
        plt.close(fig)
        made.append(p.name)

    wave_plot("core_s0.50_f100_a0.2", "core_wave_s050_100hz.png")
    wave_plot("clip_f1000_a0.05", "clip_wave_1khz.png")

    # harmonic comparison for a representative set
    keys = ["clip_f100_a0.05", "core_s0.25_f100_a0.2", "core_s0.50_f100_a0.2", "core_s1.00_f100_a0.2"]
    fig, axes = plt.subplots(1, len(keys), figsize=(14, 3.8), sharey=True)
    for ax, k in zip(axes, keys):
        r = tran[k]
        h = np.arange(2, N_HARM + 1)
        ax.bar(h - 0.2, r["harm_spice_dbc"], width=0.4, label="SPICE")
        ax.bar(h + 0.2, r["harm_dsp_dbc"], width=0.4, label="DSP")
        ax.set_title(k, fontsize=8)
        ax.set_xlabel("harmonic")
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("dBc")
    axes[0].legend(fontsize=7)
    fig.tight_layout()
    p = OUTDIR / "harmonics.png"
    fig.savefig(p, dpi=110)
    plt.close(fig)
    made.append(p.name)

    # THD vs amplitude, core
    fig, ax = plt.subplots(figsize=(8, 4))
    for i, s in enumerate(["0.25", "0.50", "1.00"]):
        for ls, f in (("-", 100), (":", 1000)):
            ks = [k for k in tran if k.startswith(f"core_s{s}_f{f}_")]
            amps = [float(k.split("_a")[1]) for k in ks]
            o = np.argsort(amps)
            ax.semilogx(np.array(amps)[o], np.array([tran[k]["thd_spice_pct"] for k in ks])[o], ls, color=f"C{i}", marker="o",
                        label=f"SPICE s={s} {f}Hz")
            ax.semilogx(np.array(amps)[o], np.array([tran[k]["thd_dsp_pct"] for k in ks])[o], ls, color=f"C{i}", marker="x",
                        alpha=0.6, label=f"DSP s={s} {f}Hz")
    ax.set_xlabel("input amplitude (V peak)")
    ax.set_ylabel("THD (%)")
    ax.set_title("Full core THD (harmonics 2-9): SPICE (o) vs DSP (x)")
    ax.grid(alpha=0.3, which="both")
    ax.legend(fontsize=6, ncol=3)
    fig.tight_layout()
    p = OUTDIR / "core_thd_vs_amplitude.png"
    fig.savefig(p, dpi=110)
    plt.close(fig)
    made.append(p.name)
    return made


# ------------------------------------------------------------------------- REPORT
def evaluate():
    c = cfg()
    fs = float(c["sampleRate"])
    ac = {}
    for case in c["ac"]:
        n = case["name"]
        band = (20.0, 20000.0) if case["block"] == "tone" else (50.0, 10000.0)
        ac[n] = ac_case(n, fs, band)
    tran = {}
    for block, n, f, a, s in tran_names(c):
        tran[n] = tran_case(n, fs, f, float(c["analysisSeconds"]))
        tran[n]["block"], tran[n]["amp"], tran[n]["sustain"] = block, a, s

    def ac_check(prefix, tol):
        names = [n for n in ac if n.startswith(prefix)]
        worst = max(ac[n]["max_abs_db"] for n in names)
        return {"pass": bool(worst <= tol), "worst_max_abs_db": worst, "tolerance_db": tol, "cases": names}

    def tran_check(block):
        names = [n for n in tran if tran[n]["block"] == block]
        wg = max(abs(tran[n]["gain_delta_db"]) for n in names)
        wt = max(abs(tran[n]["thd_delta_db"]) for n in names)
        return {"pass": bool(wg <= GAIN_TOL_DB and wt <= THD_TOL_DB),
                "worst_gain_delta_db": wg, "worst_thd_delta_db": wt,
                "gain_tolerance_db": GAIN_TOL_DB, "thd_tolerance_db": THD_TOL_DB, "cases": names}

    checks = {
        "tone": ac_check("tone_", TONE_TOL_DB),
        "input_ac": ac_check("input_", AC_TOL_DB),
        "clip_ac": ac_check("clip_ac", AC_TOL_DB),
        "output_ac": ac_check("output_", AC_TOL_DB),
        "core_ac": ac_check("core_", AC_TOL_DB),
        "clip_tran": tran_check("clip"),
        "core_tran": tran_check("core"),
    }
    return ac, tran, checks


def strip(d):
    return {k: v for k, v in d.items() if not k.startswith("_") and k not in ("freq", "spice", "dsp", "err")}


def write_md(ac, tran, checks, pngs):
    L = []
    L.append("<!-- AUTO-GENERATED by tools/compare_spice.py: do not edit between these markers -->")
    L.append("## SPICE vs DSP results")
    L.append("")
    L.append("Reference: ngspice 47 decks generated by `tools/run_spice.py` from `tests/ReferenceData/spice/bmp_parts.lib` "
             "(2N5088 / 1N914 models). DSP: `DeluxeBassFuzzRender` running the plugin's DSP blocks at 192 kHz. "
             "AC = small-signal magnitude; DSP side from a 1e-6 impulse response. "
             "Transient = last 100 ms of a 200 ms sine; harmonics 2-9 (floored at -100 dBc; "
             "harmonic deltas only scored where either side is above -60 dBc). THD above 100% means the "
             "harmonics are larger than the fundamental.")
    L.append("")
    L.append("### Checks")
    L.append("")
    L.append("| check | verdict | worst | tolerance |")
    L.append("|---|---|---|---|")
    for k, v in checks.items():
        if "worst_max_abs_db" in v:
            L.append(f"| {k} | {'PASS' if v['pass'] else 'FAIL'} | {v['worst_max_abs_db']:.2f} dB max error | {v['tolerance_db']} dB |")
        else:
            L.append(f"| {k} | {'PASS' if v['pass'] else 'FAIL'} | gain {v['worst_gain_delta_db']:.1f} dB, THD {v['worst_thd_delta_db']:.1f} dB "
                     f"| {v['gain_tolerance_db']} dB / {v['thd_tolerance_db']} dB |")
    L.append("")
    L.append("### Small-signal magnitude error (DSP - SPICE)")
    L.append("")
    L.append("Band: 20 Hz-20 kHz for the tone stack, 50 Hz-10 kHz otherwise. *Offset* = mean error (gain mismatch); "
             "*shape* = max error after removing the offset.")
    L.append("")
    L.append("| case | max abs (dB) | mean abs (dB) | offset (dB) | shape max (dB) | SPICE @1k | DSP @1k |")
    L.append("|---|---|---|---|---|---|---|")
    for n, r in ac.items():
        L.append(f"| {n} | {r['max_abs_db']:.2f} | {r['mean_abs_db']:.2f} | {r['mean_offset_db']:+.2f} | "
                 f"{r['shape_max_abs_db']:.2f} | {r['spice_1k_db']:.1f} | {r['dsp_1k_db']:.1f} |")
    L.append("")
    L.append("### Transient (fundamental gain, THD, harmonics)")
    L.append("")
    L.append("| case | gain SPICE | gain DSP | delta | THD SPICE | THD DSP | THD delta | max harmonic delta (H2-H9 above -60 dBc) | waveform RMS err |")
    L.append("|---|---|---|---|---|---|---|---|---|")
    for n, r in tran.items():
        hd = np.where(np.array(r["harm_significant"]), np.array(r["harm_delta_db"]), 0.0)
        L.append(f"| {n} | {r['gain_spice_db']:.1f} dB | {r['gain_dsp_db']:.1f} dB | {r['gain_delta_db']:+.1f} | "
                 f"{r['thd_spice_pct']:.1f}% | {r['thd_dsp_pct']:.1f}% | {r['thd_delta_db']:+.1f} dB | "
                 f"{hd[np.argmax(np.abs(hd))]:+.1f} dB (H{int(np.argmax(np.abs(hd))) + 2}) | {r['wave_rms_err']:.2f} |")
    L.append("")
    L.append("### Plots")
    L.append("")
    for p in pngs:
        L.append(f"![{p}](validation/{p})")
    L.append("")
    L.append("<!-- END AUTO-GENERATED -->")
    block = "\n".join(L)

    start, end = "<!-- AUTO-GENERATED by tools/compare_spice.py", "<!-- END AUTO-GENERATED -->"
    text = MD.read_text(encoding="utf-8") if MD.exists() else "# Validation\n"
    if start in text and end in text:
        pre = text[:text.index(start)]
        post = text[text.index(end) + len(end):]
        text = pre + block + post
    else:
        text = text.rstrip() + "\n\n" + block + "\n"
    MD.write_text(text, encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", default=None, help="name of one check; exit code reflects pass/fail")
    ap.add_argument("--no-plots", action="store_true")
    args = ap.parse_args()

    ac, tran, checks = evaluate()

    if args.check:
        v = checks[args.check]
        print(json.dumps({args.check: v}, indent=1))
        sys.exit(0 if v["pass"] else 1)

    pngs = [] if args.no_plots else plots(ac, tran)
    OUTDIR.mkdir(parents=True, exist_ok=True)
    RESULTS.write_text(json.dumps({"checks": checks,
                                   "ac": {k: strip(v) for k, v in ac.items()},
                                   "tran": {k: strip(v) for k, v in tran.items()}}, indent=1))
    write_md(ac, tran, checks, pngs)
    for k, v in checks.items():
        print(f"{k:10s} {'PASS' if v['pass'] else 'FAIL'}")
    print(f"wrote {RESULTS}, {MD}, {len(pngs)} plots")


if __name__ == "__main__":
    main()
