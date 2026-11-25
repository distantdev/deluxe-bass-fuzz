#pragma once

#include <JuceHeader.h>
#include "BmpCircuit.h"

//==============================================================================
/**
    Classic Bass Fuzz clipping stage (Q2 or Q3), simulated as a circuit:

        in --Cin 100n--10k--+-- base          +9V --12k-- collector (= output)
                            |                 feedback collector->base:
                          100k                  470k || 470p || (47n + 1N914 pair)
                            |                 emitter --390R-- gnd
                           gnd

    The BJT is the 2N5088 Ebers-Moll/Gummel-Poon-lite model, so the stage has
    its real bias point (Vc ~4.7 V, Ic ~0.36 mA), real collector swing limits
    (cut-off ~+2 V above Vc, saturation ~-4 V) and the exponential Vbe law:
    the clipping is asymmetric and produces even harmonics without any fudge
    offset. The diode pair sits in series with the 47n cap, so at low
    frequencies the diodes clip less (the "fat bottom" of the classic bass fuzz).

    The stage's collector is loaded by a copy of what follows it:
      - Load::NextClippingStage: Q3's input network (100n + 10k into its base),
        with Q3's transistor linearised but its 47n + diode pair kept nonlinear
        (Q3's input impedance drops when its diodes conduct)
      - Load::ToneStackAndOutputStage: the tone stack (tone-dependent) whose
        wiper is loaded by Q4's input (100n into Q4's base network)

    Coupling corners come from the components: 100n into 10k + base impedance
    (~120 Hz), 470p Miller, 47n clip cap, and the load network.

    Runs inside the oversampled span: prepare() must be given the oversampled
    rate. Per sample: a small state-space update plus a 3-4 unknown Newton
    solve (Vbe, Vbc, Vdiode [, next stage's Vdiode]), warm-started from a linear
    prediction, typically 1-2 iterations. The diode/BJT nonlinearity sits in a
    stateful (capacitive) feedback loop, so first-order ADAA (which needs a
    memoryless nonlinearity) does not apply; aliasing is handled by the 4x
    oversampling.
*/
class BmpClippingStage
{
public:
    enum class Load { NextClippingStage, ToneStackAndOutputStage };

    explicit BmpClippingStage (Load l = Load::NextClippingStage) : load (l) {}

    /** Call before prepare(). */
    void setLoad (Load l) { load = l; }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        updateInterval = juce::jmax (1, (int) std::lround (fs / 6000.0));
        appliedTone = tone;

        if (load == Load::NextClippingStage)
        {
            buildCore (nextStage);
            // Q3 input incl. its 47n + diode pair; Q3's collector sees 12k || tone stack (~15k)
            using namespace bmd::values;
            const auto q = bmd::biasCommonEmitter (Clip_Rc, Clip_Rf, Clip_Rb, Clip_Re);
            bmd::addStageReplica (nextStage, collector, 6, Clip_Cin, Clip_Rs, Clip_Rb, Clip_Rf, Clip_Cf,
                                  bmd::parallel (Clip_Rc, 15e3), Clip_Re, q, Clip_Cd);
            nextStage.prepare (fs);
        }
        else
        {
            buildCore (toneLoad);
            using namespace bmd::values;
            // tone stack: 6 lp, 7 hp, 8 wiper; Q4 input replica on nodes 9..12
            toneLoad.addResistor (collector, 6, Tone_Rlp);
            toneLoad.addCapacitor (6, -1, Tone_Clp);
            toneLoad.addCapacitor (collector, 7, Tone_Chp);
            toneLoad.addResistor (7, -1, Tone_Rhp);
            potA = toneLoad.addResistor (6, 8, potValue (tone));
            potB = toneLoad.addResistor (8, 7, potValue (1.0 - tone));
            const auto q = bmd::biasCommonEmitter (Q4_Rc, Q4_Rf, Q4_Rb, Q4_Re);
            bmd::addStageReplica (toneLoad, 8, 9, Q4_Cin, 0.0, Q4_Rb, Q4_Rf, 0.0,
                                  bmd::parallel (Q4_Rc, Out_Rvol), Q4_Re, q);
            toneLoad.prepare (fs);
        }
        samplesSinceUpdate = 0;
    }

    /** Tone knob 0..1 (only used with Load::ToneStackAndOutputStage). */
    void setTone (float toneValue)
    {
        tone = juce::jlimit (0.0, 1.0, (double) toneValue);
    }

    float processSample (float x)
    {
        if (load == Load::NextClippingStage)
            return (float) (nextStage.process ((double) x) - nextStage.getDcOutput());

        if (++samplesSinceUpdate >= updateInterval && std::abs (tone - appliedTone) > 1.0e-5)
        {
            toneLoad.setResistor (potA, potValue (tone));
            toneLoad.setResistor (potB, potValue (1.0 - tone));
            toneLoad.update();
            appliedTone = tone;
            samplesSinceUpdate = 0;
        }
        return (float) (toneLoad.process ((double) x) - toneLoad.getDcOutput());
    }

    void process (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

    void reset()
    {
        nextStage.reset();
        toneLoad.reset();
    }

    double getQuiescentCollectorVoltage() const
    {
        return load == Load::NextClippingStage ? nextStage.getDcOutput() : toneLoad.getDcOutput();
    }

    int getLastIterationCount() const noexcept
    {
        return load == Load::NextClippingStage ? nextStage.getLastIterationCount() : toneLoad.getLastIterationCount();
    }

private:
    static double potValue (double fraction) { return juce::jmax (1.0, bmd::values::Tone_Pot * fraction); }

    // nodes: 0 src, 1 coupling, 2 base, 3 collector, 4 emitter, 5 diode/cap node, 6.. load
    template <typename Circuit>
    static void buildCore (Circuit& c)
    {
        using namespace bmd::values;
        c.clear();
        c.setInput (0, 1.0);
        c.setOutput (collector);
        c.addCapacitor (0, 1, Clip_Cin);
        c.addResistor (1, 2, Clip_Rs);
        c.addResistor (2, -1, Clip_Rb);
        c.addResistor (collector, 2, Clip_Rf);
        c.addCapacitor (collector, 2, Clip_Cf);
        c.addCapacitor (collector, 5, Clip_Cd);
        c.addDiodePair (5, 2);
        c.addSupply (collector, Clip_Rc, Vcc);
        c.addResistor (4, -1, Clip_Re);
        c.setBjt (2, collector, 4);
    }

    static constexpr int collector = 3;
    Load load;
    double fs = 48000.0;
    double tone = 0.5, appliedTone = 0.5;
    int potA = 0, potB = 0;
    int updateInterval = 32, samplesSinceUpdate = 0;

    // <nodes, capacitors, BJTs, diode pairs>
    bmd::NonlinearCircuit<11, 6, 1, 2> nextStage;   // own diodes + Q3's diodes
    bmd::NonlinearCircuit<13, 6, 1, 1> toneLoad;
};
