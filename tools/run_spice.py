#!/usr/bin/env python3
"""Generate and run the ngspice reference decks for the SPICE-vs-DSP comparison.

Reads tests/ReferenceData/cases.json, writes decks to
tests/ReferenceData/spice/decks/ and CSV results to tests/ReferenceData/spice/.

  AC CSV:   freq_hz, mag_db, phase_deg      (output / input, small signal)
  TRAN CSV: time_s, vin, vout               (uniformly resampled at sampleRate)

Usage: python tools/run_spice.py [--ngspice PATH] [--only SUBSTR]
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "tests" / "ReferenceData"
OUT = REF / "spice"
DECKS = OUT / "decks"
LIB = REF / "spice" / "bmp_parts.lib"

DEFAULT_NGSPICE = Path(os.environ.get("USERPROFILE", "")) / "tools" / "ngspice" / "Spice64" / "bin" / "ngspice_con.exe"


def sustain_taper(s: float) -> float:
    """100k log (audio) pot: wiper fraction for knob position s (0..1); ~9% at noon."""
    return (10.0 ** (2.0 * s) - 1.0) / 99.0


def circuit(block: str, case: dict, cfg: dict) -> str:
    """Netlist body (between source and analysis) for one block. Input node 'in', output node 'out'.

    Block boundaries: every block is driven by an ideal source and its output node
    is loaded by the REAL downstream circuit (the rest of the pedal), so a block's
    output voltage is exactly what the next block sees in the full core. The DSP
    stages model their load with a linearised copy of the next stage's input.
    """
    tone = case.get("tone", cfg["toneDefault"])
    tone_d = cfg["toneDefault"]
    sus = case.get("sustain", 0.5)
    frac = sustain_taper(sus)
    # downstream chains (real circuit) used as loads
    after_q3 = (f"Xtl {{n}} twl BMP_TONE tone={tone_d}\n"
                "X4l twl c4l vcc BMP_Q4\nC13l c4l vl 100n\nRvl vl 0 100k\n")
    if block == "tone":
        # Tone stack alone, wiper unloaded (matches BmpToneStack, the unloaded model)
        return f"Xt in out BMP_TONE tone={tone}\n"
    if block == "input":
        # Q1 + sustain pot; the wiper is loaded by the real Q2 (-> Q3 -> tone -> Q4)
        return (f"X1 in c1 vcc BMP_Q1\n"
                f"Xs c1 out BMP_SUSTAIN frac={frac:.6g}\n"
                f"X2l out c2l vcc BMP_CLIP\n"
                f"X3l c2l c3l vcc BMP_CLIP\n" + after_q3.replace("{n}", "c3l"))
    if block == "clip":
        # One clipping stage (Q2); output = its collector, loaded by the real Q3 (-> tone -> Q4)
        return ("X2 in out vcc BMP_CLIP\n"
                "X3l out c3l vcc BMP_CLIP\n" + after_q3.replace("{n}", "c3l"))
    if block == "output":
        # Q4 alone from an ideal source + output coupling cap into the 100k volume pot at max
        return ("X4 in c4 vcc BMP_Q4\n"
                "C13 c4 out 100n\nRvol out 0 100k\n")
    if block == "toneout":
        # Tone stack -> Q4 -> volume: the tone stack wiper LOADED by Q4's input
        return (f"Xt in tw BMP_TONE tone={tone}\n"
                "X4 tw c4 vcc BMP_Q4\n"
                "C13 c4 out 100n\nRvol out 0 100k\n")
    if block == "core":
        # Full fuzz core: Q1 -> sustain -> Q2 -> Q3 -> tone -> Q4 -> volume (max)
        return (f"X1 in c1 vcc BMP_Q1\n"
                f"Xs c1 sw BMP_SUSTAIN frac={frac:.6g}\n"
                f"X2 sw c2 vcc BMP_CLIP\n"
                f"X3 c2 c3 vcc BMP_CLIP\n"
                f"Xt c3 tw BMP_TONE tone={tone}\n"
                f"X4 tw c4 vcc BMP_Q4\n"
                f"C13 c4 out 100n\nRvol out 0 100k\n")
    raise ValueError(block)


def deck_ac(name: str, case: dict, cfg: dict) -> str:
    csv = (OUT / f"ac_{name}.csv").as_posix()
    return f"""* AC {name}
