#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DeluxeBassFuzzAudioProcessor::DeluxeBassFuzzAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
    ),
    parameters (*this, nullptr, juce::Identifier ("DeluxeBassFuzz"),
        {   // Parameter layout
            std::make_unique<juce::AudioParameterFloat> (paramSustain,   "Sustain",   0.0f, 1.0f, 0.5f),
            std::make_unique<juce::AudioParameterFloat> (paramTone,      "Tone",      0.0f, 1.0f, 0.5f),
            std::make_unique<juce::AudioParameterFloat> (paramVolume,    "Volume",    0.0f, 1.0f, 0.7f),
            std::make_unique<juce::AudioParameterFloat> (paramBlend,     "Blend",     0.0f, 1.0f, 0.5f),
            std::make_unique<juce::AudioParameterFloat> (paramGate,      "Gate",      0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> (paramHPF,       "Crossover HPF", 125.0f, 1750.0f, 500.0f),
            std::make_unique<juce::AudioParameterFloat> (paramLPF,       "Crossover LPF", 60.0f, 10000.0f, 200.0f),
            std::make_unique<juce::AudioParameterBool>  (paramCrossover, "Crossover", false),
            std::make_unique<juce::AudioParameterBool>  (paramInputPad,  "Input Pad", false),
            std::make_unique<juce::AudioParameterBool>  (paramBypass,    "Bypass",    false),
        }
    ),
    crossoverHPF (CrossoverFilters::FilterType::HighPass),
    crossoverLPF (CrossoverFilters::FilterType::LowPass),
    oversampler (4)  // 4x oversampling
{
    // Parameter updates are read in processBlock via smoothed values
    prepareInternal (44100.0, 512);
}

DeluxeBassFuzzAudioProcessor::~DeluxeBassFuzzAudioProcessor()
{
}

//==============================================================================
void DeluxeBassFuzzAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareInternal (sampleRate, samplesPerBlock);
}

void DeluxeBassFuzzAudioProcessor::prepareInternal (double sampleRate, int samplesPerBlock)
{
    maxChunkSize = juce::jmax (1, samplesPerBlock);

    oversampler.prepare (sampleRate, maxChunkSize);
    const int factor = oversampler.getOversamplingFactor();
    const double osRate = sampleRate * factor;

    oversampledBuffer.setSize (1, maxChunkSize * factor);
    dryBuffer.setSize (1, maxChunkSize);
    wetBuffer.setSize (1, maxChunkSize);
    keyBuffer.setSize (1, maxChunkSize);
    oversampledBuffer.clear();
    dryBuffer.clear();
    wetBuffer.clear();
    keyBuffer.clear();

    // Base-rate components
    inputPad.prepare (sampleRate);
    crossoverHPF.prepare (sampleRate);
    crossoverLPF.prepare (sampleRate);
    noiseGate.prepare (sampleRate);
    blend.prepare (sampleRate);

    // Components inside the oversampled span run at the oversampled rate
    // Q3 is loaded by the tone stack + Q4's input; the output stage simulates the
    // tone stack together with Q4 (see BmpClippingStage / BmpOutputStage)
    clippingStage1.setLoad (BmpClippingStage::Load::NextClippingStage);
    clippingStage2.setLoad (BmpClippingStage::Load::ToneStackAndOutputStage);
    outputStage.setIncludeToneStack (true);
    {
        const float sustain = parameters.getRawParameterValue (paramSustain)->load();
        const float tone = parameters.getRawParameterValue (paramTone)->load();
        inputStage.setSustain (sustain);
        clippingStage2.setTone (tone);
        outputStage.setTone (tone);
    }
    inputStage.prepare (osRate);
    clippingStage1.prepare (osRate);
    clippingStage2.prepare (osRate);
    outputStage.prepare (osRate);

    // Latency: the wet path is delayed by the oversampling filters; align the
    // dry, key and bypass paths with it and report it to the host.
    const int latency = oversampler.getLatencyInSamples();
    dryDelay.prepare (latency);
    bypassDelay.prepare (latency);
    setLatencySamples (latency);

    auto initSmoother = [this] (auto& smoother, double rate, const char* id)
    {
        smoother.reset (rate, 0.02);
        smoother.setCurrentAndTargetValue (parameters.getRawParameterValue (id)->load());
    };

    initSmoother (smoothedSustain, osRate, paramSustain);
    initSmoother (smoothedTone,    osRate, paramTone);
    initSmoother (smoothedVolume,  sampleRate, paramVolume);
    initSmoother (smoothedBlend,   sampleRate, paramBlend);
    initSmoother (smoothedGate,    sampleRate, paramGate);
    initSmoother (smoothedHPF,     sampleRate, paramHPF);
    initSmoother (smoothedLPF,     sampleRate, paramLPF);

    updateParameters();
    inputPad.reset();
}

