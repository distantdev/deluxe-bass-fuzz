# Circuit Notes: Deluxe Bass Fuzz

## Overview
This document describes the analog circuit topology, component values, and DSP architecture modeled in **Deluxe Bass Fuzz**.

## Core Circuit

### Transistors
- **Q1–Q4**: 2N5088-class NPN silicon transistors

### Power Supply
- 9V DC (center-positive / standard pedal supply rail)
- 22 µF electrolytic filter capacitor and reverse-polarity protection diode (1N4001)

### Stage 1: Input Buffer / Gain (Q1)
```
Input ──C1(100n)───┬──R1(39k)───┬──Base Q1
                   │            │
                  R2(100k)     R3(470k)──Miller C2(470p)──Collector──┬──R_C(12k)── +9V
                   │            │                                     │
                  GND          Emitter──R4(390R)──GND                C3(100n)───SUSTAIN pot (100k log)───► to Q2
```
- **C1** = 100 nF (input coupling)
- **R1** = 39 kΩ (series input resistor), **R2** = 100 kΩ (base bias to GND)
- **R3** = 470 kΩ (collector-to-base feedback), **C2** = 470 pF (Miller compensation)
- **R4** = 390 Ω (emitter degeneration), **R_C** = 12 kΩ (collector load)
- **C3** = 100 nF (output coupling to SUSTAIN pot)
- **SUSTAIN** = 100 kΩ log (audio) taper attenuator between Q1's collector (via C3) and Q2, bottom leg returned through 1 kΩ to GND. Q1's own gain is fixed.

### Stage 2: First Clipping Stage (Q2)
```
From SUSTAIN ──C4(100n)───┬──R5(10k)───┬──Base Q2
                          │            │
                         R6(100k)     R7(470k) ∥ [C_fb(47n) + D1/D2(1N914 anti-parallel)]──Collector──┬──R_C(12k)── +9V
                          │            │                                                              │
                         GND          Emitter──R8(390R)──GND                                         C5(100n)───► to Q3
```
- **C4** = 100 nF (coupling), **R5** = 10 kΩ (series input), **R6** = 100 kΩ (bias)
- **R7** = 470 kΩ (feedback), **C_fb** = 47 nF in series with anti-parallel **D1, D2** (1N914/1N4148 silicon clipping diodes)
- **R8** = 390 Ω (emitter), **R_C** = 12 kΩ (collector load), **C5** = 100 nF (coupling to Q3)

### Stage 3: Second Clipping Stage (Q3)
- Identical topology and component values to Stage 2 (10 kΩ series input, 100 kΩ bias, 470 kΩ feedback, 47 nF + anti-parallel 1N914 diodes, 390 Ω emitter, 12 kΩ collector load, 100 nF output coupling into the tone stack).

### Stage 4: Passive Tone Stack
```
From Q3 collector ──┬──R_LP(11.5k)──┬── LP node ──┐
                    │               C_LP(10n)     TONE pot (100k linear, wiper ──C(100n)──► Q4 base)
                    │               GND           │
                    └──C_HP(3n9)────┬── HP node ──┘
                                    R_HP(22k)
                                    GND
```
- **R_LP** = 11.5 kΩ, **C_LP** = 10 nF → LP corner `1 / (2π · 11.5k · 10n)` = **1384 Hz**
- **C_HP** = 3.9 nF, **R_HP** = 22 kΩ → HP corner `1 / (2π · 22k · 3.9n)` = **1855 Hz**
- **TONE** = 100 kΩ linear pot; wiper drives Q4 through a 100 nF coupling capacitor. Q4's input impedance loads the wiper, which is modeled directly in the coupled Q3/tone/Q4 simulation.

### Stage 5: Output Recovery (Q4)
```
From TONE ──C10(100n)───┬──R15(10k)───┬──Base Q4
                        │             │
                       R16(100k)     R17(470k)──Collector──┬──R_C(12k)── +9V
                        │             │                    │
                       GND           Emitter──R18(2k)──GND C11(100n)───VOLUME pot (100k log)───► Output
```
- **C10** = 100 nF, **R15** = 10 kΩ, **R16** = 100 kΩ, **R17** = 470 kΩ, **R18** = 2 kΩ, **R_C** = 12 kΩ, **C11** = 100 nF
- **VOLUME** = 100 kΩ log/audio taper pot

---

## Deluxe Features

### Input Pad
- Toggle switch: `0 dB` / `-10 dB` input attenuation before Q1.

### Crossover Section (Footswitchable)
- **HPF (on wet/fuzz path)**: 2nd-order Butterworth high-pass, `125 Hz – 1.75 kHz`, engaged before Q1 input.
- **LPF (on dry path)**: 2nd-order Butterworth low-pass, `60 Hz – 10 kHz`, engaged on the dry/blend path.

### Blend Control
- Buffered dry path mixed with the fuzz path via constant-power crossfade.

### Noise Gate
- Applied to the wet/fuzz path, keyed from the dry input to preserve low-note sustain (`-80 dB` to `-20 dB` threshold, fast attack, smooth release).

---

## Component Value Summary

| Ref | Value | Notes |
|-----|-------|-------|
| C1, C3, C4, C5, C6, C7, C10, C11 | 100 nF | Coupling capacitors |
| C2 | 470 pF | Miller compensation (Q1) |
| C_fb (Q2, Q3) | 47 nF | Series feedback cap with clipping diodes |
| C_LP | 10 nF | Tone stack low-pass capacitor |
| C_HP | 3.9 nF | Tone stack high-pass capacitor |
| R1 | 39 kΩ | Q1 series input resistor |
| R5, R9, R15 | 10 kΩ | Q2, Q3, Q4 series input resistors |
| R2, R6, R10, R16 | 100 kΩ | Base bias resistors |
| R3, R7, R11, R17 | 470 kΩ | Collector-to-base feedback resistors |
| R4, R8, R12 | 390 Ω | Q1–Q3 emitter resistors |
| R18 | 2 kΩ | Q4 emitter resistor |
| R_C (Q1–Q4) | 12 kΩ | Collector load resistors |
| R_LP / R_HP | 11.5 kΩ / 22 kΩ | Tone stack resistors |
| SUSTAIN / VOLUME | 100 kΩ log | Attenuator (Q1→Q2) and output level |
| TONE | 100 kΩ lin | Passive tone balance |
| D1–D4 | 1N914 / 1N4148 | Anti-parallel silicon clipping diodes |
| Q1–Q4 | 2N5088 | NPN silicon transistors |

---

## DSP Model

Each gain stage is a fixed-topology modified-nodal-analysis (MNA) circuit simulation (`Source/dsp/NonlinearCircuit.h`, `Source/dsp/BmpCircuit.h`), running inside a 4× oversampled span:

- Trapezoidal capacitor companions reduced to a compact state-space (DK) form: per sample a small matrix-vector multiply plus a 2–4 unknown Newton solve on the junction voltages (SPICE `pnjlim` limiting with linear predictor, ~1.2 iterations on average).
- 2N5088 Ebers-Moll transport with Gummel-Poon Early effect and 1N914 diode pairs.
- Each stage is loaded by the next stage's input network, and the tone stack is simulated directly between Q3 and Q4 so Q4's input impedance loads the wiper.
- Aliasing is controlled via 4× oversampling around the nonlinear core.

## SPICE Netlist Reference

See `docs/deluxe_bass_fuzz.cir` for the full reference netlist and `docs/validation.md` for ngspice comparison results.
