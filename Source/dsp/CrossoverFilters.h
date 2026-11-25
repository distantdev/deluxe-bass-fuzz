#pragma once

#include <JuceHeader.h>
#include <cmath>

//==============================================================================
/**
    2nd-order Butterworth crossover filters for the Deluxe Bass Fuzz.
    HPF: 125 Hz - 1.75 kHz on wet path
    LPF: 60 Hz - 10 kHz on dry path

    Self-contained biquad (bilinear transform, RBJ form, Q = 1/sqrt(2)) with
    plain-double coefficients, so cutoff changes are allocation-free and can
    be applied per sample from a smoothed parameter. Transposed Direct Form II.
*/
class CrossoverFilters
{
public:
    enum class FilterType
    {
        HighPass,
        LowPass
    };

    CrossoverFilters (FilterType filterType = FilterType::HighPass)
        : type (filterType)
    {
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        currentCutoff = -1.0f;           // force recompute
        updateCoefficients (targetCutoff);
        reset();
    }

    // Allocation-free; only recomputes when the cutoff actually changes
    void setCutoff (float cutoffHz)
    {
        targetCutoff = juce::jlimit (20.0f, (float) (fs * 0.45), cutoffHz);
        if (targetCutoff != currentCutoff)
            updateCoefficients (targetCutoff);
    }

    void setBypassed (bool shouldBypass) noexcept { bypassed = shouldBypass; }
    bool isBypassed() const noexcept              { return bypassed; }

    float processSample (float x) noexcept
    {
        const double in = (double) x;
        const double y = b0 * in + s1;
        s1 = b1 * in - a1 * y + s2;
        s2 = b2 * in - a2 * y;
        return (float) y;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (bypassed)
            return;

        // Mono model: one filter state shared by all channels
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            process (buffer.getWritePointer (ch), buffer.getNumSamples());
    }

    void process (float* samples, int numSamples)
    {
        if (bypassed)
            return;
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

    void reset() noexcept
    {
        s1 = s2 = 0.0;
    }

private:
    void updateCoefficients (float cutoffHz)
    {
        currentCutoff = cutoffHz;

        const double w0 = 2.0 * juce::MathConstants<double>::pi * (double) cutoffHz / fs;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        if (type == FilterType::HighPass)
        {
            b0 = (1.0 + cosw) * 0.5 / a0;
            b1 = -(1.0 + cosw) / a0;
            b2 = b0;
        }
        else
        {
            b0 = (1.0 - cosw) * 0.5 / a0;
            b1 = (1.0 - cosw) / a0;
            b2 = b0;
        }

        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    static constexpr double q = 0.70710678118654752;

    FilterType type;
    bool bypassed = false;
    double fs = 44100.0;
    float targetCutoff = 1000.0f;
    float currentCutoff = -1.0f;

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;
};
