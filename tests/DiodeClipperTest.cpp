#include <gtest/gtest.h>
#include <cmath>
#include "dsp/DiodeClipper.h"
#include "dsp/Oversampler.h"

//==============================================================================
// Diode Clipper Tests
//==============================================================================

namespace
{
    // Residual of the clipper's implicit equation (memoryless case)
    double residual (double vin, double vout, double R, double Is = 2.52e-9, double n = 1.75, double vt = 0.02585)
    {
        return (vout - vin) / R + 2.0 * Is * std::sinh (vout / (n * vt));
    }
}

TEST(DiodeClipperTest, ConstructionAndReset)
{
    DiodeClipper clipper;
    clipper.reset();  // Should not crash
}

TEST(DiodeClipperTest, DiodeParameterSetting)
{
    DiodeClipper clipper;
    
    // Default parameters (1N4148-like)
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    // Custom parameters
    clipper.setDiodeParams(1e-12f, 1.0f, 0.025f);
    clipper.setSeriesResistance(100.0f);
    
    // Should not crash
    clipper.processSample(0.5f);
}

TEST(DiodeClipperTest, Symmetry)
{
    DiodeClipper clipper;
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    // Anti-series diodes should give roughly symmetric clipping
    for (float in = -2.0f; in <= 2.0f; in += 0.1f)
    {
        float outPos = clipper.processSample(in);
        float outNeg = clipper.processSample(-in);
        
        // Should be roughly anti-symmetric (odd function)
        EXPECT_NEAR(outPos, -outNeg, 0.05f);
    }
}

TEST(DiodeClipperTest, OutputBounded)
{
    DiodeClipper clipper;
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    // Large inputs should be clipped to reasonable range
    float out1 = clipper.processSample(10.0f);
    float out2 = clipper.processSample(-10.0f);
    float out3 = clipper.processSample(100.0f);
    float out4 = clipper.processSample(-100.0f);
    
    EXPECT_TRUE(std::isfinite(out1));
    EXPECT_TRUE(std::isfinite(out2));
    EXPECT_TRUE(std::isfinite(out3));
    EXPECT_TRUE(std::isfinite(out4));
    
    // Output should be bounded (roughly ±2V for 390Ω series R)
    EXPECT_LT(std::abs(out1), 5.0f);
    EXPECT_LT(std::abs(out2), 5.0f);
    EXPECT_LT(std::abs(out3), 5.0f);
    EXPECT_LT(std::abs(out4), 5.0f);
}

TEST(DiodeClipperTest, SmallSignalLinearity)
{
    DiodeClipper clipper;
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    // Very small signals should pass through nearly linearly
    float in = 0.001f;
    float out = clipper.processSample(in);
    
    // Gain should be close to 1 (minus series resistance drop)
    float gain = out / in;
    EXPECT_NEAR(gain, 1.0f, 0.1f);  // Within 10%
}

TEST(DiodeClipperTest, ClippingBehavior)
{
    DiodeClipper clipper;
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    // Test progressive clipping
    float outSmall = clipper.processSample(0.1f);
    float outMed = clipper.processSample(0.5f);
    float outLarge = clipper.processSample(2.0f);
    
    // Should compress progressively
    float gainSmall = outSmall / 0.1f;
    float gainMed = outMed / 0.5f;
    float gainLarge = outLarge / 2.0f;
    
    EXPECT_GT(gainSmall, gainMed);
    EXPECT_GT(gainMed, gainLarge);
}

TEST(DiodeClipperTest, NewtonRaphsonConvergence)
{
    DiodeClipper clipper;
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    // Test many input values for convergence
    for (float in = -5.0f; in <= 5.0f; in += 0.05f)
    {
        float out = clipper.processSample(in);
        EXPECT_TRUE(std::isfinite(out)) << "Failed at input: " << in;
        EXPECT_LE(std::abs(out), 10.0f) << "Output unbounded at input: " << in;
    }
}

