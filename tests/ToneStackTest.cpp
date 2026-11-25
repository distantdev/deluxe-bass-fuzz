#include <gtest/gtest.h>
#include <complex>
#include "dsp/BmpToneStack.h"

//==============================================================================
// Tone Stack Tests
//
// Component values (validated): R_LP = 11.5k (circuit reference, "Classic Bass Fuzz transistors and
// other components": "Deluxe Bass Fuzz - the low pass resistor value was
// 11.5k"; HP resistor 22k), C_LP = 10n, C_HP = 3n9, R_HP = 22k, 100k pot.
// The previous 39k/10n low-pass (408 Hz) became 11.5k/10n (1384 Hz). With the
// LP corner this close to the HP corner (1855 Hz) the noon scoop is shallow
// (~2.5 dB) and centred near 2 kHz instead of ~1 kHz / >6 dB. The expected
// values below were recomputed from the nodal analysis of the circuit
// (BmpToneStack::getAnalogResponse), not fitted to the DSP output.
//==============================================================================

namespace
{
    float magDb (const BmpToneStack& ts, float f)
    {
        return 20.0f * std::log10 (std::abs (ts.getFrequencyResponse (f)));
    }

    double analogDb (double f, double tone)
    {
        return 20.0 * std::log10 (std::abs (BmpToneStack::getAnalogResponse (f, tone)));
    }
}

TEST(ToneStackTest, PrepareAndReset)
{
    BmpToneStack ts;
    ts.prepare(48000.0);
    ts.reset();
    
    float testSignal[10] = { 1.0f, 0.5f, -0.5f, 0.0f, 0.25f, -0.25f, 0.0f, 0.0f, 0.0f, 0.0f };
    ts.process(testSignal, 10);
    
    // Should not crash and should produce finite output
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(std::isfinite(testSignal[i]));
    }
}

TEST(ToneStackTest, ToneParameterRange)
{
    BmpToneStack ts;
    ts.prepare(48000.0);
    
    // Test boundary values
    ts.setTone(0.0f);   // Full bass
    ts.setTone(1.0f);   // Full treble
    ts.setTone(0.5f);   // Noon
    
    // Test clamping
    ts.setTone(-0.5f);  // Should clamp to 0
    EXPECT_FLOAT_EQ(ts.getTone(), 0.0f);
    ts.setTone(1.5f);   // Should clamp to 1
    EXPECT_FLOAT_EQ(ts.getTone(), 1.0f);
    
    float testSignal[1] = { 1.0f };
    ts.process(testSignal, 1);
    EXPECT_TRUE(std::isfinite(testSignal[0]));
}

TEST(ToneStackTest, FrequencyResponseAtNoon)
{
    BmpToneStack ts;
    ts.prepare(48000.0);
    ts.setTone(0.5f);
    
    // At noon the scoop sits near 2 kHz (analog notch 2.04 kHz, between the
    // 1.38 kHz LP and 1.86 kHz HP RC corners, pushed up by pot loading).
    // NOTE (validated): with the old 39k LP resistor this test expected the scoop
    // at 800 Hz and >6 dB deep. With circuit reference's 11.5k the analog circuit gives
    // 800 Hz = -6.45 dB, 3 kHz = -7.54 dB (800 Hz is no longer below 3 kHz) and
    // a 2.5 dB scoop, so the old expectations are physically impossible.
    float mag2000  = magDb(ts, 2000.0f);
    float mag100   = magDb(ts, 100.0f);
    float mag10000 = magDb(ts, 10000.0f);
    
    EXPECT_LT(mag2000, mag100);
    EXPECT_LT(mag2000, mag10000);
    
    // Analog: 100 Hz is 2.47 dB and 10 kHz is 1.61 dB above the 2 kHz scoop
    EXPECT_NEAR(mag100 - mag2000, 2.47f, 0.25f);
    EXPECT_NEAR(mag10000 - mag2000, 1.61f, 0.25f);
}

TEST(ToneStackTest, FrequencyResponseBass)
{
    BmpToneStack ts;
    ts.prepare(48000.0);
    ts.setTone(0.0f);  // Full bass
    
    float mag100 = magDb(ts, 100.0f);
    float mag1000 = magDb(ts, 1000.0f);
    float mag5000 = magDb(ts, 5000.0f);
    
    // Bass setting: low frequencies boosted, highs cut
    EXPECT_GT(mag100, mag1000);
    EXPECT_GT(mag1000, mag5000);
}

TEST(ToneStackTest, FrequencyResponseTreble)
{
    BmpToneStack ts;
    ts.prepare(48000.0);
    ts.setTone(1.0f);  // Full treble
    
    float mag100 = magDb(ts, 100.0f);
    float mag1000 = magDb(ts, 1000.0f);
    float mag5000 = magDb(ts, 5000.0f);
    
    // Treble setting: highs boosted, lows cut
    EXPECT_LT(mag100, mag1000);
    EXPECT_LT(mag1000, mag5000);
}

