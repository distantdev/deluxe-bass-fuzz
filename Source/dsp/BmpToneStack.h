#pragma once

#include <JuceHeader.h>
#include <complex>

//==============================================================================
/**
    Classic Bass Fuzz passive tone stack - white-box nodal model.

    Circuit (component values from docs/circuit-notes.md):

        Vin --R1 11k5-+-- A          Vin --C2 3n9--+-- B
                      |                            |
                    C1 10n                       R2 22k
                      |                            |
                     GND                          GND

        TONE pot (100k linear) between A and B, wiper = output.

    A is the low-pass (bass) node: 1/(2*pi*11.5k*10n) = 1384 Hz.
    B is the high-pass (treble) node: 1/(2*pi*22k*3n9) = 1855 Hz
    (1808 Hz with the classic 4n cap). Because the LP corner sits below the
    HP corner, blending the two leaves a gap -> the Classic Bass Fuzz mid-scoop. With
    R1 = 11.5k (circuit reference: "Deluxe Bass Fuzz - the low pass resistor value was
    11.5k"; the classic bass fuzz used 20k, earlier project value 39k) the gap is
    small: the noon scoop is ~2.5 dB deep and centred near 2 kHz.

    This is the UNLOADED stack (wiper into an open circuit), used for the
    tone-stack validation and unit tests. In the plugin core the stack is
    simulated inside BmpOutputStage together with Q4's input impedance.

    Nodal analysis (G1 = 1/R1, G2 = 1/R2, Gp = 1/Rpot, wiper unloaded):

        | G1 + Gp + sC1     -Gp          | |Va|   | G1  |
        | -Gp               G2 + Gp + sC2| |Vb| = | sC2 | * Vin

        D(s)  = C1C2 s^2 + (C1(G2+Gp) + C2(G1+Gp)) s + (G1+Gp)(G2+Gp) - Gp^2
        Na(s) = (G1+Gp) C2 s + G1 (G2+Gp)
        Nb(s) = C1C2 s^2 + (G1+Gp) C2 s + Gp G1

    With an unloaded wiper the pot always carries the same A-B current, so
    the wiper voltage is the linear interpolation of the node voltages:

        Vout = (1 - tone) * Va + tone * Vb     (tone = 0 bass, 1 treble)

    Both node transfer functions share D(s), so the stack is one 2nd-order
    system whose numerator is blended by the pot. Coefficients for Na, Nb and
    D are discretised once in prepare() with a bilinear transform
    (pre-warped at 1 kHz, the scoop centre). The filter runs in Direct Form II
    with the state on the shared denominator, so moving the tone knob only
    changes the output tap weights: no coefficient recomputation, allocation,
    or state discontinuity on the audio thread.
*/
class BmpToneStack
{
public:
    // Component values
    static constexpr double R1   = 11500.0;   // LP series resistor (circuit reference: Deluxe Bass Fuzz 11.5k)
    static constexpr double C1   = 10.0e-9;   // LP shunt capacitor
    static constexpr double R2   = 22000.0;   // HP shunt resistor
    static constexpr double C2   = 3.9e-9;    // HP series capacitor
    static constexpr double Rpot = 100000.0;  // TONE pot

    BmpToneStack() { prepare (44100.0); }

    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        computeCoefficients();
        reset();
    }

    void setTone (float toneValue)
    {
        tone = juce::jlimit (0.0f, 1.0f, toneValue);
    }

    float getTone() const noexcept { return tone; }

    float processSample (float x)
    {
        const double t = (double) tone;

        // Direct Form II: state lives on the shared denominator
        const double w = (double) x - a1 * w1 - a2 * w2;

        const double yA = bA[0] * w + bA[1] * w1 + bA[2] * w2;   // bass node A
        const double yB = bB[0] * w + bB[1] * w1 + bB[2] * w2;   // treble node B

        w2 = w1;
        w1 = w;

        return (float) ((1.0 - t) * yA + t * yB);
    }

    void process (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        // Mono model: channels are processed in sequence through the same state
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            process (buffer.getWritePointer (ch), buffer.getNumSamples());
    }

    void reset()
    {
        w1 = w2 = 0.0;
    }

    // Digital frequency response at the current tone setting (for validation)
    std::complex<float> getFrequencyResponse (float freqHz) const
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * (double) freqHz / fs;
        const std::complex<double> z1 = std::polar (1.0, -w);   // z^-1
        const std::complex<double> z2 = z1 * z1;
        const double t = (double) tone;

        const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
        const std::complex<double> numA = bA[0] + bA[1] * z1 + bA[2] * z2;
        const std::complex<double> numB = bB[0] + bB[1] * z1 + bB[2] * z2;

        const auto h = ((1.0 - t) * numA + t * numB) / den;
        return { (float) h.real(), (float) h.imag() };
    }

    // Analog (continuous-time) response of the circuit, for test comparison
    static std::complex<double> getAnalogResponse (double freqHz, double toneValue)
    {
        const auto c = analogCoefficients();
        const std::complex<double> s (0.0, 2.0 * juce::MathConstants<double>::pi * freqHz);
        const auto D  = c.d2 * s * s + c.d1 * s + c.d0;
        const auto Na = c.na1 * s + c.na0;
        const auto Nb = c.nb2 * s * s + c.nb1 * s + c.nb0;
        return ((1.0 - toneValue) * Na + toneValue * Nb) / D;
    }

private:
    struct Analog
    {
        double d2, d1, d0;
        double na1, na0;
        double nb2, nb1, nb0;
    };

    static Analog analogCoefficients()
    {
        const double G1 = 1.0 / R1, G2 = 1.0 / R2, Gp = 1.0 / Rpot;
        Analog c {};
        c.d2  = C1 * C2;
        c.d1  = C1 * (G2 + Gp) + C2 * (G1 + Gp);
        c.d0  = (G1 + Gp) * (G2 + Gp) - Gp * Gp;
        c.na1 = (G1 + Gp) * C2;
        c.na0 = G1 * (G2 + Gp);
        c.nb2 = C1 * C2;
        c.nb1 = (G1 + Gp) * C2;
        c.nb0 = Gp * G1;
        return c;
    }

    void computeCoefficients()
    {
        const auto c = analogCoefficients();

        // Bilinear transform s = K (1 - z^-1) / (1 + z^-1), pre-warped at f0
        const double f0 = juce::jmin (1000.0, 0.25 * fs);
        const double K = 2.0 * juce::MathConstants<double>::pi * f0
                         / std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double K2 = K * K;

        auto bilinear = [K, K2] (double n2, double n1, double n0, double* out)
        {
            out[0] = n2 * K2 + n1 * K + n0;
            out[1] = 2.0 * (n0 - n2 * K2);
            out[2] = n2 * K2 - n1 * K + n0;
        };

        double a[3], na[3], nb[3];
        bilinear (c.d2, c.d1, c.d0, a);
        bilinear (0.0, c.na1, c.na0, na);
        bilinear (c.nb2, c.nb1, c.nb0, nb);

        const double norm = 1.0 / a[0];
        a1 = a[1] * norm;
        a2 = a[2] * norm;
        for (int i = 0; i < 3; ++i)
        {
            bA[i] = na[i] * norm;
            bB[i] = nb[i] * norm;
        }
    }

    double fs = 44100.0;
    float tone = 0.5f;

    // Shared denominator and per-node numerators (normalised, a0 = 1)
    double a1 = 0.0, a2 = 0.0;
    double bA[3] { 1.0, 0.0, 0.0 };
    double bB[3] { 1.0, 0.0, 0.0 };

    // Direct Form II state
    double w1 = 0.0, w2 = 0.0;
};
