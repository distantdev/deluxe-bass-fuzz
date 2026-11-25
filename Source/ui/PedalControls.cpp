#include "PedalControls.h"
#include "BinaryData.h"
#include <cmath>

namespace PedalAssets
{
    namespace
    {
        juce::Image loadCached (const void* data, int size)
        {
            return juce::ImageCache::getFromMemory (data, size);
        }
    }

    juce::Image chassis()                { return loadCached (BinaryData::pedal_chassis_png,            BinaryData::pedal_chassis_pngSize); }
    juce::Image knobShadow()             { return loadCached (BinaryData::knob_shadow_png,              BinaryData::knob_shadow_pngSize); }
    juce::Image knobSkirtStrip()         { return loadCached (BinaryData::knob_skirt_strip_png,         BinaryData::knob_skirt_strip_pngSize); }
    juce::Image knobWallStrip()          { return loadCached (BinaryData::knob_wall_strip_png,          BinaryData::knob_wall_strip_pngSize); }
    juce::Image knobCap()                { return loadCached (BinaryData::knob_cap_png,                 BinaryData::knob_cap_pngSize); }
    juce::Image knobLighting()           { return loadCached (BinaryData::knob_lighting_png,            BinaryData::knob_lighting_pngSize); }
    juce::Image toggleUp()               { return loadCached (BinaryData::toggle_up_png,                BinaryData::toggle_up_pngSize); }
    juce::Image toggleDown()             { return loadCached (BinaryData::toggle_down_png,              BinaryData::toggle_down_pngSize); }
    juce::Image ledGreenOff()            { return loadCached (BinaryData::led_green_off_png,            BinaryData::led_green_off_pngSize); }
    juce::Image ledGreenOn()             { return loadCached (BinaryData::led_green_on_png,             BinaryData::led_green_on_pngSize); }
    juce::Image ledRedOff()              { return loadCached (BinaryData::led_red_off_png,              BinaryData::led_red_off_pngSize); }
    juce::Image ledRedOn()               { return loadCached (BinaryData::led_red_on_png,               BinaryData::led_red_on_pngSize); }
    juce::Image footswitchLeftPressed()  { return loadCached (BinaryData::footswitch_left_pressed_png,  BinaryData::footswitch_left_pressed_pngSize); }
    juce::Image footswitchRightPressed() { return loadCached (BinaryData::footswitch_right_pressed_png, BinaryData::footswitch_right_pressed_pngSize); }
}

