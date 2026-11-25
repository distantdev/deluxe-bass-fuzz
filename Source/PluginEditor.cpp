#include "PluginEditor.h"
#include "ui/PedalLayout.h"

//==============================================================================
DeluxeBassFuzzAudioProcessorEditor::DeluxeBassFuzzAudioProcessorEditor (DeluxeBassFuzzAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      volumeSlider  (0.7,   PedalLayout::volumeParallax.dx,  PedalLayout::volumeParallax.dy),
      blendSlider   (0.5,   PedalLayout::blendParallax.dx,   PedalLayout::blendParallax.dy),
      toneSlider    (0.5,   PedalLayout::toneParallax.dx,    PedalLayout::toneParallax.dy),
      sustainSlider (0.5,   PedalLayout::sustainParallax.dx, PedalLayout::sustainParallax.dy),
      gateSlider    (0.0,   PedalLayout::gateParallax.dx,    PedalLayout::gateParallax.dy),
      hpfSlider     (500.0, PedalLayout::hpfParallax.dx,     PedalLayout::hpfParallax.dy),
      lpfSlider     (200.0, PedalLayout::lpfParallax.dx,     PedalLayout::lpfParallax.dy),
      crossoverButton (true),
      bypassButton    (false)
{
    auto& apvts = processorRef.getParameters();

    volumeAttachment    = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramVolume,    volumeSlider);
    blendAttachment     = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramBlend,     blendSlider);
    toneAttachment      = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramTone,      toneSlider);
    sustainAttachment   = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramSustain,   sustainSlider);
    gateAttachment      = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramGate,      gateSlider);
    hpfAttachment       = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramHPF,       hpfSlider);
    lpfAttachment       = std::make_unique<SliderAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramLPF,       lpfSlider);
    inputPadAttachment  = std::make_unique<ButtonAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramInputPad,  inputPadButton);
    crossoverAttachment = std::make_unique<ButtonAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramCrossover, crossoverButton);
    bypassAttachment    = std::make_unique<ButtonAttachment> (apvts, DeluxeBassFuzzAudioProcessor::paramBypass,    bypassButton);

    // Preserve logarithmic midpoint feel on HPF (500 Hz at 12:00) and LPF (200 Hz at 12:00)
    const double currentHpf = hpfSlider.getValue();
    hpfSlider.setRange (125.0, 1750.0, 1.0);
    hpfSlider.setSkewFactorFromMidPoint (500.0);
    hpfSlider.setValue (currentHpf, juce::dontSendNotification);

    const double currentLpf = lpfSlider.getValue();
    lpfSlider.setRange (60.0, 10000.0, 1.0);
    lpfSlider.setSkewFactorFromMidPoint (200.0);
    lpfSlider.setValue (currentLpf, juce::dontSendNotification);

    // Sync LEDs on both user clicks and host automation
    bypassButton.onStateChange    = [this] { syncLedStates(); };
    crossoverButton.onStateChange = [this] { syncLedStates(); };

    addAndMakeVisible (volumeSlider);
    addAndMakeVisible (blendSlider);
    addAndMakeVisible (toneSlider);
    addAndMakeVisible (sustainSlider);
    addAndMakeVisible (gateSlider);
    addAndMakeVisible (hpfSlider);
    addAndMakeVisible (lpfSlider);
    addAndMakeVisible (inputPadButton);
    addAndMakeVisible (crossoverLed);
    addAndMakeVisible (bypassLed);
    addAndMakeVisible (crossoverButton);
    addAndMakeVisible (bypassButton);

    syncLedStates();

    setResizable (true, true);
    if (auto* boundsConstrainer = getConstrainer())
    {
        boundsConstrainer->setFixedAspectRatio (static_cast<double> (PedalLayout::canvasWidth)
                                                / static_cast<double> (PedalLayout::canvasHeight));
    }
    setResizeLimits (574, 479, 1440, 1202);
    setSize (PedalLayout::defaultWidth, PedalLayout::defaultHeight);
}

DeluxeBassFuzzAudioProcessorEditor::~DeluxeBassFuzzAudioProcessorEditor()
{
    bypassButton.onStateChange = nullptr;
    crossoverButton.onStateChange = nullptr;
}

void DeluxeBassFuzzAudioProcessorEditor::syncLedStates()
{
    // Hardware convention: paramBypass == false means the effect is ACTIVE (Red LED ON).
    const bool effectActive = ! bypassButton.getToggleState();
    bypassLed.setLit (effectActive);
    crossoverLed.setLit (effectActive && crossoverButton.getToggleState());
}

//==============================================================================
void DeluxeBassFuzzAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0D0F11));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    const auto chassisImg = PedalAssets::chassis();
    if (chassisImg.isValid())
        g.drawImage (chassisImg, getLocalBounds().toFloat());
}

void DeluxeBassFuzzAudioProcessorEditor::resized()
{
    const auto area = getLocalBounds();

    volumeSlider.setBounds    (PedalLayout::volume.toBounds (area));
    blendSlider.setBounds     (PedalLayout::blend.toBounds (area));
    toneSlider.setBounds      (PedalLayout::tone.toBounds (area));
    sustainSlider.setBounds   (PedalLayout::sustain.toBounds (area));
    gateSlider.setBounds      (PedalLayout::gate.toBounds (area));
    hpfSlider.setBounds       (PedalLayout::hpf.toBounds (area));
    lpfSlider.setBounds       (PedalLayout::lpf.toBounds (area));
    inputPadButton.setBounds  (PedalLayout::inputPadToggle.toBounds (area));
    crossoverLed.setBounds    (PedalLayout::crossoverLed.toBounds (area));
    bypassLed.setBounds       (PedalLayout::bypassLed.toBounds (area));
    crossoverButton.setBounds (PedalLayout::crossoverFootswitch.toBounds (area));
    bypassButton.setBounds    (PedalLayout::bypassFootswitch.toBounds (area));
}
