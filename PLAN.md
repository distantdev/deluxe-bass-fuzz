# Plan: Replicate the Deluxe Bass Fuzz as a JUCE VST

Target product: **Deluxe Bass Fuzz** (version 11A, released January 2013).
This is the "famous bass fuzz deluxe" — a flagship bass fuzz, built on the classic
4-transistor Classic Bass Fuzz topology with blend, crossover, gate, and a −10 dB input pad.

---

## 1. How the pedal actually works (research summary)

### 1.1 The core: the 4-transistor Classic Bass Fuzz (Bob Myer's design)

The Deluxe Bass Fuzz's fuzz core is the **Deluxe Bass Fuzz** circuit, which circuit reference
describes as "a slightly lower gain, slightly less bassy version of a classic bass fuzz Big
Fuzz, with a brighter mid range." The classic bass fuzz itself is a vintage-voiced Classic Bass Fuzz:
lower gain, fatter bottom, brighter mids than the USA versions.

The Classic Bass Fuzz skeleton (from the 1972 "Triangle" schematic broken down by Jorge Pais) is
five blocks:

1. **Input stage (Q1)** — common-emitter BJT amp with series–shunt feedback. A Miller
   capacitor from collector to base forms a low-pass and prevents oscillation; the input
   coupling cap forms a high-pass. SUSTAIN controls how much signal is passed on to the
   clippers (i.e. drive into the distortion). This stage also buffers the high-impedance
   instrument signal.
2. **First clipping stage (Q2)** — near-identical common-emitter stage, but with **two
   anti-series diodes in the collector→base feedback loop**. This is *soft clipping*: the
   feedback diode pair shunts signal and rounds off the waveform.
3. **Second clipping stage (Q3)** — another copy, same feedback-diode soft clipping.
   Stacking two diode clippers in series is what makes the Classic Bass Fuzz a *fuzz* (hard,
   square-ish, compressed) rather than a mild overdrive.
4. **Passive tone stack** — a passive network that blends a low-pass and a high-pass
   filter through the 100k TONE pot. The two filters' cutoffs do not meet (LP ≈ 1808 Hz,
   HP ≈ 408 Hz), so the blend carves the signature **mid-scoop**. This stack is lossy

   > **Correction (build/test pass, validated):** the LP/HP labels above are swapped. The
   > 39k/10n section is the **low-pass** (bass) node at ≈408 Hz and the 3n9/22k section is
   > the **high-pass** (treble) node at ≈1.8 kHz (1855 Hz with 3n9, 1808 Hz with 4n). The
   > scoop exists *because* the LP corner sits below the HP corner. See Source/dsp/BmpToneStack.h.
   (several dB).
5. **Output/recovery stage (Q4)** — another common-emitter amp to recover the tone-stack
   loss, followed by the VOLUME pot.

Transistors are 2N5088-class NPN silicon (classic units used unmarked/KT3102 silicon).
Bass/classic variants use larger coupling caps (more low end) and slightly different
emitter/feedback values (lower gain, brighter mids) than the USA Fuzzs.

### 1.2 What the "Deluxe" adds on top (per the OEM manual + circuit reference)

- **Blend** — a **buffered dry path** is mixed against the fuzz path, letting bassists keep
  a solid fundamental under the fuzz. (The pedal also exposes a buffered 1/4" *Direct Out*
  that carries this dry signal.)
- **Crossover** (separate footswitch) — splits tone-sculpting into two bands:
  - **HPF knob**: a **2nd-order high-pass on the signal feeding the fuzz**, 125 Hz → 1.75 kHz.
  - **LPF knob**: a **2nd-order low-pass on the dry signal**, 60 Hz → 10 kHz.
  This is *not* a textbook complementary crossover — it's two independent filters (a
  "cocked-wah" style sculpting tool per circuit reference). When Crossover is off, both filters are
  bypassed.
- **Gate** — a noise gate on the fuzz signal to kill high-gain hiss/hum when not playing.
  OEM notes it can also be used as an effect (chopped, "sputtery" gated tones).
- **INPUT toggle** — 0 dB (normal) or **−10 dB** attenuation, for active-pickup basses.
- **DI output** — balanced XLR out with (per marketing copy) speaker emulation, plus the
  buffered dry 1/4" out. These are hardware I/O features; see §2 scope note.