void DeluxeBassFuzzAudioProcessor::releaseResources()
{
}

bool DeluxeBassFuzzAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono in, stereo out
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DeluxeBassFuzzAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0)
        return;

    bypassState = parameters.getRawParameterValue (paramBypass)->load() > 0.5f;
    updateParameters();

    smoothedSustain.setTargetValue (parameters.getRawParameterValue (paramSustain)->load());
    smoothedTone.setTargetValue    (parameters.getRawParameterValue (paramTone)->load());
    smoothedVolume.setTargetValue  (parameters.getRawParameterValue (paramVolume)->load());
    smoothedBlend.setTargetValue   (parameters.getRawParameterValue (paramBlend)->load());
    smoothedGate.setTargetValue    (parameters.getRawParameterValue (paramGate)->load());
    smoothedHPF.setTargetValue     (parameters.getRawParameterValue (paramHPF)->load());
    smoothedLPF.setTargetValue     (parameters.getRawParameterValue (paramLPF)->load());

    auto* channel0 = buffer.getWritePointer (0);
    auto* channel1 = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Process in chunks no larger than the prepared block size, so hosts that
    // send larger (or smaller) blocks never overrun the oversampler/work buffers.
    for (int start = 0; start < numSamples; start += maxChunkSize)
    {
        const int n = juce::jmin (maxChunkSize, numSamples - start);
        processChunk (channel0 + start, channel0 + start,
                      channel1 != nullptr ? channel1 + start : nullptr, n);
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
}

void DeluxeBassFuzzAudioProcessor::processChunk (const float* input, float* outL, float* outR, int numSamples)
{
    auto* dry = dryBuffer.getWritePointer (0);
    auto* wet = wetBuffer.getWritePointer (0);
    auto* key = keyBuffer.getWritePointer (0);

    const bool crossoverOn = parameters.getRawParameterValue (paramCrossover)->load() > 0.5f;

    // 1) Input pad, path split, latency alignment, crossover filters (base rate)
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = input[i];
        const float rawDelayed = bypassDelay.processSample (x);

        const float padded = inputPad.processSample (x);
        const float dryAligned = dryDelay.processSample (padded);

        float w = padded;
        float d = dryAligned;

        const float hpfCut = smoothedHPF.getNextValue();
        const float lpfCut = smoothedLPF.getNextValue();

        if (crossoverOn)
        {
            crossoverHPF.setCutoff (hpfCut);
            crossoverLPF.setCutoff (lpfCut);
            w = crossoverHPF.processSample (w);
            d = crossoverLPF.processSample (d);
        }

        wet[i] = w;
        dry[i] = d;
        // Gate key = latency-aligned, pre-crossover dry signal. While bypassed the
        // key buffer carries the latency-aligned raw input instead.
        key[i] = bypassState ? rawDelayed : dryAligned;
    }

    // 2) Classic Bass Fuzz core at the oversampled rate (kept running while bypassed so
    //    the filters/oversampler stay warm and switching back is seamless)
    applyOversampledProcessing (wet, numSamples);

    if (bypassState)
    {
        // Latency-aligned dry passthrough
        for (int i = 0; i < numSamples; ++i)
        {
            const float y = key[i];
            outL[i] = y;
            if (outR != nullptr)
                outR[i] = y;
        }
        // Keep base-rate smoothers moving
        smoothedVolume.skip (numSamples);
        smoothedBlend.skip (numSamples);
        smoothedGate.skip (numSamples);
        return;
    }

    // 3) Gate (keyed from dry), blend, volume (per-sample smoothing)
    for (int i = 0; i < numSamples; ++i)
    {
        noiseGate.setThresholdFromKnob (smoothedGate.getNextValue());
        const float gated = noiseGate.processSample (wet[i], key[i]);

        blend.setMix (smoothedBlend.getNextValue());
        const float mixed = blend.processSample (dry[i], gated);

        const float volumeGain = juce::Decibels::decibelsToGain (-60.0f + smoothedVolume.getNextValue() * 60.0f);
        const float y = mixed * volumeGain;

        outL[i] = y;
        if (outR != nullptr)
            outR[i] = y;  // mono to stereo
    }
}