TEST(ToneStackTest, CutoffFrequencies)
{
    BmpToneStack ts;
    ts.prepare(192000.0);

    // Bass extreme: the wiper sits on the LOW-PASS node. The bare RC corner is
    // 1/(2*pi*11.5k*10n) = 1384 Hz; the 100k pot + 22k loading the node moves
    // the -3 dB point (relative to the -0.78 dB DC level) to 1607 Hz.
    ts.setTone(0.0f);
    {
        const float ref = magDb(ts, 10.0f);
        float corner = 0.0f;
        for (float f = 200.0f; f < 20000.0f; f *= 1.001f)
            if (magDb(ts, f) < ref - 3.0f) { corner = f; break; }
        EXPECT_NEAR(corner, 1607.0f, 0.05f * 1607.0f);
        EXPECT_NEAR(magDb(ts, 1384.0f), -3.20f, 0.3f);   // analog value at the RC corner
    }

    // Treble extreme: the wiper sits on the HIGH-PASS node (3n9 / 22k, RC corner
    // 1855 Hz). Pot loading moves the -3 dB point (relative to the HF shelf) to 2608 Hz.
    ts.setTone(1.0f);
    {
        const float ref = magDb(ts, 40000.0f);
        float corner = 0.0f;
        for (float f = 20000.0f; f > 100.0f; f /= 1.001f)
            if (magDb(ts, f) < ref - 3.0f) { corner = f; break; }
        EXPECT_NEAR(corner, 2608.0f, 0.05f * 2608.0f);
    }
}

TEST(ToneStackTest, ProcessBuffer)
{
    BmpToneStack ts;
    ts.prepare(48000.0);
    
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    
    // Fill with test signal
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < 512; ++i)
        {
            data[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f);
        }
    }
    
    ts.process(buffer);
    
    // Check output is finite and non-zero
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        bool hasSignal = false;
        for (int i = 0; i < 512; ++i)
        {
            EXPECT_TRUE(std::isfinite(data[i]));
            if (std::abs(data[i]) > 0.001f) hasSignal = true;
        }
        EXPECT_TRUE(hasSignal);
    }
}

//==============================================================================
// Added: white-box model checks
//==============================================================================

// The discretised filter must match the nodal-analysis (analog) response.
TEST(ToneStackTest, MatchesAnalogCircuit)
{
    for (double fs : { 48000.0, 192000.0 })
    {
        BmpToneStack ts;
        ts.prepare(fs);

        for (float tone : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            ts.setTone(tone);
            for (float f : { 50.0f, 100.0f, 1000.0f, 1384.0f, 1808.0f, 2040.0f, 5000.0f })
            {
                const float digital = magDb(ts, f);
                const float analog = (float) analogDb (f, tone);
                EXPECT_NEAR(digital, analog, 0.5f) << "fs=" << fs << " tone=" << tone << " f=" << f;
            }
        }
    }
}

// At noon the notch should sit near 2 kHz (analog 2.04 kHz) and be 2.49 dB below
// the 50 Hz level and 1.78 dB below the 20 kHz level (analog values).
// NOTE (validated): previously 700-1400 Hz and >6 dB, which held for the 39k LP
// resistor only (analog notch 1.03 kHz, 6.9/7.9 dB).
TEST(ToneStackTest, ScoopCentreAndDepth)
{
    BmpToneStack ts;
    ts.prepare(192000.0);
    ts.setTone(0.5f);

    float minDb = 1000.0f, minF = 0.0f;
    for (float f = 200.0f; f < 5000.0f; f *= 1.001f)
    {
        const float m = magDb(ts, f);
        if (m < minDb) { minDb = m; minF = f; }
    }

    EXPECT_GT(minF, 1850.0f);
    EXPECT_LT(minF, 2250.0f);
    EXPECT_NEAR(magDb(ts, 50.0f) - minDb, 2.49f, 0.2f);
    EXPECT_NEAR(magDb(ts, 20000.0f) - minDb, 1.78f, 0.2f);
}

// Sweeping tone moves energy monotonically from bass to treble.
// NOTE (validated): the treble probe moved from 5 kHz to 10 kHz. With the 11.5k LP
// resistor the analog circuit itself is not monotonic at 5 kHz near full bass
// (tone 0: -10.77 dB, tone 0.1: -11.42 dB, tone 0.2: -11.04 dB) because the LP
// node still passes some 5 kHz; at 10 kHz it is monotonic (-16.4 ... -0.3 dB).
TEST(ToneStackTest, ToneSweepIsMonotonic)
{
    BmpToneStack ts;
    ts.prepare(48000.0);

    float prevLow = 1000.0f, prevHigh = -1000.0f;
    for (int i = 0; i <= 10; ++i)
    {
        ts.setTone((float) i / 10.0f);
        const float low = magDb(ts, 100.0f);
        const float high = magDb(ts, 10000.0f);
        EXPECT_LE(low, prevLow + 1e-4f);
        EXPECT_GE(high, prevHigh - 1e-4f);
        prevLow = low;
        prevHigh = high;
    }
}

// Time-domain check: steady-state sine amplitude through process() matches getFrequencyResponse().
TEST(ToneStackTest, TimeDomainMatchesResponse)
{
    const double fs = 48000.0;
    BmpToneStack ts;
    ts.prepare(fs);
    ts.setTone(0.5f);

    const float f = 1000.0f;
    const int N = 48000;
    float peak = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float y = ts.processSample(std::sin(2.0f * juce::MathConstants<float>::pi * f * (float) i / (float) fs));
        if (i > N / 2) peak = std::max(peak, std::abs(y));
    }

    EXPECT_NEAR(20.0f * std::log10(peak), magDb(ts, f), 0.1f);
}
