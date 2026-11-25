#pragma once

#include <JuceHeader.h>
#include "BmpCircuit.h"

//==============================================================================
/**
    Classic Bass Fuzz output recovery stage (Q4), optionally with the passive tone stack
    in front of it, simulated as one circuit:

        [tone stack: in --11.5k-- lp (10n to gnd), in --3n9-- hp (22k to gnd),
         100k pot lp..hp, wiper]
        wiper/in --C12 100n-- base (R21 100k to gnd, R22 470k from collector)
        +9V --R24 10k-- collector --C13 100n-- 100k (volume pot at max) = output
        emitter --R23 2k-- gnd

    With setIncludeToneStack (true) (the plugin core) the tone-stack wiper is
    loaded by Q4's real input impedance (100n in series with ~45k), which
    lowers and reshapes the stack's response compared with the unloaded
    BmpToneStack model. Q4's gain (~12 dB into 100k) comes from its own
    10k/2k values, and the 2N5088 model gives its asymmetric swing limits.

    Runs inside the oversampled span: prepare() must be given the oversampled rate.
*/
class BmpOutputStage
{
public:
    BmpOutputStage() = default;

    /** Call before prepare(). */
    void setIncludeToneStack (bool shouldInclude) { includeTone = shouldInclude; }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        updateInterval = juce::jmax (1, (int) std::lround (fs / 6000.0));
        appliedTone = tone;

        if (includeTone)
        {
            using namespace bmd::values;
            auto& c = withTone;
            c.clear();
            c.setInput (0, 1.0);
            c.addResistor (0, 1, Tone_Rlp);
            c.addCapacitor (1, -1, Tone_Clp);
            c.addCapacitor (0, 2, Tone_Chp);
            c.addResistor (2, -1, Tone_Rhp);
            potA = c.addResistor (1, 3, potValue (tone));
            potB = c.addResistor (3, 2, potValue (1.0 - tone));
            c.addCapacitor (3, 4, Q4_Cin);
            buildQ4 (c);
            c.prepare (fs);
        }
        else
        {
            using namespace bmd::values;
            auto& c = alone;
            c.clear();
            c.setInput (0, 1.0);
            c.addCapacitor (0, 4, Q4_Cin);
            c.addResistor (1, -1, 1.0e6);   // unused nodes
            c.addResistor (2, -1, 1.0e6);
            c.addResistor (3, -1, 1.0e6);
            buildQ4 (c);
            c.prepare (fs);
        }
        samplesSinceUpdate = 0;
    }

    /** Tone knob 0..1 (0 = bass, 1 = treble); used when the tone stack is included. */
    void setTone (float toneValue)
    {
        tone = juce::jlimit (0.0, 1.0, (double) toneValue);
    }

    float processSample (float x)
    {
        if (! includeTone)
            return (float) (alone.process ((double) x) - alone.getDcOutput());

        if (++samplesSinceUpdate >= updateInterval && std::abs (tone - appliedTone) > 1.0e-5)
        {
            withTone.setResistor (potA, potValue (tone));
            withTone.setResistor (potB, potValue (1.0 - tone));
            withTone.update();
            appliedTone = tone;
            samplesSinceUpdate = 0;
        }
        return (float) (withTone.process ((double) x) - withTone.getDcOutput());
    }

    void process (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

    void reset()
    {
        alone.reset();
        withTone.reset();
    }

    double getQuiescentCollectorVoltage() const { return includeTone ? withTone.getDcVoltage (5) : alone.getDcVoltage (5); }

private:
    static double potValue (double fraction) { return juce::jmax (1.0, bmd::values::Tone_Pot * fraction); }

    // nodes: 4 base, 5 collector, 6 emitter, 7 output (after C13, into the 100k volume pot)
    template <typename Circuit>
    static void buildQ4 (Circuit& c)
    {
        using namespace bmd::values;
        c.setOutput (output);
        c.addResistor (4, -1, Q4_Rb);
        c.addResistor (5, 4, Q4_Rf);
        c.addSupply (5, Q4_Rc, Vcc);
        c.addResistor (6, -1, Q4_Re);
        c.setBjt (4, 5, 6);
        c.addCapacitor (5, output, Out_C);
        c.addResistor (output, -1, Out_Rvol);
    }

    static constexpr int output = 7;
    bool includeTone = false;
    double fs = 48000.0;
    double tone = 0.5, appliedTone = 0.5;
    int potA = 0, potB = 0;
    int updateInterval = 32, samplesSinceUpdate = 0;
    // <nodes, capacitors, BJTs, diode pairs>
    bmd::NonlinearCircuit<8, 2, 1, 0> alone;      // Q4 driven directly (validation "output" block)
    bmd::NonlinearCircuit<8, 4, 1, 0> withTone;   // tone stack + Q4 (plugin core)
};