void DeluxeBassFuzzAudioProcessor::applyOversampledProcessing (float* wet, int numSamples)
{
    const int osNumSamples = numSamples * oversampler.getOversamplingFactor();

    // Upsample using JUCE's oversampling
    {
        float* inChannels[] = { wet };
        juce::dsp::AudioBlock<float> inBlock (inChannels, 1, (size_t) numSamples);
        juce::dsp::AudioBlock<float> outBlock (oversampledBuffer.getArrayOfWritePointers(), 1, (size_t) osNumSamples);
        oversampler.upsample (inBlock, outBlock);
    }

    auto* os = oversampledBuffer.getWritePointer (0);

    for (int i = 0; i < osNumSamples; ++i)
    {
        const float sustain = smoothedSustain.getNextValue();
        inputStage.setSustain (sustain);
        const float tone = smoothedTone.getNextValue();
        clippingStage2.setTone (tone);   // Q3's load includes the tone stack
        outputStage.setTone (tone);

        float x = os[i];
        x = inputStage.processSample (x);      // Q1
        x = clippingStage1.processSample (x);  // Q2 + diodes
        x = clippingStage2.processSample (x);  // Q3 + diodes (loaded by tone stack + Q4 input)
        x = outputStage.processSample (x);     // passive tone stack + Q4 recovery
        os[i] = x;
    }

    // Downsample back
    {
        float* outChannels[] = { wet };
        juce::dsp::AudioBlock<float> inBlock (oversampledBuffer.getArrayOfWritePointers(), 1, (size_t) osNumSamples);
        juce::dsp::AudioBlock<float> outBlock (outChannels, 1, (size_t) numSamples);
        oversampler.downsample (inBlock, outBlock);
    }
}

//==============================================================================
juce::AudioProcessorEditor* DeluxeBassFuzzAudioProcessor::createEditor()
{
    return new DeluxeBassFuzzAudioProcessorEditor (*this);
}

bool DeluxeBassFuzzAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String DeluxeBassFuzzAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DeluxeBassFuzzAudioProcessor::acceptsMidi() const
{
    return false;
}

bool DeluxeBassFuzzAudioProcessor::producesMidi() const
{
    return false;
}

bool DeluxeBassFuzzAudioProcessor::isMidiEffect() const
{
    return false;
}

double DeluxeBassFuzzAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
int DeluxeBassFuzzAudioProcessor::getNumPrograms()
{
    return 1;
}

int DeluxeBassFuzzAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DeluxeBassFuzzAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String DeluxeBassFuzzAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void DeluxeBassFuzzAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void DeluxeBassFuzzAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DeluxeBassFuzzAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

void DeluxeBassFuzzAudioProcessor::updateParameters()
{
    // Block-rate switches (smoothed internally where it matters)
    const bool crossoverOn = parameters.getRawParameterValue (paramCrossover)->load() > 0.5f;
    crossoverHPF.setBypassed (! crossoverOn);
    crossoverLPF.setBypassed (! crossoverOn);

    inputPad.setAttenuation (parameters.getRawParameterValue (paramInputPad)->load() > 0.5f ? -10.0f : 0.0f);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DeluxeBassFuzzAudioProcessor();
}