//==============================================================================
PedalKnob::PedalKnob (double defaultValue, float parallaxXNorm, float parallaxYNorm)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      parallaxX (parallaxXNorm),
      parallaxY (parallaxYNorm)
{
    constexpr float pi = juce::MathConstants<float>::pi;
    setRotaryParameters (1.1667f * pi, 2.8333f * pi, true);
    setMouseDragSensitivity (180);
    setScrollWheelEnabled (true);
    setDoubleClickReturnValue (true, defaultValue);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool PedalKnob::hitTest (int x, int y)
{
    const float w = static_cast<float> (getWidth());
    const float h = static_cast<float> (getHeight());
    const float cx = (0.5f + parallaxX) * w;
    const float cy = (0.5f + parallaxY) * h;
    const float rMax = 0.37f * juce::jmin (w, h);
    const float dx = static_cast<float> (x) - cx;
    const float dy = static_cast<float> (y) - cy;
    return (dx * dx + dy * dy) <= (rMax * rMax);
}

void PedalKnob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    const auto shadowImg  = PedalAssets::knobShadow();
    const auto skirtStrip = PedalAssets::knobSkirtStrip();
    const auto wallStrip  = PedalAssets::knobWallStrip();
    const auto capImg     = PedalAssets::knobCap();
    const auto lightImg   = PedalAssets::knobLighting();

    const auto params = getRotaryParameters();
    const double prop = valueToProportionOfLength (getValue());
    const float angle = params.startAngleRadians
                      + static_cast<float> (prop) * (params.endAngleRadians - params.startAngleRadians);

    constexpr int numFrames = 12;
    constexpr float toothPitch = juce::MathConstants<float>::twoPi / 64.0f;
    float wrapped = std::fmod (angle, toothPitch);
    if (wrapped < 0.0f)
        wrapped += toothPitch;

    const int frameIdx = juce::jlimit (
        0, numFrames - 1,
        static_cast<int> (std::floor ((wrapped / toothPitch) * static_cast<float> (numFrames))));

    // 1. Stationary base AO + directional cast shadow centered over faceplate label
    if (shadowImg.isValid())
        g.drawImage (shadowImg, bounds);

    const float pxShiftX = parallaxX * bounds.getWidth();
    const float pxShiftY = parallaxY * bounds.getHeight();
    const float shiftLen = std::hypot (pxShiftX, pxShiftY);

    // 2. 3D tapered fluted side wall extruded smoothly from grounded base to perspective-offset cap
    if (wallStrip.isValid() && shiftLen > 0.25f)
    {
        const int frameSize = wallStrip.getWidth();
        const auto wallFrame = wallStrip.getClippedImage ({ 0, frameIdx * frameSize, frameSize, frameSize });
        const int steps = juce::jlimit (4, 20, static_cast<int> (std::ceil (shiftLen * 2.0f)));
        const float baseCx = bounds.getCentreX();
        const float baseCy = bounds.getCentreY();

        for (int i = 0; i < steps; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (steps);
            const float taper = 1.036f - 0.036f * t;
            const float sw = bounds.getWidth() * taper;
            const float sh = bounds.getHeight() * taper;
            const float cx = baseCx + t * pxShiftX;
            const float cy = baseCy + t * pxShiftY;
            const juce::Rectangle<float> sliceBounds (cx - sw * 0.5f, cy - sh * 0.5f, sw, sh);

            g.setOpacity (1.0f);
            g.drawImage (wallFrame, sliceBounds);

            const float aoDarken = 0.34f * (1.0f - t) * (1.0f - t);
            if (aoDarken > 0.01f)
            {
                g.setColour (juce::Colours::black.withAlpha (aoDarken));
                g.drawImage (wallFrame, sliceBounds, juce::RectanglePlacement::stretchToFit, true);
            }
        }
        g.setOpacity (1.0f);
    }

    const auto capBounds = bounds.translated (pxShiftX, pxShiftY);

    // 3. Stationary-lit 64-tooth PBR knurled top skirt cycled across 12 tooth-pitch frames
    if (skirtStrip.isValid())
    {
        const int frameSize = skirtStrip.getWidth();
        const auto frameImg = skirtStrip.getClippedImage ({ 0, frameIdx * frameSize, frameSize, frameSize });
        g.drawImage (frameImg, capBounds);
    }

    // 4. Continuously rotating matte ABS cap + pointed-V white indicator
    if (capImg.isValid())
    {
        const float scaleX = capBounds.getWidth()  / static_cast<float> (capImg.getWidth());
        const float scaleY = capBounds.getHeight() / static_cast<float> (capImg.getHeight());
        const float imgCx  = static_cast<float> (capImg.getWidth())  * 0.5f;
        const float imgCy  = static_cast<float> (capImg.getHeight()) * 0.5f;

        auto xform = juce::AffineTransform::rotation (angle, imgCx, imgCy)
                         .scaled (scaleX, scaleY)
                         .translated (capBounds.getX(), capBounds.getY());
        g.drawImageTransformed (capImg, xform, false);
    }

    // 5. Stationary softbox lighting gradient over rotating cap
    if (lightImg.isValid())
        g.drawImage (lightImg, capBounds);
}

//==============================================================================
PedalToggleSwitch::PedalToggleSwitch()
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PedalToggleSwitch::paintButton (juce::Graphics& g,
                                     bool shouldDrawButtonAsHighlighted,
                                     bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto img = getToggleState() ? PedalAssets::toggleUp() : PedalAssets::toggleDown();
    if (img.isValid())
        g.drawImage (img, bounds);
}

//==============================================================================
PedalFootswitch::PedalFootswitch (bool isLeftCrossoverSwitch)
    : isLeft (isLeftCrossoverSwitch)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool PedalFootswitch::hitTest (int x, int y)
{
    const float cx = static_cast<float> (getWidth()) * 0.5f;
    const float cy = static_cast<float> (getHeight()) * 0.5f;
    const float rMax = 0.42f * static_cast<float> (juce::jmin (getWidth(), getHeight()));
    const float dx = static_cast<float> (x) - cx;
    const float dy = static_cast<float> (y) - cy;
    return (dx * dx + dy * dy) <= (rMax * rMax);
}

void PedalFootswitch::paintButton (juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted);
    if (! shouldDrawButtonAsDown)
        return;

    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto pressed = isLeft ? PedalAssets::footswitchLeftPressed()
                                : PedalAssets::footswitchRightPressed();
    if (pressed.isValid())
        g.drawImage (pressed, bounds);
}

//==============================================================================
PedalLed::PedalLed (bool greenLed)
    : isGreen (greenLed)
{
    setInterceptsMouseClicks (false, false);
}

void PedalLed::setLit (bool shouldBeLit)
{
    if (lit == shouldBeLit)
        return;
    lit = shouldBeLit;
    repaint();
}

void PedalLed::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto img = isGreen
        ? (lit ? PedalAssets::ledGreenOn() : PedalAssets::ledGreenOff())
        : (lit ? PedalAssets::ledRedOn()   : PedalAssets::ledRedOff());
    if (img.isValid())
        g.drawImage (img, bounds);
}
