#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/BmpInputStage.h"
#include "dsp/BmpClippingStage.h"
#include "dsp/BmpOutputStage.h"

//==============================================================================
// Circuit-stage tests (Q1 + sustain, Q2/Q3 clipping stages, tone stack + Q4).
//
// Quiescent collector voltages are compared with ngspice 47 .op of the core
// deck (tests/ReferenceData/spice/decks/ac_core_s0.50.cir, bmp_parts.lib):
//   Q1 4.7122 V, Q2 4.7122 V, Q3 4.6215 V (loaded by tone stack + Q4), Q4 6.5065 V.
// Tolerance 50 mV: the DSP uses the same 2N5088 Ebers-Moll/Gummel-Poon DC model
// but replaces the downstream stages by linearised replicas, so a few mV of
// difference are expected (the measured difference is < 10 mV).
//==============================================================================

namespace
{
    constexpr double fs = 192000.0;   // the stages run inside the 4x oversampled span

    template <typename Stage>
    std::vector<float> runSine (Stage& s, double amp, double f, int n)
    {
        std::vector<float> y ((size_t) n);
        for (int i = 0; i < n; ++i)
            y[(size_t) i] = s.processSample ((float) (amp * std::sin (2.0 * 3.14159265358979 * f * i / fs)));
        return y;
    }

    double rmsTail (const std::vector<float>& y, int tail)
    {
        double a = 0.0;
        for (size_t i = y.size() - (size_t) tail; i < y.size(); ++i)
            a += (double) y[i] * y[i];
        return std::sqrt (a / tail);
    }

    template <typename Stage>
    void expectRobust (Stage& s)
    {
        const float bad[] = { std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              1.0e6f, -1.0e6f, 1.0e30f };
        for (int rep = 0; rep < 64; ++rep)
            for (float b : bad)
                EXPECT_TRUE (std::isfinite (s.processSample (b)));

        // a very hot but physical input (20 V peak) stays bounded by the supply swing
        const auto y = runSine (s, 20.0, 100.0, (int) fs / 10);
        for (float v : y)
        {
            ASSERT_TRUE (std::isfinite (v));
            EXPECT_LT (std::abs (v), 20.0f);
        }

        // and the stage recovers to silence
        float last = 0.0f;
        for (int i = 0; i < (int) fs; ++i)
            last = s.processSample (0.0f);
        EXPECT_LT (std::abs (last), 1.0e-3f);
    }
}

TEST (StageTest, QuiescentPointsMatchSpice)
{
    BmpInputStage in;
    in.setSustain (0.5f);
    in.prepare (fs);
    EXPECT_NEAR (in.getQuiescentCollectorVoltage(), 4.7122, 0.05);

    BmpClippingStage c1 (BmpClippingStage::Load::NextClippingStage);
    c1.prepare (fs);
    EXPECT_NEAR (c1.getQuiescentCollectorVoltage(), 4.7122, 0.05);

    BmpClippingStage c2 (BmpClippingStage::Load::ToneStackAndOutputStage);
    c2.setTone (0.5f);
    c2.prepare (fs);
    EXPECT_NEAR (c2.getQuiescentCollectorVoltage(), 4.6215, 0.05);

    BmpOutputStage out;
    out.setIncludeToneStack (true);
    out.setTone (0.5f);
    out.prepare (fs);
    EXPECT_NEAR (out.getQuiescentCollectorVoltage(), 6.5065, 0.05);
}

TEST (StageTest, SilenceInSilenceOut)
{
    BmpInputStage in;            in.prepare (fs);
    BmpClippingStage c1;         c1.prepare (fs);
    BmpClippingStage c2 (BmpClippingStage::Load::ToneStackAndOutputStage); c2.prepare (fs);
    BmpOutputStage out;          out.setIncludeToneStack (true); out.prepare (fs);
    for (int i = 0; i < 4096; ++i)
    {
        const float y = out.processSample (c2.processSample (c1.processSample (in.processSample (0.0f))));
        ASSERT_LT (std::abs (y), 1.0e-6f);
    }
}

