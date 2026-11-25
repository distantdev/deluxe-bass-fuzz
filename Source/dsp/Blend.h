#pragma once

#include <JuceHeader.h>
#include <cmath>

//==============================================================================
/**
    Blend/Mix control for the Deluxe Bass Fuzz.
    Constant-power crossfade between dry and wet signals.
    Matches the hardware BLEND pot behavior.
*/
class Blend
{
public:
    Blend() = default;

    void prepare (double /*sampleRate*/)
    {
        setMix (mix);
    }

    void setMix (float mixValue)
    {
        mix = juce::jlimit (0.0f, 1.0f, mixValue);
        dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
    }

    float processSample (float dry, float wet) const noexcept
    {
        return dry * dryGain + wet * wetGain;
    }

    void process (juce::AudioBuffer<float>& dryBuffer, const juce::AudioBuffer<float>& wetBuffer, int numSamples)
    {
        for (int ch = 0; ch < dryBuffer.getNumChannels(); ++ch)
        {
            auto* dryData = dryBuffer.getWritePointer (ch);
            const float* wetData = wetBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                dryData[i] = processSample (dryData[i], wetData[i]);
        }
    }

    void reset() {}

private:
    float mix = 0.5f;
    float dryGain = 0.70710678f;
    float wetGain = 0.70710678f;
};
