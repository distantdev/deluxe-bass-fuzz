#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Input pad: 0 dB or -10 dB attenuation for active pickups (smoothed).
*/
class InputPad
{
public:
    InputPad() = default;

    void prepare (double sampleRate)
    {
        smoother.reset (sampleRate, 0.01);
        smoother.setCurrentAndTargetValue (targetGain);
    }

    void setAttenuation (float attenuationDb)
    {
        targetGain = juce::Decibels::decibelsToGain (attenuationDb);
        smoother.setTargetValue (targetGain);
    }

    float processSample (float x)
    {
        return x * smoother.getNextValue();
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        // One gain ramp shared by all channels
        const int numChannels = buffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = smoother.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    float getCurrentGain() const noexcept { return smoother.getCurrentValue(); }

    void reset()
    {
        smoother.setCurrentAndTargetValue (targetGain);
    }

private:
    float targetGain = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother { 1.0f };
};