### 1.3 Signal flow (block level)

```
in ──[INPUT pad −10dB?]──┬───────────────────────────────► dry path
                         │                                  │
                         │                           (LPF if crossover)   ──┐
                         │                                                   │
                         └─[HPF if crossover]─► Q1 ─► Q2 clip ─► Q3 clip ─► │
                                                   (SUSTAIN)   |             │
                                                         tone stack ─► Q4 ─► │
                                                         (TONE)     (VOLUME) │
                                                                            │
                                               wet ──[GATE]──────────────────┤
                                                                            │
                                                 dry ────────────────────────┤
                                                                            │
                                                     BLEND mix ──────────────┴──► out
```

(Exact tap points for HPF/LPF/blend/gate per the traced PCB — see §6 "open questions".)

---

## 2. Scope & fidelity decisions

**Locked decisions (confirmed with user):** target the original transistor **Deluxe Bass
Classic Bass Fuzz ** only, and use **full circuit-level virtual analog (white-box)**
modeling — not a lighter waveshaper/static-tone-curve approximation. The next-gen
(dual op-amp core) is explicitly out of scope for this build.

| Decision | Choice | Rationale |
|---|---|---|
| Modeling style | **Circuit-level virtual analog (VA)**, white-box | "Replicate the same circuit" = component-level, not a generic fuzz waveshaper |
| Core nonlinear method | Per-block **Antiderivative / DK method** diode + BJT stages, or full **MNA + Newton-Raphson** if we want one monolithic solve | Best aliasing/smoothness-per-CPU tradeoff; the Classic Bass Fuzz is DC-coupled enough to need the real nonlinear solve in the clippers |
| Oversampling | **2×–4×** around the clippers | Diode clippers alias badly at 44.1 kHz |
| Hardware I/O (XLR DI, dry direct out, true-bypass switch) | **Out of scope** for v1 VST | Not meaningful inside a DAW; document as future sidechain/secondary-out options |
| Cab sim on the DI | Optional v2, as a separate "DI out" bus | Replicates the hardware DI behavior without polluting the main out |
| Reference model | The **Deluxe Bass Fuzz (V11) / classic bass fuzz** core, not the USA NYC Fuzz | Deluxe is explicitly the classic-family circuit |

**Note — Deluxe Bass Fuzz *2* (2026):** out of scope. It is a different, newer
pedal built on a "dual op-amp Classic Bass Fuzz 2" core (op-amp clipping). We target the original
transistor Deluxe  only.

---

## 3. DSP architecture

Model each analog block as a small, testable DSP object, chained in `processBlock`:

```
input
 └─ InputPad            : linear gain 1.0 / 0.316 (−10 dB), smoothed
 └─ (CrossoverHPF)      : 2nd-order HPF, 125 Hz–1.75 kHz, bypassable
 └─ BmpInputStage       : Q1 — linear(ish) gain + input HPF + Miller LPF, optional BJT nonlinearity
 └─ BmpClippingStage ×2 : Q2, Q3 — diode soft-clipper (DK/antiderivative) + coupling filters, SUSTAIN → drive
 └─ BmpToneStack        : passive mid-scoop, MNA → state-space/biquad, TONE param
 └─ BmpOutputStage      : Q4 recovery gain + VOLUME
 └─ NoiseGate           : envelope follower + downward expander on the wet path
 ──┐
dry path ── buffered, (CrossoverLPF: 2nd-order LPF 60 Hz–10 kHz, bypassable) ──┘
 └─ Blend               : equal-/constant-power crossfade (dry ↔ wet), pot-law matched
 └─ output gain / soft limiter
```

Oversampling wrapper: the `BmpInputStage → … → BmpOutputStage` span runs at 2×–4× rate
with a halfband up/down filter pair; the crossover filters, gate, and blend run at base
rate.

---

## 4. Per-block modeling approach

1. **Input pad / blend / volume** — trivial smoothed linear gains. Match OEM pot tapers
   (linear for sustain/tone; log for volume — verify blend taper, see §6).