TEST(DiodeClipperTest, ProcessBlock)
{
    DiodeClipper clipper;
    clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper.setSeriesResistance(390.0f);
    
    const int N = 1024;
    float buffer[N];
    
    // Fill with sine wave
    for (int i = 0; i < N; ++i)
    {
        buffer[i] = 1.0f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f);
    }
    
    clipper.process(buffer, N);
    
    // Check all outputs finite
    for (int i = 0; i < N; ++i)
    {
        EXPECT_TRUE(std::isfinite(buffer[i]));
    }
    
    // Check clipping occurred (peaks should be flattened)
    float maxVal = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        maxVal = std::max(maxVal, std::abs(buffer[i]));
    }
    
    // Peak should be less than input peak (1.0) due to clipping
    EXPECT_LT(maxVal, 0.95f);
}

TEST(DiodeClipperTest, SeriesResistanceEffect)
{
    DiodeClipper clipper1, clipper2;
    clipper1.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper2.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
    clipper1.setSeriesResistance(100.0f);  // Low R = softer clipping (less drop across R)
    clipper2.setSeriesResistance(1000.0f); // High R = harder clipping
    
    float in = 1.0f;
    float out1 = clipper1.processSample(in);
    float out2 = clipper2.processSample(in);
    
    // Higher series resistance = more voltage drop = more clipping (lower output).
    // NOTE: this assertion previously read EXPECT_LT(|out1|, |out2|), i.e. it
    // expected the 100 ohm clipper to give the LOWER output, contradicting the
    // comment above and the circuit (series R into a shunt diode pair: the
    // diode current I satisfies Vout = Vin - I*R, so larger R => lower Vout).
    // Fixed to match the physics the comment describes.
    EXPECT_GT(std::abs(out1), std::abs(out2));
}

TEST(DiodeClipperTest, IdealityFactorEffect)
{
    DiodeClipper clipper1, clipper2;
    clipper1.setDiodeParams(2.52e-9f, 1.0f, 0.02585f);  // Ideal diode
    clipper2.setDiodeParams(2.52e-9f, 2.0f, 0.02585f);  // Non-ideal
    clipper1.setSeriesResistance(390.0f);
    clipper2.setSeriesResistance(390.0f);
    
    float in = 0.5f;
    float out1 = clipper1.processSample(in);
    float out2 = clipper2.processSample(in);
    
    // Different ideality factors give different knee sharpness
    EXPECT_NE(out1, out2);
}

//==============================================================================
// Added: solver accuracy and robustness
//==============================================================================

// The returned voltage must actually solve the implicit equation, across a huge input range.
TEST(DiodeClipperTest, SolverSatisfiesEquation)
{
    for (double R : { 100.0, 390.0, 1000.0, 100000.0 })
    {
        DiodeClipper clipper;
        clipper.setDiodeParams(2.52e-9f, 1.75f, 0.02585f);
        clipper.setSeriesResistance((float) R);

        for (float in : { -1e6f, -100.0f, -10.0f, -1.0f, -0.3f, -1e-4f, 0.0f, 1e-4f, 0.3f, 1.0f, 10.0f, 100.0f, 1e6f })
        {
            const float out = clipper.processSample(in);
            ASSERT_TRUE(std::isfinite(out)) << "R=" << R << " in=" << in;
            // Residual current small relative to the input current scale (float output rounding)
            const double scale = std::max(1e-9, std::abs((double) in) / R);
            EXPECT_LT(std::abs(residual(in, out, R)) / scale, 1e-3) << "R=" << R << " in=" << in;
            EXPECT_LE(clipper.getLastIterationCount(), 50);
        }
    }
}

// DC sweep: output is monotonic, odd, and bounded by about one diode drop (~1 V).
TEST(DiodeClipperTest, DcTransferMonotonicAndBounded)
{
    DiodeClipper clipper;
    clipper.setSeriesResistance(390.0f);

    float prev = -1e9f;
    for (float in = -50.0f; in <= 50.0f; in += 0.01f)
    {
        const float out = clipper.processSample(in);
        EXPECT_GE(out, prev - 1e-6f);
        EXPECT_LT(std::abs(out), 1.0f);
        prev = out;
    }
}

