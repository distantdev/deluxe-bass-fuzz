#pragma once

#include <JuceHeader.h>

namespace PedalAssets
{
    juce::Image chassis();
    juce::Image knobShadow();
    juce::Image knobSkirtStrip();
    juce::Image knobWallStrip();
    juce::Image knobCap();
    juce::Image knobLighting();
    juce::Image toggleUp();
    juce::Image toggleDown();
    juce::Image ledGreenOff();
    juce::Image ledGreenOn();
    juce::Image ledRedOff();
    juce::Image ledRedOn();
    juce::Image footswitchLeftPressed();
    juce::Image footswitchRightPressed();
}

//==============================================================================
class PedalKnob final : public juce::Slider
{
public:
    explicit PedalKnob (double defaultValue, float parallaxX = 0.0f, float parallaxY = 0.0f);

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics& g) override;

private:
    float parallaxX = 0.0f;
    float parallaxY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalKnob)
};

//==============================================================================
class PedalToggleSwitch final : public juce::ToggleButton
{
public:
    PedalToggleSwitch();

    void paintButton (juce::Graphics& g,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalToggleSwitch)
};

//==============================================================================
class PedalFootswitch final : public juce::ToggleButton
{
public:
    explicit PedalFootswitch (bool isLeftCrossoverSwitch);

    bool hitTest (int x, int y) override;
    void paintButton (juce::Graphics& g,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    bool isLeft = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalFootswitch)
};

//==============================================================================
class PedalLed final : public juce::Component
{
public:
    explicit PedalLed (bool greenLed);

    void setLit (bool shouldBeLit);
    bool isLit() const noexcept { return lit; }

    void paint (juce::Graphics& g) override;

private:
    bool isGreen = false;
    bool lit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalLed)
};
