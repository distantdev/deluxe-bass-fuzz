/*
    DeluxeBassFuzzRender - renders the SPICE comparison signals through the DSP blocks.

    Reads tests/ReferenceData/cases.json (shared with tools/run_spice.py) and writes
    tests/ReferenceData/dsp/:
      ir_<name>.csv    impulse response (tiny amplitude, small-signal) for AC cases
      tran_<name>.csv  time_s, vin, vout for transient sine cases

    All blocks run at cases.json "sampleRate" (192 kHz), i.e. the same rate the
    plugin runs them at inside its 4x oversampler, without the oversampling filters.

    Usage: DeluxeBassFuzzRender [--root <project dir>]
*/
#include <JuceHeader.h>
#include "dsp/BmpInputStage.h"
#include "dsp/BmpClippingStage.h"
#include "dsp/BmpToneStack.h"
#include "dsp/BmpOutputStage.h"

#ifndef BMD_DIAG_EXTRA
 #define BMD_DIAG_EXTRA
#endif

namespace
{
    struct Chain
    {
        juce::String block;
        double fs = 192000.0;
        float sustain = 0.5f, tone = 0.5f;

        // Same structure as DeluxeBassFuzzAudioProcessor: Q1+sustain -> Q2 -> Q3 (loaded by
        // the tone stack + Q4 input) -> tone stack + Q4 -> volume load
        BmpInputStage input;
        BmpClippingStage clip1 { BmpClippingStage::Load::NextClippingStage };
        BmpClippingStage clip2 { BmpClippingStage::Load::ToneStackAndOutputStage };
        BmpToneStack toneStack;        // unloaded stack, for the "tone" block only
        BmpOutputStage output;

        void prepare()
        {
            // "output" = Q4 alone; "toneout" and "core" include the (loaded) tone stack
            output.setIncludeToneStack (block != "output");
            input.setSustain (sustain);
            clip2.setTone (tone);
            output.setTone (tone);
            toneStack.setTone (tone);

            input.prepare (fs);
            clip1.prepare (fs);
            clip2.prepare (fs);
            toneStack.prepare (fs);
            output.prepare (fs);
        }

        float process (float x)
        {
            if (block == "tone")   return toneStack.processSample (x);
            if (block == "input")  return input.processSample (x);
            if (block == "clip")   return clip1.processSample (x);
            if (block == "output") return output.processSample (x);
            if (block == "toneout") return output.processSample (x);

            // core
            x = input.processSample (x);
            x = clip1.processSample (x);
            x = clip2.processSample (x);
            return output.processSample (x);
        }
    };

    juce::String fmt (double v) { return juce::String (v, 9); }

    bool writeLines (const juce::File& f, const juce::StringArray& lines)
    {
        f.deleteFile();
        return f.replaceWithText (lines.joinIntoString ("\n") + "\n");
    }

    juce::String ampTag (double a)
    {
        // match Python's "%g"
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%g", a);
        return buf;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedNoDenormals noDenormals;

    juce::File root = juce::File::getCurrentWorkingDirectory();
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--root")
            root = juce::File (juce::String (argv[i + 1]));

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--diag")
        {
            // Quiescent points and real-time cost of the core at 192 kHz
            Chain ch;
            ch.block = "core";
            ch.fs = 192000.0;
            ch.prepare();
            std::printf ("Vc(Q2 clip) = %.4f V\n", ch.clip1.getQuiescentCollectorVoltage());
            std::printf ("Vc(Q3 clip) = %.4f V\n", ch.clip2.getQuiescentCollectorVoltage());
            BMD_DIAG_EXTRA
            const int n = 192000 * 2;
            const auto t0 = juce::Time::getHighResolutionTicks();
            double acc = 0.0, it1 = 0.0, it2 = 0.0; int mx = 0;
            for (int k = 0; k < n; ++k)
            {
                acc += ch.process ((float) (0.5 * std::sin (2.0 * 3.14159265358979 * 110.0 * k / 192000.0)));
                it1 += ch.clip1.getLastIterationCount(); it2 += ch.clip2.getLastIterationCount();
                mx = std::max (mx, ch.clip1.getLastIterationCount());
            }
            const double sec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            std::printf ("core: %.1f ns/sample at 192 kHz = %.1f %% of one core in real time (checksum %g)\n",
                         1e9 * sec / n, 100.0 * sec / 2.0, acc);
            std::printf ("mean Newton iterations: clip1 %.2f clip2 %.2f (max %d)\n", it1 / n, it2 / n, mx);
            {
                // per-stage cost on a pre-rendered signal (stage input recorded from the chain)
                Chain c2; c2.block = "core"; c2.fs = 192000.0; c2.prepare();
                std::vector<float> xin (n), x1 (n), x2 (n), x3 (n);
                for (int k = 0; k < n; ++k) xin[k] = (float) (0.5 * std::sin (2.0 * 3.14159265358979 * 110.0 * k / 192000.0));
                auto timeIt = [&] (const char* name, auto&& fn)
                {
                    const auto s0 = juce::Time::getHighResolutionTicks();
                    fn();
                    const double s = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - s0);
                    std::printf ("  %-8s %.1f ns/sample\n", name, 1e9 * s / n);
                };
                timeIt ("input", [&] { for (int k = 0; k < n; ++k) x1[k] = c2.input.processSample (xin[k]); });
                timeIt ("clip1", [&] { for (int k = 0; k < n; ++k) x2[k] = c2.clip1.processSample (x1[k]); });
                timeIt ("clip2", [&] { for (int k = 0; k < n; ++k) x3[k] = c2.clip2.processSample (x2[k]); });
                timeIt ("output", [&] { for (int k = 0; k < n; ++k) x1[k] = c2.output.processSample (x3[k]); });
                float pk = 0; for (int k = 0; k < n; ++k) pk = std::max (pk, std::abs (x2[k]));
                std::printf ("  clip1 peak out %.3f V\n", pk);
            }
            return 0;
        }