2. **Crossover filters** — 2nd-order Butterworth/state-variable biquads, coefficient
   smoothing on cutoff. Cutoff ranges from the manual (HPF 125 Hz–1.75 kHz,
   LPF 60 Hz–10 kHz); verify the LPF top end against a unit (suspiciously wide).
3. **Q1 input stage** — start as a linearized transfer (gain + 1st-order HPF from input
   cap + 1st-order LPF from Miller cap). Upgrade to a one-transistor nonlinear stage only
   if measured clipping at the input warrants it.
4. **Clipping stages (the core of the sound)** — model as a single-stage diode clipper
   using the **Antiderivative / DK-method** (Yeh's nodal K-method applied to one node):
   - Solve the implicit diode-pair equation per sample (Newton-Raphson) inside the
     surrounding RC network, or precompute the antiderivative and use its inverse.
   - The two stages are cascaded; SUSTAIN maps to the drive/gain into stage 2.
   - This is exactly the technique used to simulate the Classic Bass Fuzz V3 in the
     [NTUA thesis on digital simulation of audio circuits](https://dspace.lib.ntua.gr/xmlui/handle/123456789/53137)
     (MNA + trapezoidal discretization + Newton-Raphson, "NK method"), which is our main
     implementation reference for the nonlinear stages.
5. **Tone stack** — derive the exact 2nd/3rd-order transfer function by nodal analysis of
   the passive network (39k / 22k resistors, 10n / 3n9 caps, 100k pot), then implement as
   a state-space filter (the approach in the [Aalto white-box modeling thesis](https://aaltodoc.aalto.fi/server/api/core/bitstreams/4b830300-a5d6-4544-a0fa-a6f2ea123a5e/content)).
   Validate the two cutoffs against 408 Hz / 1808 Hz.
6. **Q4 output stage** — linear recovery gain + volume (can fold into output gain if the
   stage is linear in practice).
7. **Gate** — RMS/peak envelope follower on the key signal with adjustable threshold,
   fast attack, adjustable release; applies gain reduction to the wet path. Key from the
   dry input (so low notes aren't chopped), matching OEM's "remove hum/hiss" behavior.
   Smooth the gain-reduction signal to avoid zipper noise.
8. **Oversampling** — 4× on the clippers; use linear-phase-ish halfband or FIR polyphase.

---

## 5. JUCE implementation plan

### 5.1 Project scaffold

- JUCE 8.x, **CMake** (or Projucer). Plugin formats: VST3 + AU (standalone for testing).
- Target: `DeluxeBassFuzz`. `processBlock` mono-in/mono-out internally (bass is mono);
  duplicate to both output channels.

### 5.2 Parameters (AudioProcessorValueTreeState)

| ID | Name | Range | Type | Notes |
|---|---|---|---|---|
| `sustain` | Sustain | 0–1 | float | drive into clippers |
| `tone` | Tone | 0–1 | float | tone-stack pot position |
| `volume` | Volume | 0–1 | float | log taper |
| `blend` | Blend | 0–1 | float | dry↔wet crossfade |
| `gate` | Gate | 0–1 | float | threshold/amount |
| `hpf` | Crossover HPF | 125–1750 Hz | float | 2nd-order, bypassable |
| `lpf` | Crossover LPF | 60–10000 Hz | float | 2nd-order, bypassable |
| `crossover` | Crossover on/off | bool | bool | footswitch equivalent |
| `inputPad` | Input pad | 0 / −10 dB | bool | toggle |
| `bypass` | Effect bypass | bool | bool | |

### 5.3 Source layout

```
DeluxeBassFuzz/
├─ CMakeLists.txt
├─ Source/
│  ├─ PluginProcessor.{h,cpp}        # parameters, chain wiring
│  ├─ PluginEditor.{h,cpp}           # knob GUI
│  ├─ dsp/
│  │  ├─ InputPad.h
│  │  ├─ BmpInputStage.h
│  │  ├─ DiodeClipper.h              # antiderivative/DK diode-pair solver
│  │  ├─ BmpClippingStage.h
│  │  ├─ BmpToneStack.h              # MNA → state-space
│  │  ├─ BmpOutputStage.h
│  │  ├─ CrossoverFilters.h          # 2nd-order HPF/LPF
│  │  ├─ NoiseGate.h
│  │  ├─ Blend.h
│  │  └─ Oversampler.h
│  └─ (smoothers, tests)
├─ tests/
│  ├─ ToneStackTest.cpp
│  ├─ DiodeClipperTest.cpp
│  └─ ReferenceData/                 # SPICE exports + recorded pedal wavs
└─ docs/
   ├─ circuit-notes.md               # transcribed schematic + values
   └─ validation.md
```

### 5.4 Build order (milestones)

1. **M0 — reference capture** (before any DSP): transcribe the Deluxe Bass Fuzz / Deluxe
   schematic with component values (from circuit reference + freestompboxes EC-D40 trace); build an
   **LTspice/QSPICE netlist** of the fuzz core + tone stack as ground truth.
2. **M1 — linear skeleton**: input pad, tone stack, output gain, blend, crossover filters,
   gate (all linear). Passes a dry-signal A/B against SPICE frequency responses.
3. **M2 — nonlinear clippers**: implement `DiodeClipper`, cascade two stages, add
   oversampling. Validate THD/harmonics vs SPICE transient (sine sweep + fuzz-on-bass).
4. **M3 — GUI + parameter smoothing + bypass**, plugin packages.
5. **M4 — ear/A-B tuning** against recorded pedal samples (DI re-amp), then polish
   (anti-aliasing, gate feel, blend law).

---

## 6. Open questions to resolve during M0

1. **Exact component values** for the Deluxe Bass Fuzz (EC-D40) core — pull the traced
   schematic (freestompboxes `t=31296`) and transcribe; the classic bass fuzz/Bass Fuzz use
   different resistors than the USA Fuzzs.
2. **LPF top-end** — the manual says 60 Hz–10 kHz, which is suspiciously wide for a
   "crossover" low-pass; measure a real unit or confirm the intended range.
3. **Blend & gate tap points / taper** — where the blend is injected and the gate key is
   taken, and the blend pot law. Confirm from the Deluxe PCB trace or a unit.
4. **DI/cab-sim circuit** — out of scope for v1, but document the topology if we add a
   "DI out" bus later.

---

## 7. References (read for this plan)

- [circuit reference — Classic Bass Fuzz History Part 3, "Deluxe Bass Fuzz" section](http://www.kitrae.net/music/) (circuit description, controls, designers)
- [circuit reference — Classic Bass Fuzz History Part 2, classic bass fuzz section](http://www.kitrae.net/music/) (classic core details, feedback caps, transistors)
- [Deluxe Bass Fuzz manual](https://archive.forum/) (control ranges: HPF 125 Hz–1.75 kHz, LPF 60 Hz–10 kHz, −10 dB pad)
- [Jorge Pais — "The Fuzzler Delete"](https://jorge-pais.github.io/articles/2022/03/10/The-Fuzzler-Delete/) (4-stage topology: input → 2 diode-feedback clippers → tone stack → output)
- [NTUA thesis — Digital simulation of audio circuits (Classic Bass Fuzz V3, NK method)](https://dspace.lib.ntua.gr/xmlui/handle/123456789/53137) (main nonlinear-modeling reference)
- [Aalto — White-box modeling of guitar tone stacks](https://aaltodoc.aalto.fi/server/api/core/bitstreams/4b830300-a5d6-4544-a0fa-a6f2ea123a5e/content) (tone-stack MNA/state-space approach)
- [UPM thesis (Carlos Pérez Olea)](https://oa.upm.es/70484/1/TFG_CARLOS_PEREZ_OLEA.pdf) (tone-stack cutoffs: LP 1808 Hz, HP 408 Hz)
- [freestompboxes — Deluxe Bass Fuzz schematic discussion](https://www.freestompboxes.org/viewtopic.php?t=31296) (EC-D40 PCB / classic values)
- [TalkBass — Deluxe Bass Fuzz mods](https://www.talkbass.com/) (coupling-cap / clipping-loop value notes)

### Further resources to pull during implementation (not yet read)

- Electrosmash — "Classic Bass Fuzz Analysis" (stage-by-stage gain math)
- Jack Orman — "A Close Look at the Classic Bass Fuzz & Proco Rat" (tone-stack + mods)
- Piso-Tones — Classic Bass Fuzz variant schematic collection
