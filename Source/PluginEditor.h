#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/PedalControls.h"

//==============================================================================
/**
    Photorealistic skeuomorphic editor matching the Deluxe Bass Fuzz.
*/
class DeluxeBassFuzzAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DeluxeBassFuzzAudioProcessorEditor (DeluxeBassFuzzAudioProcessor&);
    ~DeluxeBassFuzzAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    DeluxeBassFuzzAudioProcessor& processorRef;

    // Knobs (top row left-to-right: VOLUME, BLEND, TONE, SUSTAIN, GATE, HPF; second row: LPF)
    PedalKnob volumeSlider;
    PedalKnob blendSlider;
    PedalKnob toneSlider;
    PedalKnob sustainSlider;
    PedalKnob gateSlider;
    PedalKnob hpfSlider;
    PedalKnob lpfSlider;

    // Hardware switches & status LEDs
    PedalToggleSwitch inputPadButton;
    PedalFootswitch crossoverButton;
    PedalFootswitch bypassButton;
    PedalLed crossoverLed { true };
    PedalLed bypassLed    { false };

    // APVTS attachments (declared after components so they are destroyed first)
    std::unique_ptr<SliderAttachment> volumeAttachment;
    std::unique_ptr<SliderAttachment> blendAttachment;
    std::unique_ptr<SliderAttachment> toneAttachment;
    std::unique_ptr<SliderAttachment> sustainAttachment;
    std::unique_ptr<SliderAttachment> gateAttachment;
    std::unique_ptr<SliderAttachment> hpfAttachment;
    std::unique_ptr<SliderAttachment> lpfAttachment;
    std::unique_ptr<ButtonAttachment> inputPadAttachment;
    std::unique_ptr<ButtonAttachment> crossoverAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    void syncLedStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeluxeBassFuzzAudioProcessorEditor)
};
