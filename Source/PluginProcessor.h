#pragma once

#include <JuceHeader.h>
#include <vector>
#include "dsp/InputPad.h"
#include "dsp/BmpInputStage.h"
#include "dsp/BmpClippingStage.h"
#include "dsp/BmpToneStack.h"
#include "dsp/BmpOutputStage.h"
#include "dsp/CrossoverFilters.h"
#include "dsp/NoiseGate.h"
#include "dsp/Blend.h"
#include "dsp/Oversampler.h"

//==============================================================================
/**
    Audio processor for the DeluxeBassFuzz plugin.
    Models the Deluxe Bass Fuzz circuit.
*/
class DeluxeBassFuzzAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    DeluxeBassFuzzAudioProcessor();
    ~DeluxeBassFuzzAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // The panel Bypass switch is the host bypass, so the wrapper does not add a second one.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    // Parameter access
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    const juce::AudioProcessorValueTreeState& getParameters() const { return parameters; }

    // Parameter IDs
    static constexpr const char* paramSustain   = "sustain";
    static constexpr const char* paramTone      = "tone";
    static constexpr const char* paramVolume    = "volume";
    static constexpr const char* paramBlend     = "blend";
    static constexpr const char* paramGate      = "gate";
    static constexpr const char* paramHPF       = "hpf";
    static constexpr const char* paramLPF       = "lpf";
    static constexpr const char* paramCrossover = "crossover";
    static constexpr const char* paramInputPad  = "inputPad";
    static constexpr const char* paramBypass    = "bypass";

private:
    //==============================================================================
    /** Fixed integer delay used to latency-align the dry/key/bypass paths with
        the oversampled wet path. Storage is allocated in prepare() only. */
    struct DelayLine
    {
        void prepare (int delaySamples)
        {
            delay = juce::jmax (0, delaySamples);
            buffer.assign ((size_t) delay + 1, 0.0f);
            writePos = 0;
        }

        float processSample (float x) noexcept
        {
            if (delay == 0)
                return x;
            buffer[(size_t) writePos] = x;
            int readPos = writePos - delay;
            if (readPos < 0)
                readPos += (int) buffer.size();
            const float y = buffer[(size_t) readPos];
            if (++writePos >= (int) buffer.size())
                writePos = 0;
            return y;
        }

        void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

        std::vector<float> buffer;
        int writePos = 0;
        int delay = 0;
    };

    //==============================================================================
    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    // DSP chain components
    InputPad inputPad;
    CrossoverFilters crossoverHPF;  // HPF on wet path (125 Hz - 1.75 kHz)
    CrossoverFilters crossoverLPF;  // LPF on dry path (60 Hz - 10 kHz)
    BmpInputStage inputStage;
    BmpClippingStage clippingStage1;
    BmpClippingStage clippingStage2;
    BmpOutputStage outputStage;
    NoiseGate noiseGate;
    Blend blend;

    // Oversampling for the nonlinear stages
    Oversampler oversampler;

    // Latency compensation for the non-oversampled paths
    DelayLine dryDelay;     // padded input -> dry path and gate key
    DelayLine bypassDelay;  // raw input -> bypass path

    // Internal work buffers (sized in prepareToPlay)
    juce::AudioBuffer<float> oversampledBuffer;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> keyBuffer;
    int maxChunkSize = 512;

    // Parameter smoothing (sustain/tone run at the oversampled rate, the rest at the base rate)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSustain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTone;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedVolume;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBlend;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHPF;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLPF;

    // Bypass state
    bool bypassState = false;

    //==============================================================================
    void prepareInternal (double sampleRate, int samplesPerBlock);
    void updateParameters();
    void processChunk (const float* input, float* outL, float* outR, int numSamples);
    void applyOversampledProcessing (float* wet, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeluxeBassFuzzAudioProcessor)
};
