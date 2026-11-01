#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Divisions.h"

namespace drift
{
/** Modulación rítmica estilo Shaperbox: volumen + filtro + paneo,
    todo derivado de la posición PPQ del host (sin deriva). */
class Shaper
{
public:
    struct Settings
    {
        float  on = 1.0f;
        double divBeats = 0.25;
        int    steps = 16, hits = 11, rotate = 0;
        float  depth = 0.5f, curve = 0.3f, smoothMs = 3.0f;
        float  filtDepth = 0.0f, filtFreq = 20000.0f, filtRes = 0.1f;
        float  panDepth = 0.0f;
        double panDivBeats = 1.0;
    };

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        onSmooth.reset (sr, 0.05);
        reset();
    }

    void reset() noexcept
    {
        filter.reset();
        volEnv = 1.0f; filtEnv = 1.0f;
        currentStep.store (-1, std::memory_order_relaxed);
    }

    void process (float* L, float* R, int n, const Settings& s, double ppqStart, double ppqPerSample) noexcept
    {
        onSmooth.setTargetValue (s.on);
        const int steps = juce::jlimit (1, 32, s.steps);
        const uint32_t pattern = euclidMask (steps, juce::jmin (s.hits, steps), s.rotate);
        const float smoothCoeff = std::exp (-1.0f / (juce::jmax (0.5f, s.smoothMs) * 0.001f * (float) sampleRate));
        const float decayK = s.curve * 6.0f;
        filter.setResonance (0.707f + s.filtRes * 6.0f);

        const bool filterUsed = s.filtDepth > 0.001f || s.filtFreq < 19000.0f;

        for (int i = 0; i < n; ++i)
        {
            const double ppq = ppqStart + i * ppqPerSample;
            const float onV = onSmooth.getNextValue();

            const int64_t cyc = cycleIndex (ppq, s.divBeats);
            int step = (int) (cyc % steps);
            if (step < 0) step += steps;
            const float stepPhase = (float) phaseFor (ppq, s.divBeats);
            const bool hit = (pattern >> step) & 1u;
            const float g = hit ? std::exp (-stepPhase * decayK) : 0.0f;

            // Suavizado anti-clic (sube y baja con la misma constante)
            const float volTarget = 1.0f - s.depth * onV * (1.0f - g);
            volEnv = smoothCoeff * volEnv + (1.0f - smoothCoeff) * volTarget;
            filtEnv = smoothCoeff * filtEnv + (1.0f - smoothCoeff) * g;

            float l = L[i] * volEnv, r = R[i] * volEnv;

            if (filterUsed && onV > 0.0005f)
            {
                if ((i & 15) == 0)
                {
                    const float base = 20000.0f + (s.filtFreq - 20000.0f) * onV;
                    const float fc = base * std::exp2 (-s.filtDepth * onV * 5.0f * (1.0f - filtEnv));
                    filter.setCutoffFrequency (juce::jlimit (30.0f, (float) sampleRate * 0.45f, fc));
                }
                l = filter.processSample (0, l);
                r = filter.processSample (1, r);
            }

            if (s.panDepth > 0.001f)
            {
                const float ph = (float) phaseFor (ppq, s.panDivBeats);
                const float tri = 1.0f - 4.0f * std::abs (ph - 0.5f); // -1..1
                const float pan = s.panDepth * onV * tri;
                const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                l *= std::cos (ang) * juce::MathConstants<float>::sqrt2;
                r *= std::sin (ang) * juce::MathConstants<float>::sqrt2;
            }

            L[i] = l; R[i] = r;
            if (i == n - 1) currentStep.store (step, std::memory_order_relaxed);
        }
    }

    std::atomic<int> currentStep { -1 };

private:
    double sampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> filter;
    float volEnv = 1.0f, filtEnv = 1.0f;
    juce::SmoothedValue<float> onSmooth;
};
} // namespace drift
