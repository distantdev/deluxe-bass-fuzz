#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <limits>

//==============================================================================
/**
    Anti-parallel diode pair clipper (series resistance R, diode pair to ground,
    optional capacitor C across the diodes).

    Nodal equation at the clipping node (V = output voltage):

        (V - Vin) / R  +  2 * Is * sinh(V / (n * Vt))  +  i_C  =  0

    With C = 0 the clipper is memoryless. With C > 0 the capacitor is
    discretised with the trapezoidal rule (companion model i_C = Geq*V - Ieq,
    Geq = 2C/T), which makes it a first-order RC + diode clipper.

    Either way each sample reduces to solving

        f(V) = G*V - J + 2*Is*sinh(V/a) = 0,   a = n*Vt

    f is odd-symmetric in (V, J), strictly increasing, and convex for V > 0.
    We solve for |J| and restore the sign, starting Newton-Raphson from a
    guaranteed upper bound of the root:

        V0 = min(|J|/G, a*asinh(|J|/(2*Is)))

    From there Newton converges monotonically from above (no overshoot), and
    the iterate is additionally clamped to the bracket [0, V0]. sinh() never
    sees an argument larger than asinh(|J|/(2*Is)), so it cannot overflow even
    for huge inputs. The solve runs in double precision to a 1e-9 V tolerance,
    capped at 50 iterations (it typically needs 3-8).

    For the memoryless case, processSampleADAA() applies first-order
    antiderivative anti-aliasing. With x = Vin and y = g(x) the implicit
    solution, x = h(y) = y + 2*Is*R*sinh(y/a), so

        G1(x) = integral g dx = y*x - y^2/2 - 2*Is*R*a*cosh(y/a)

    and y_n = (G1(x_n) - G1(x_{n-1})) / (x_n - x_{n-1}), falling back to
    g((x_n + x_{n-1})/2) when the step is tiny.
*/
class DiodeClipper
{
public:
    DiodeClipper() = default;

    // Configure diode parameters (default: silicon 1N4148-like)
    void setDiodeParams (float is = 2.52e-9f, float n = 1.75f, float vt = 0.02585f)
    {
        saturationCurrent = juce::jmax (1.0e-18, (double) is);
        idealityFactor    = juce::jmax (0.1, (double) n);
        thermalVoltage    = juce::jmax (1.0e-4, (double) vt);
    }

    // Series resistance feeding the diode pair
    void setSeriesResistance (float r)
    {
        seriesR = juce::jmax (1.0e-3, (double) r);
    }

    // Optional capacitance across the diodes (0 = memoryless clipper)
    void setCapacitance (float c, double sampleRate)
    {
        capacitance = juce::jmax (0.0, (double) c);
        geq = (capacitance > 0.0 && sampleRate > 0.0) ? 2.0 * capacitance * sampleRate : 0.0;
        reset();
    }

    // Process a single sample: returns the clipping-node voltage
    float processSample (float input)
    {
        const double vin = sanitise (input);
        const double G = 1.0 / seriesR + geq;
        const double ieq = geq * vPrev + iCPrev;          // trapezoidal companion source
        const double J = vin / seriesR + ieq;

        const double v = solve (G, J);

        if (geq > 0.0)
        {
            iCPrev = geq * v - ieq;                        // capacitor current this sample
            vPrev = v;
        }

        return (float) v;
    }

    // Process a block of samples
    void process (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

    // First-order antiderivative anti-aliased version (memoryless case only;
    // with a capacitor set it falls back to processSample).
    float processSampleADAA (float input)
    {
        if (geq > 0.0)
            return processSample (input);

        const double x = sanitise (input);
        const double G = 1.0 / seriesR;
        const double y = solve (G, x * G);
        const double g1 = antiderivative (x, y);

        double out;
        const double dx = x - xPrev;

        if (std::abs (dx) > 1.0e-5)
            out = (g1 - g1Prev) / dx;
        else
            out = solve (G, 0.5 * (x + xPrev) * G);

        xPrev = x;
        g1Prev = g1;
        return (float) out;
    }

    void processADAA (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSampleADAA (samples[i]);
    }

    void reset()
    {
        vPrev = 0.0;
        iCPrev = 0.0;
        xPrev = 0.0;
        g1Prev = antiderivative (0.0, 0.0);
    }

    // Iteration count of the most recent solve (for tests/diagnostics)
    int getLastIterationCount() const noexcept { return lastIterations; }

private:
    static double sanitise (float x) noexcept
    {
        // Guard against NaN/Inf reaching the solver
        if (! std::isfinite (x))
            return 0.0;
        return (double) x;
    }

    double antiderivative (double x, double y) const noexcept
    {
        const double a = idealityFactor * thermalVoltage;
        return y * x - 0.5 * y * y - 2.0 * saturationCurrent * seriesR * a * std::cosh (y / a);
    }

    // Solve G*V - J + 2*Is*sinh(V/a) = 0 for V
    double solve (double G, double J) noexcept
    {
        lastIterations = 0;

        const double sign = J < 0.0 ? -1.0 : 1.0;
        const double Jabs = std::abs (J);

        if (Jabs == 0.0)
            return 0.0;

        const double a = idealityFactor * thermalVoltage;
        const double twoIs = 2.0 * saturationCurrent;

        // Upper bound of the root (see class comment)
        const double hi = juce::jmin (Jabs / G, a * std::asinh (Jabs / twoIs));
        double v = hi;

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            lastIterations = iter + 1;

            const double s = std::sinh (v / a);
            const double c = std::cosh (v / a);
            const double f  = G * v - Jabs + twoIs * s;
            const double df = G + twoIs * c / a;

            double next = v - f / df;
            next = juce::jlimit (0.0, hi, next);           // stay inside the bracket

            const double step = std::abs (next - v);
            v = next;

            if (step < tolerance)
                break;
        }

        return sign * v;
    }

    static constexpr int maxIterations = 50;
    static constexpr double tolerance = 1.0e-9;

    // Diode parameters (1N4148 silicon defaults)
    double saturationCurrent = 2.52e-9;  // Is
    double idealityFactor = 1.75;        // n
    double thermalVoltage = 0.02585;     // Vt at 25 degC
    double seriesR = 390.0;              // Series resistance

    // Optional capacitor (trapezoidal companion model)
    double capacitance = 0.0;
    double geq = 0.0;
    double vPrev = 0.0;
    double iCPrev = 0.0;

    // ADAA state
    double xPrev = 0.0;
    double g1Prev = -2.0 * 2.52e-9 * 390.0 * 1.75 * 0.02585;

    int lastIterations = 0;
};
