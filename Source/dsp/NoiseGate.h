#pragma once

#include <JuceHeader.h>
#include <cmath>

//==============================================================================
/**
    Noise Gate for the Deluxe Bass Fuzz.
    Downward expander with envelope follower.
    Keyed from the dry input (pre-fuzz, pre-crossover) to preserve low-note sustain.
*/
class NoiseGate
{
public:
    NoiseGate() = default;

    static constexpr float minThresholdDb = -80.0f;
    static constexpr float maxThresholdDb = -20.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        attackCoeff  = coeffFor (attackTime);
        releaseCoeff = coeffFor (releaseTime);
        envelope = 0.0f;
    }

    // Threshold in dBFS (clamped to -80..0)
    void setThreshold (float thresholdDb)
    {
        threshold = juce::jlimit (-80.0f, 0.0f, thresholdDb);
    }

    // GATE knob 0..1 -> -80..-20 dBFS (per docs/circuit-notes.md)
    void setThresholdFromKnob (float knob01)
    {
        setThreshold (knobToThresholdDb (knob01));
    }

    static float knobToThresholdDb (float knob01)
    {
        return juce::jmap (juce::jlimit (0.0f, 1.0f, knob01), minThresholdDb, maxThresholdDb);
    }

    float getThreshold() const noexcept { return threshold; }

    void setRange (float rangeDb)
    {
        range = juce::jlimit (-80.0f, 0.0f, rangeDb);
    }

    void setAttackTime (float timeMs)
    {
        attackTime = juce::jmax (0.01f, timeMs) / 1000.0f;
        attackCoeff = coeffFor (attackTime);
    }

    void setReleaseTime (float timeMs)
    {
        releaseTime = juce::jmax (0.01f, timeMs) / 1000.0f;
        releaseCoeff = coeffFor (releaseTime);
    }

    // Gain to apply to the wet sample, given the key (dry) sample
    float processSample (float wet, float key)
    {
        const float inputLevel = std::abs (key);

        if (inputLevel > envelope)
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * inputLevel;
        else
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevel;

        const float envDb = juce::Decibels::gainToDecibels (envelope, -100.0f);

        float gainReductionDb = 0.0f;
        if (envDb < threshold)
        {
            gainReductionDb = (envDb - threshold) * (1.0f - 1.0f / ratio);
            gainReductionDb = juce::jmax (gainReductionDb, range);
        }

        return wet * juce::Decibels::decibelsToGain (gainReductionDb);
    }

    void process (juce::AudioBuffer<float>& wetBuffer, const juce::AudioBuffer<float>& keyBuffer, int numSamples)
    {
        // Mono model: one envelope; channel 0 of the key drives all wet channels
        const float* keyData = keyBuffer.getReadPointer (0);
        for (int ch = 0; ch < wetBuffer.getNumChannels(); ++ch)
        {
            auto* wetData = wetBuffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                wetData[i] = processSample (wetData[i], keyData[i]);
        }
    }

    void reset()
    {
        envelope = 0.0f;
    }

private:
    float coeffFor (float timeSeconds) const
    {
        return (float) std::exp (-1.0 / ((double) timeSeconds * fs));
    }

    double fs = 44100.0;
    float threshold = -80.0f;  // dB
    float range = -60.0f;      // Max attenuation (dB)
    float ratio = 10.0f;       // Expansion ratio
    float envelope = 0.0f;
    float attackTime = 0.0005f;  // 0.5 ms
    float releaseTime = 0.05f;   // 50 ms
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
};