    const auto casesFile = root.getChildFile ("tests/ReferenceData/cases.json");
    const auto cfg = juce::JSON::parse (casesFile);
    if (! cfg.isObject())
    {
        std::fprintf (stderr, "Could not parse %s\n", casesFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto outDir = root.getChildFile ("tests/ReferenceData/dsp");
    outDir.createDirectory();

    const double fs = (double) cfg["sampleRate"];
    const double tranSeconds = (double) cfg["tranSeconds"];
    const float toneDefault = (float) (double) cfg["toneDefault"];

    int written = 0;

    // ---- AC cases: small-signal impulse responses --------------------------------
    if (auto* ac = cfg["ac"].getArray())
    {
        for (auto& c : *ac)
        {
            Chain chain;
            chain.block = c["block"].toString();
            chain.fs = fs;
            chain.sustain = c.hasProperty ("sustain") ? (float) (double) c["sustain"] : 0.5f;
            chain.tone = c.hasProperty ("tone") ? (float) (double) c["tone"] : toneDefault;
            chain.prepare();

            // Let any start-up state settle, then a tiny impulse keeps every stage linear
            for (int i = 0; i < 4096; ++i)
                chain.process (0.0f);

            const double amp = 1.0e-6;
            const int N = 1 << 17;
            juce::StringArray lines;
            lines.add ("n,h");
            for (int i = 0; i < N; ++i)
            {
                const float y = chain.process (i == 0 ? (float) amp : 0.0f);
                lines.add (juce::String (i) + "," + fmt (y / amp));
            }

            writeLines (outDir.getChildFile ("ir_" + c["name"].toString() + ".csv"), lines);
            ++written;
        }
    }

    // ---- Transient sine cases ---------------------------------------------------
    if (auto* tran = cfg["tran"].getArray())
    {
        for (auto& g : *tran)
        {
            const auto block = g["block"].toString();
            juce::Array<juce::var> sustains;
            if (auto* s = g["sustains"].getArray()) sustains = *s; else sustains.add (juce::var());

            for (auto& s : sustains)
                for (auto& f : *g["freqs"].getArray())
                    for (auto& a : *g["amps"].getArray())
                    {
                        Chain chain;
                        chain.block = block;
                        chain.fs = fs;
                        chain.tone = toneDefault;
                                    juce::String tag = block;
                        if (! s.isVoid())
                        {
                            chain.sustain = (float) (double) s;
                            char sb[32]; std::snprintf (sb, sizeof (sb), "_s%.2f", (double) s); tag << sb;
                        }
                        chain.prepare();

                        const double freq = (double) f, amp = (double) a;
                        const juce::String name = tag + "_f" + juce::String ((int) freq) + "_a" + ampTag (amp);

                        const int N = (int) std::lround (tranSeconds * fs) + 1;
                        juce::StringArray lines;
                        lines.add ("time_s,vin,vout");
                        for (int i = 0; i < N; ++i)
                        {
                            const double t = i / fs;
                            const float x = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * t));
                            const float y = chain.process (x);
                            lines.add (fmt (t) + "," + fmt (x) + "," + fmt (y));
                        }

                        writeLines (outDir.getChildFile ("tran_" + name + ".csv"), lines);
                        ++written;
                    }
        }
    }

    std::printf ("DeluxeBassFuzzRender: wrote %d files to %s\n", written, outDir.getFullPathName().toRawUTF8());
    return 0;
}