TEST (StageTest, FiniteAndBoundedForExtremeInput)
{
    { BmpInputStage s; s.setSustain (1.0f); s.prepare (fs); expectRobust (s); }
    { BmpClippingStage s; s.prepare (fs); expectRobust (s); }
    { BmpClippingStage s (BmpClippingStage::Load::ToneStackAndOutputStage); s.prepare (fs); expectRobust (s); }
    { BmpOutputStage s; s.setIncludeToneStack (true); s.prepare (fs); expectRobust (s); }
}

TEST (StageTest, SustainIsMonotonicLogTaper)
{
    // small-signal 1 kHz level at the sustain wiper rises monotonically with the knob
    double prev = 0.0;
    for (float k : { 0.1f, 0.3f, 0.5f, 0.7f, 1.0f })
    {
        BmpInputStage s;
        s.setSustain (k);
        s.prepare (fs);
        const double r = rmsTail (runSine (s, 1.0e-4, 1000.0, (int) fs / 5), (int) fs / 20);
        EXPECT_GT (r, prev) << "sustain " << k;
        prev = r;
    }
}

namespace
{
    struct Chain
    {
        BmpInputStage in;
        BmpClippingStage c1 { BmpClippingStage::Load::NextClippingStage };
        BmpClippingStage c2 { BmpClippingStage::Load::ToneStackAndOutputStage };
        BmpOutputStage out;

        Chain (float sustain, float tone)
        {
            setKnobs (sustain, tone);
            out.setIncludeToneStack (true);
            in.prepare (fs); c1.prepare (fs); c2.prepare (fs); out.prepare (fs);
        }
        void setKnobs (float sustain, float tone) { in.setSustain (sustain); c2.setTone (tone); out.setTone (tone); }
        float process (float x) { return out.processSample (c2.processSample (c1.processSample (in.processSample (x)))); }
    };

    float bass (int i) { return (float) (0.3 * std::sin (2.0 * 3.14159265358979 * 110.0 * i / fs)); }
}

TEST (StageTest, KnobChangesMidStreamStayFiniteAndSmooth)
{
    // Reference: the largest sample-to-sample step of the fuzz output with the knobs
    // held at their extremes (the clipped square wave has steep but finite edges).
    float staticMax = 0.0f;
    for (float s : { 0.0f, 1.0f })
        for (float t : { 0.0f, 1.0f })
        {
            Chain ch (s, t);
            float prev = 0.0f;
            for (int i = 0; i < (int) fs / 2; ++i)
            {
                const float y = ch.process (bass (i));
                if (i > 2000) staticMax = std::max (staticMax, std::abs (y - prev));
                prev = y;
            }
        }

    // Now send both knobs to new random positions every ~10 ms, each move ramped
    // linearly over 20 ms exactly like the plugin's SmoothedValue (PluginProcessor:
    // smoother.reset (rate, 0.02)). No click may exceed the steepest edge of the
    // signal itself by more than 25 %, and nothing may go non-finite or out of range.
    // NOTE: an *instantaneous* tone jump is not tested on purpose: it moves the wiper
    // between the LP and HP nodes, whose instantaneous voltages differ by up to ~0.5 V
    // on the fuzz edges, and Q4 amplifies that step - the real pedal does the same,
    // which is why the plugin smooths the parameters.
    Chain ch (0.5f, 0.5f);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> knob (0.0f, 1.0f);
    float prevY = 0.0f, maxStep = 0.0f;
    float s = 0.5f, t = 0.5f, ds = 0.0f, dt = 0.0f;
    const int ramp = (int) (0.02 * fs);
    int rampLeft = 0;
    for (int i = 0; i < (int) fs; ++i)
    {
        if (i % 2000 == 0)
        {
            ds = (knob (rng) - s) / (float) ramp;
            dt = (knob (rng) - t) / (float) ramp;
            rampLeft = ramp;
        }
        if (rampLeft > 0)
        {
            s += ds; t += dt; --rampLeft;
            ch.setKnobs (s, t);
        }
        const float y = ch.process (bass (i));
        ASSERT_TRUE (std::isfinite (y));
        ASSERT_LT (std::abs (y), 9.0f);
        if (i > 2000)
            maxStep = std::max (maxStep, std::abs (y - prevY));
        prevY = y;
    }
    EXPECT_GT (staticMax, 0.0f);
    EXPECT_LE (maxStep, 1.25f * staticMax) << "static max step " << staticMax;
}
