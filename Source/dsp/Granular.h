#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

namespace drift
{
/** Motor granular estilo "Portal": captura continua + hasta 64 granos simultáneos.
    Toda la memoria se reserva en prepare(); process() es RT-safe. */
class Granular
{
public:
    struct Settings
    {
        float  on = 1.0f;          // 0..1 (se suaviza internamente)
        float  mix = 0.5f;
        bool   sync = true;
        double divBeats = 0.25;
        float  densityHz = 12.0f;
        float  sizeMs = 120.0f;
        float  pitchSemis = 0.0f;
        float  detuneCents = 0.0f;
        float  position = 0.2f;
        float  spray = 0.3f;
        float  reverse = 0.2f;
        float  spread = 0.6f;
        float  feedback = 0.2f;
        int    window = 0;
        bool   freeze = false;
    };

    static constexpr int kMaxGrains = 64;
    static constexpr int kWindowSize = 1024;

    void prepare (double sr)
    {
        sampleRate = sr;
        size = juce::nextPowerOfTwo ((int) (sr * 6.0));
        mask = size - 1;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);

        for (int w = 0; w < 3; ++w)
            for (int i = 0; i <= kWindowSize; ++i)
            {
                const float x = (float) i / (float) kWindowSize;
                float v = 0.0f;
                if (w == 0)       v = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * x);
                else if (w == 1)  { const float a = 0.25f;
                                    v = x < a ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x / a)
                                      : x > 1.0f - a ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - x) / a)
                                      : 1.0f; }
                else              v = x < 0.03f ? x / 0.03f : std::exp (-5.0f * (x - 0.03f) / 0.97f) * (1.0f - std::pow (x, 8.0f));
                windows[(size_t) w][(size_t) i] = v;
            }

        hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * 120.0f / (float) sr);
        mixSmooth.reset (sr, 0.05);
        onSmooth.reset (sr, 0.05);
        reset();
    }

    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        for (auto& g : grains) g.active = false;
        writePos = 0;
        lastCycle = INT64_MIN;
        samplesToNext = 0.0;
        hpL = hpR = hpInL = hpInR = 0.0f;
        fbL = fbR = 0.0f;
        norm = 1.0f;
    }

    void onTransportJump() noexcept { lastCycle = INT64_MIN; }

    void process (float* L, float* R, int n, const Settings& s, double ppqStart, double ppqPerSample, double bpm) noexcept
    {
        onSmooth.setTargetValue (s.on);
        mixSmooth.setTargetValue (s.mix);

        // Normalización por solapamiento esperado (evita que la densidad dispare el nivel)
        const double grainsPerSec = s.sync ? 1.0 / (s.divBeats * 60.0 / bpm) : (double) s.densityHz;
        const float overlap = (float) (grainsPerSec * s.sizeMs * 0.001);
        const float targetNorm = 1.0f / std::sqrt (juce::jmax (1.0f, overlap));

        const float fb = juce::jlimit (0.0f, 0.85f, s.feedback);
        const auto& win = windows[(size_t) juce::jlimit (0, 2, s.window)];

        for (int i = 0; i < n; ++i)
        {
            norm += 0.001f * (targetNorm - norm);
            const float onV = onSmooth.getNextValue();
            const float mixV = mixSmooth.getNextValue() * onV;

            // ---- Programador de granos
            bool trigger = false;
            if (s.sync)
            {
                const auto c = cycleIndex (ppqStart + i * ppqPerSample, s.divBeats);
                trigger = c != lastCycle;
                lastCycle = c;
            }
            else
            {
                samplesToNext -= 1.0;
                if (samplesToNext <= 0.0)
                {
                    trigger = true;
                    const double interval = sampleRate / juce::jmax (0.5, (double) s.densityHz);
                    samplesToNext += interval * (0.7 + 0.6 * rand01());
                    if (samplesToNext < 1.0) samplesToNext = interval;
                }
            }

            if (trigger && onV > 0.001f)
                spawn (s);

            // ---- Suma de granos
            float outL = 0.0f, outR = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float ph = (float) g.age / (float) g.length;
                const float wpos = ph * (float) kWindowSize;
                const int wi = juce::jmin ((int) wpos, kWindowSize - 1);
                const float wf = wpos - (float) wi;
                const float w = (win[(size_t) wi] + wf * (win[(size_t) wi + 1] - win[(size_t) wi])) * g.amp;

                outL += hermite (bufL.data(), g.pos) * w * g.gainL;
                outR += hermite (bufR.data(), g.pos) * w * g.gainR;

                g.pos += g.inc;
                if (g.pos >= (double) size) g.pos -= (double) size;
                if (g.pos < 0.0)            g.pos += (double) size;
                if (++g.age >= g.length) g.active = false;
            }
            outL *= norm;
            outR *= norm;

            // ---- Escritura (con realimentación filtrada y saturada: nunca se embala)
            const float inL = L[i], inR = R[i];
            if (! s.freeze)
            {
                hpL = hpCoeff * (hpL + fbL - hpInL); hpInL = fbL;
                hpR = hpCoeff * (hpR + fbR - hpInR); hpInR = fbR;
                bufL[(size_t) writePos] = inL + fb * std::tanh (hpL);
                bufR[(size_t) writePos] = inR + fb * std::tanh (hpR);
                writePos = (writePos + 1) & mask;
            }
            fbL = outL; fbR = outR;

            L[i] = inL + (outL - inL) * mixV;
            R[i] = inR + (outR - inR) * mixV;
        }
    }