.include "{LIB.as_posix()}"
Vcc vcc 0 DC 9
Vin in 0 DC 0 AC 1
{circuit(case['block'], case, cfg)}
.control
set wr_singlescale
set wr_vecnames
option numdgt=8
ac dec {cfg['acPointsPerDecade']} {cfg['acStartHz']} {cfg['acStopHz']}
let mag_db = db(v(out))
let phase_deg = cph(v(out)) * 180 / pi
wrdata {csv} mag_db phase_deg
quit
.endc
.end
"""


def deck_tran(name: str, block: str, case: dict, freq: float, amp: float, cfg: dict) -> str:
    fs = cfg["sampleRate"]
    T = cfg["tranSeconds"]
    csv = (OUT / f"tran_{name}.csv").as_posix()
    return f"""* TRAN {name}
.include "{LIB.as_posix()}"
.options reltol=1e-4 method=gear
Vcc vcc 0 DC 9
Vin in 0 DC 0 SIN(0 {amp} {freq})
{circuit(block, case, cfg)}
.control
set wr_singlescale
set wr_vecnames
option numdgt=8
tran {1.0/fs:.10g} {T} 0 {0.5/fs:.10g}
linearize v(in) v(out)
let vin = v(in)
let vout = v(out)
wrdata {csv} vin vout
quit
.endc
.end
"""


def tran_cases(cfg):
    for group in cfg["tran"]:
        block = group["block"]
        for s in group.get("sustains", [None]):
            for f in group["freqs"]:
                for a in group["amps"]:
                    case = {"block": block}
                    tag = block
                    if s is not None:
                        case["sustain"] = s
                        tag += f"_s{s:.2f}"
                    name = f"{tag}_f{int(f)}_a{a:g}"
                    yield name, block, case, f, a


def fix_csv(path: Path):
    """wrdata writes whitespace-separated columns with a header; convert to CSV."""
    lines = path.read_text().strip().splitlines()
    header = lines[0].split()
    rows = [",".join(l.split()) for l in lines[1:] if l.strip()]
    if header[0] == "frequency":
        header = ["freq_hz"] + header[1:]
    elif header[0] == "time":
        header = ["time_s"] + header[1:]
    path.write_text(",".join(header) + "\n" + "\n".join(rows) + "\n")


def run(ngspice: Path, deck_path: Path, csv: Path):
    if csv.exists():
        csv.unlink()
    r = subprocess.run([str(ngspice), "-b", str(deck_path)], capture_output=True, text=True)
    log = deck_path.with_suffix(".log")
    log.write_text(r.stdout + "\n--- stderr ---\n" + r.stderr)
    if not csv.exists():
        raise RuntimeError(f"ngspice produced no output for {deck_path.name}; see {log}")
    fix_csv(csv)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ngspice", default=str(DEFAULT_NGSPICE))
    ap.add_argument("--only", default="")
    args = ap.parse_args()

    ngspice = Path(args.ngspice)
    if not ngspice.exists():
        sys.exit(f"ngspice not found at {ngspice}")

    cfg = json.loads((REF / "cases.json").read_text())
    DECKS.mkdir(parents=True, exist_ok=True)

    jobs = []
    for case in cfg["ac"]:
        name = case["name"]
        jobs.append((f"ac_{name}", deck_ac(name, case, cfg), OUT / f"ac_{name}.csv"))
    for name, block, case, f, a in tran_cases(cfg):
        jobs.append((f"tran_{name}", deck_tran(name, block, case, f, a, cfg), OUT / f"tran_{name}.csv"))

    for tag, deck, csv in jobs:
        if args.only and args.only not in tag:
            continue
        p = DECKS / f"{tag}.cir"
        p.write_text(deck)
        print(f"ngspice {tag} ...", flush=True)
        run(ngspice, p, csv)
    print("done")


if __name__ == "__main__":
    main()
