#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace drift
{
/** Delay ping-pong sincronizado al tempo + reverb difusa. */
class Space
{
public:
    struct Settings
    {
        float  on = 1.0f;
        double delaySeconds = 0.375;
        float  delayFb = 0.35f, delayMix = 0.2f;
        float  revSize = 0.5f, revDamp = 0.5f, revMix = 0.2f;
    };

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        size = juce::nextPowerOfTwo ((int) (sr * 3.0));
        mask = size - 1;
        dl[0].assign ((size_t) size, 0.0f);
        dl[1].assign ((size_t) size, 0.0f);
        tmpL.assign ((size_t) maxBlock, 0.0f);
        tmpR.assign ((size_t) maxBlock, 0.0f);
        reverb.setSampleRate (sr);
        dampCoeff = std::exp (-juce::MathConstants<float>::twoPi * 4500.0f / (float) sr);
        hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * 90.0f / (float) sr);
        onSmooth.reset (sr, 0.05);
        delayMixS.reset (sr, 0.05);
        revMixS.reset (sr, 0.05);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        writePos = 0;
        currentDelay = -1.0f;
        lpL = lpR = hpL = hpR = hpInL = hpInR = 0.0f;
        reverb.reset();
    }

    void process (float* L, float* R, int n, const Settings& s) noexcept
    {
        onSmooth.setTargetValue (s.on);
        delayMixS.setTargetValue (s.delayMix);
        revMixS.setTargetValue (s.revMix);

        juce::Reverb::Parameters p;
        p.roomSize = 0.3f + 0.68f * s.revSize;
        p.damping = s.revDamp;
        p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.width = 1.0f; p.freezeMode = 0.0f;
        if (std::abs (p.roomSize - lastParams.roomSize) > 1.0e-5f || std::abs (p.damping - lastParams.damping) > 1.0e-5f)
        {
            reverb.setParameters (p);
            lastParams = p;
        }

        const float target = (float) juce::jlimit (0.01, 2.5, s.delaySeconds) * (float) sampleRate;
        if (currentDelay < 0.0f) currentDelay = target;
        const float fb = juce::jlimit (0.0f, 0.85f, s.delayFb);

        for (int i = 0; i < n; ++i) { tmpL[(size_t) i] = L[i]; tmpR[(size_t) i] = R[i]; }
        reverb.processStereo (tmpL.data(), tmpR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            currentDelay += 0.0004f * (target - currentDelay); // cambio de tempo = glide, no clic
            const float dLv = readDelay (0, currentDelay);
            const float dRv = readDelay (1, currentDelay);

            // Ping-pong con realimentación oscurecida y saturada
            lpL = dampCoeff * lpL + (1.0f - dampCoeff) * dRv;
            lpR = dampCoeff * lpR + (1.0f - dampCoeff) * dLv;
            hpL = hpCoeff * (hpL + lpL - hpInL); hpInL = lpL;
            hpR = hpCoeff * (hpR + lpR - hpInR); hpInR = lpR;
            const float mono = 0.5f * (L[i] + R[i]);
            dl[0][(size_t) writePos] = mono + fb * std::tanh (hpL);
            dl[1][(size_t) writePos] = fb * std::tanh (hpR);
            writePos = (writePos + 1) & mask;

            const float onV = onSmooth.getNextValue();
            const float dm = delayMixS.getNextValue() * onV;
            const float rm = revMixS.getNextValue() * onV;
            L[i] = L[i] * (1.0f - 0.3f * rm) + dm * dLv + rm * tmpL[(size_t) i] * 0.8f;
            R[i] = R[i] * (1.0f - 0.3f * rm) + dm * dRv + rm * tmpR[(size_t) i] * 0.8f;
        }
    }

private:
    float readDelay (int ch, float d) const noexcept
    {
        const float rp = (float) writePos - d;
        const int i0 = (int) std::floor (rp);
        const float t = rp - (float) i0;
        const auto& b = dl[(size_t) ch];
        const float a = b[(size_t) (i0 & mask)], c = b[(size_t) ((i0 + 1) & mask)];
        return a + t * (c - a);
    }

    double sampleRate = 44100.0;
    std::vector<float> dl[2];
    std::vector<float> tmpL, tmpR;
    int size = 1, mask = 0, writePos = 0;
    float currentDelay = -1.0f;
    float dampCoeff = 0.5f, hpCoeff = 0.99f, lpL = 0, lpR = 0, hpL = 0, hpR = 0, hpInL = 0, hpInR = 0;
    juce::Reverb reverb;
    juce::Reverb::Parameters lastParams { -1.0f, -1.0f, 0, 0, 0, 0 };
    juce::SmoothedValue<float> onSmooth, delayMixS, revMixS;
};
} // namespace drift
