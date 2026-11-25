#pragma once

#include <JuceHeader.h>
#include "BmpCircuit.h"

//==============================================================================
/**
    Classic Bass Fuzz input booster (Q1) + SUSTAIN pot, simulated as a circuit:

        in --C1 100n--R1 39k--+-- base         +9V --R5 12k-- collector
                              |                feedback 470k || 470p
                           R2 100k             emitter --R4 390R-- gnd
                              |
                             gnd
        collector --C3 100n-- SUSTAIN 100k log pot --R7 1k-- gnd ; wiper = output

    Q1 has a fixed gain (~14 dB into its load); SUSTAIN is a passive log-taper
    attenuator after it (wiper fraction (10^(2s)-1)/99, ~9% at noon), not a
    gain control. The wiper is loaded by a linearised copy of Q2's input
    (100n + 10k into Q2's base network, Q2's 47n + diode pair kept nonlinear),
    so the pot's loading of Q1 and its frequency response follow the real
    circuit, including the lower load impedance once Q2's diodes conduct.

    The BJT is the 2N5088 model (see NonlinearCircuit.h), so large inputs hit
    Q1's real, asymmetric collector swing limits (cut-off / saturation).

    Runs inside the oversampled span: prepare() must be given the oversampled rate.
*/
class BmpInputStage
{
public:
    BmpInputStage() = default;

    void prepare (double sampleRate)
    {
        using namespace bmd::values;
        fs = sampleRate;
        updateInterval = juce::jmax (1, (int) std::lround (fs / 6000.0));

        auto& c = circuit;
        c.clear();
        // nodes: 0 src, 1 coupling, 2 base, 3 collector, 4 emitter,
        //        5 pot top, 6 wiper (output), 7 pot bottom, 8..11 Q2 replica
        c.setInput (0, 1.0);
        c.setOutput (wiper);
        c.addCapacitor (0, 1, Q1_Cin);
        c.addResistor (1, 2, Q1_Rs);
        c.addResistor (2, -1, Q1_Rb);
        c.addResistor (3, 2, Q1_Rf);
        c.addCapacitor (3, 2, Q1_Cf);
        c.addSupply (3, Q1_Rc, Vcc);
        c.addResistor (4, -1, Q1_Re);
        c.setBjt (2, 3, 4);

        c.addCapacitor (3, 5, Sus_C);
        const double frac = logTaper (sustain);
        potTop = c.addResistor (5, 6, potValue (1.0 - frac));
        potBottom = c.addResistor (6, 7, potValue (frac));
        c.addResistor (7, -1, Sus_R7);

        // Q2 input replica incl. its 47n + diode pair; Q2's collector sees 12k || Q3's input (~35k)
        const auto q = bmd::biasCommonEmitter (Clip_Rc, Clip_Rf, Clip_Rb, Clip_Re);
        bmd::addStageReplica (c, 6, 8, Clip_Cin, Clip_Rs, Clip_Rb, Clip_Rf, Clip_Cf,
                              bmd::parallel (Clip_Rc, 35e3), Clip_Re, q, Clip_Cd);

        appliedSustain = sustain;
        c.prepare (fs);
        samplesSinceUpdate = 0;
    }

    /** SUSTAIN knob 0..1 (log-taper pot position). */
    void setSustain (float knob)
    {
        sustain = juce::jlimit (0.0, 1.0, (double) knob);
    }

    float processSample (float x)
    {
        if (++samplesSinceUpdate >= updateInterval && std::abs (sustain - appliedSustain) > 1.0e-5)
        {
            const double frac = bmd::values::logTaper (sustain);
            circuit.setResistor (potTop, potValue (1.0 - frac));
            circuit.setResistor (potBottom, potValue (frac));
            circuit.update();
            appliedSustain = sustain;
            samplesSinceUpdate = 0;
        }

        return (float) (circuit.process ((double) x) - circuit.getDcOutput());
    }

    void process (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

    void reset() { circuit.reset(); }

    double getQuiescentCollectorVoltage() const { return circuit.getDcVoltage (3); }

private:
    static double potValue (double fraction) { return juce::jmax (1.0, bmd::values::Sus_Pot * fraction); }

    static constexpr int wiper = 6;
    double fs = 48000.0;
    double sustain = 0.5, appliedSustain = 0.5;
    int potTop = 0, potBottom = 0;
    int updateInterval = 32, samplesSinceUpdate = 0;
    bmd::NonlinearCircuit<13, 6, 1, 1> circuit;   // <nodes, capacitors, BJTs, diode pairs>
};
