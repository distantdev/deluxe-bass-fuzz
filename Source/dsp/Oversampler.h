#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    2x/4x oversampler using JUCE's dsp::Oversampling with half-band
    equiripple FIR filters (linear phase). Used around the nonlinear stages
    to reduce aliasing.

    prepare() takes the host's maximum block size so no fixed limit is baked
    in; latency is rounded to an integer number of base-rate samples so it can
    be reported to the host and matched on the dry path.
*/
class Oversampler
{
public:
    Oversampler (int oversamplingFactor = 4)
        : factor (oversamplingFactor == 2 ? 2 : 4)
    {
        jassert (oversamplingFactor == 2 || oversamplingFactor == 4);
    }

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1,                                   // numChannels
            (size_t) (factor == 4 ? 2 : 1),      // JUCE takes log2 of the factor
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true,                                // isMaxQuality
            true);                               // useIntegerLatency

        oversampling->initProcessing ((size_t) maxBlock);
    }

    int getOversamplingFactor() const noexcept { return factor; }
    int getMaxBlockSize() const noexcept       { return maxBlock; }
    double getOversampledRate() const noexcept { return fs * factor; }

    // Latency in base-rate samples (integer because useIntegerLatency = true)
    int getLatencyInSamples() const
    {
        return oversampling ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    // Upsample: input at base rate -> output at oversampled rate
    void upsample (const juce::dsp::AudioBlock<float>& input, juce::dsp::AudioBlock<float>& output)
    {
        jassert ((int) input.getNumSamples() <= maxBlock);
        // JUCE upsamples into its own internal buffer; copy it out for the caller
        lastUpBlock = oversampling->processSamplesUp (juce::dsp::AudioBlock<const float> (input));
        output.copyFrom (lastUpBlock);
    }

    // Downsample: input at oversampled rate -> output at base rate
    void downsample (const juce::dsp::AudioBlock<float>& input, juce::dsp::AudioBlock<float>& output)
    {
        // JUCE downsamples from its internal buffer, so put the processed samples back there first
        lastUpBlock.copyFrom (input);
        oversampling->processSamplesDown (output);
    }

    void reset()
    {
        if (oversampling)
            oversampling->reset();
    }

private:
    int factor = 4;
    int maxBlock = 512;
    double fs = 44100.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::AudioBlock<float> lastUpBlock;
};