private:
    struct Grain
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        int age = 0, length = 1;
        float gainL = 1.0f, gainR = 1.0f, amp = 1.0f;
    };

    float rand01() noexcept
    {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (float) (rngState >> 8) * (1.0f / 16777216.0f);
    }

    void spawn (const Settings& s) noexcept
    {
        Grain* slot = nullptr;
        for (auto& g : grains) if (! g.active) { slot = &g; break; }
        if (slot == nullptr) return; // todas ocupadas: se omite (sin robo => sin clics)

        const int len = juce::jlimit (64, (int) (sampleRate * 0.5), (int) (s.sizeMs * 0.001 * sampleRate));
        const float semis = s.pitchSemis + s.detuneCents * 0.01f * (2.0f * rand01() - 1.0f);
        const double ratio = std::pow (2.0, (double) semis / 12.0);
        const bool rev = rand01() < s.reverse;

        const double lookahead = rev ? 0.0 : juce::jmax (0.0, (ratio - 1.0) * len);
        double delay = s.position * sampleRate * 2.0
                     + s.spray * rand01() * sampleRate * 0.5
                     + lookahead + 64.0;
        if (s.freeze) delay += len * ratio;
        delay = juce::jmin (delay, (double) size - (ratio + 1.0) * len - 256.0);

        double p = (double) writePos - delay;
        while (p < 0.0) p += (double) size;

        const float pan = s.spread * (2.0f * rand01() - 1.0f);
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

        slot->active = true;
        slot->pos = p;
        slot->inc = rev ? -ratio : ratio;
        slot->age = 0;
        slot->length = len;
        slot->gainL = std::cos (ang) * juce::MathConstants<float>::sqrt2;
        slot->gainR = std::sin (ang) * juce::MathConstants<float>::sqrt2;
        slot->amp = 0.8f + 0.2f * rand01();
    }

    float hermite (const float* buf, double pos) const noexcept
    {
        const int i1 = (int) pos;
        const float t = (float) (pos - (double) i1);
        const float xm1 = buf[(size_t) ((i1 - 1) & mask)];
        const float x0  = buf[(size_t) (i1 & mask)];
        const float x1  = buf[(size_t) ((i1 + 1) & mask)];
        const float x2  = buf[(size_t) ((i1 + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    static int64_t cycleIndex (double ppq, double beats) noexcept { return (int64_t) std::floor (ppq / beats); }

    double sampleRate = 44100.0;
    int size = 1, mask = 0, writePos = 0;
    std::vector<float> bufL, bufR;
    std::array<std::array<float, kWindowSize + 1>, 3> windows {};
    std::array<Grain, kMaxGrains> grains {};
    int64_t lastCycle = INT64_MIN;
    double samplesToNext = 0.0;
    float hpCoeff = 0.98f, hpL = 0, hpR = 0, hpInL = 0, hpInR = 0, fbL = 0, fbR = 0;
    float norm = 1.0f;
    uint32_t rngState = 0x12345678u;
    juce::SmoothedValue<float> mixSmooth, onSmooth;
};
} // namespace drift