// Non-finite input must not poison the output.
TEST(DiodeClipperTest, NonFiniteInputIsSafe)
{
    DiodeClipper clipper;
    EXPECT_TRUE(std::isfinite(clipper.processSample(std::numeric_limits<float>::quiet_NaN())));
    EXPECT_TRUE(std::isfinite(clipper.processSample(std::numeric_limits<float>::infinity())));
    EXPECT_TRUE(std::isfinite(clipper.processSample(-std::numeric_limits<float>::infinity())));
}

// Loud sine through the ADAA path stays bounded and tracks the plain solver.
TEST(DiodeClipperTest, AdaaSineBoundedAndTracksStatic)
{
    DiodeClipper adaa, plain;
    adaa.setSeriesResistance(390.0f);
    plain.setSeriesResistance(390.0f);
    adaa.reset();

    const float fs = 192000.0f;
    float maxDiff = 0.0f, peak = 0.0f;
    float prevPlain = 0.0f, prevIn = 0.0f;
    for (int i = 0; i < 4096; ++i)
    {
        const float in = 20.0f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * (float) i / fs);
        const float y = adaa.processSampleADAA(in);
        ASSERT_TRUE(std::isfinite(y));
        peak = std::max(peak, std::abs(y));

        // ADAA ~ static curve evaluated half a sample back
        const float ref = plain.processSample(0.5f * (in + prevIn));
        if (i > 0) maxDiff = std::max(maxDiff, std::abs(y - ref));
        prevPlain = ref;
        prevIn = in;
    }
    juce::ignoreUnused(prevPlain);
    EXPECT_LT(peak, 1.0f);
    EXPECT_LT(maxDiff, 0.02f);
}

// With a capacitor across the diodes the clipper becomes an RC low-pass for small signals.
TEST(DiodeClipperTest, CapacitorMakesFirstOrderLowPass)
{
    const double fs = 48000.0;
    DiodeClipper rc;
    rc.setSeriesResistance(10000.0f);
    rc.setCapacitance(10e-9f, fs);   // fc = 1/(2*pi*10k*10n) ~ 1.59 kHz

    // Small step: output rises to ~63% after one time constant (100 us ~ 4.8 samples)
    const float step = 0.01f;
    float y = 0.0f;
    for (int i = 0; i < 5; ++i)
        y = rc.processSample(step);
    EXPECT_GT(y, 0.5f * step);
    EXPECT_LT(y, 0.8f * step);

    for (int i = 0; i < 2000; ++i)
        y = rc.processSample(step);
    EXPECT_NEAR(y, step, 1e-4f);
}

//==============================================================================
// Oversampler latency
//==============================================================================

TEST(OversamplerTest, ReportsIntegerLatencyMatchingImpulse)
{
    Oversampler os(4);
    const int block = 256;
    os.prepare(48000.0, block);

    const int latency = os.getLatencyInSamples();
    EXPECT_GT(latency, 0);

    juce::AudioBuffer<float> base(1, block), up(1, block * 4);
    base.clear();
    base.setSample(0, 0, 1.0f);

    juce::dsp::AudioBlock<float> baseBlock(base);
    juce::dsp::AudioBlock<float> upBlock(up);
    os.upsample(baseBlock, upBlock);
    os.downsample(upBlock, baseBlock);

    int peakIndex = 0;
    for (int i = 1; i < block; ++i)
        if (std::abs(base.getSample(0, i)) > std::abs(base.getSample(0, peakIndex)))
            peakIndex = i;

    EXPECT_NEAR(peakIndex, latency, 1);
}

// Blocks up to the prepared size work (no fixed 4096 limit)
TEST(OversamplerTest, LargePreparedBlock)
{
    Oversampler os(4);
    const int block = 8192;
    os.prepare(48000.0, block);

    juce::AudioBuffer<float> base(1, block), up(1, block * 4);
    for (int i = 0; i < block; ++i)
        base.setSample(0, i, std::sin(0.01f * (float) i));

    juce::dsp::AudioBlock<float> baseBlock(base);
    juce::dsp::AudioBlock<float> upBlock(up);
    os.upsample(baseBlock, upBlock);
    os.downsample(upBlock, baseBlock);

    for (int i = 0; i < block; ++i)
        ASSERT_TRUE(std::isfinite(base.getSample(0, i)));
}
