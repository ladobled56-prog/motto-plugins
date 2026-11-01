#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace drift
{
/** Coloración analógica / lo-fi estilo RC-20:
    Wobble (wow+flutter) -> Drive (oversampling 2x) -> Bitcrush/Downsample -> Tone -> Noise.
    La latencia es CONSTANTE (exista o no efecto) para no desfasar la rama dry. */
class Character
{
public:
    struct Settings
    {
        float on = 1.0f;
        float wow = 0.0f, flutter = 0.0f, drive = 0.0f;
        float bits = 16.0f, downsample = 1.0f, toneHz = 20000.0f, noiseDb = -80.0f;
        int   noiseType = 0;
    };

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        dlSize = juce::nextPowerOfTwo ((int) (sr * 0.05) + 8);
        dlMask = dlSize - 1;
        for (auto& d : delay) d.assign ((size_t) dlSize, 0.0f);
        baseDelay = (int) std::round (sr * 0.004);

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampling->initProcessing ((size_t) maxBlock);

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
        tone.prepare (spec);
        tone.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        tone.setResonance (0.6f);

        onSmooth.reset (sr, 0.05);
        noiseGain.reset (sr, 0.05);
        driveSmooth.reset (sr, 0.05);

        hissCoeff = std::exp (-juce::MathConstants<float>::twoPi * 7000.0f / (float) sr);
        envAtt = std::exp (-1.0f / (0.002f * (float) sr));
        envRel = std::exp (-1.0f / (0.3f * (float) sr));
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        dlWrite = 0;
        if (oversampling) oversampling->reset();
        tone.reset();
        wowPhase = flutPhase = 0.0;
        holdL = holdR = 0.0f; holdPhase = 1.0f;
        hissL = hissR = 0.0f; env = 0.0f; gate = 0.0f;
        crackleL = crackleR = 0.0f; drift = 0.0f;
    }

    int getLatency() const noexcept
    {
        return baseDelay + (oversampling ? (int) std::round (oversampling->getLatencyInSamples()) : 0);
    }

    void process (juce::dsp::AudioBlock<float> block, const Settings& s) noexcept
    {
        const int n = (int) block.getNumSamples();
        float* L = block.getChannelPointer (0);
        float* R = block.getChannelPointer (1);

        onSmooth.setTargetValue (s.on);
        driveSmooth.setTargetValue (s.drive);
        noiseGain.setTargetValue (juce::Decibels::decibelsToGain (s.noiseDb, -80.0f));

        const float wowDepth  = 0.0022f * (float) sampleRate * s.wow;
        const float flutDepth = 0.00025f * (float) sampleRate * s.flutter;
        const double wowInc  = 0.55 / sampleRate;
        const double flutInc = 8.5 / sampleRate;

        // ---- 1) Wobble: línea de retardo modulada (latencia base constante)
        for (int i = 0; i < n; ++i)
        {
            // seguidor de envolvente de la entrada (para que el ruido respete el silencio)
            const float a = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            env = a > env ? envAtt * env + (1.0f - envAtt) * a : envRel * env + (1.0f - envRel) * a;

            const float onV = onSmooth.getNextValue();
            drift += 0.0005f * ((rand01() * 2.0f - 1.0f) - drift);
            const float wowL = (float) std::sin (juce::MathConstants<double>::twoPi * wowPhase);
            const float wowR = (float) std::sin (juce::MathConstants<double>::twoPi * wowPhase + 0.4);
            const float fl   = (float) std::sin (juce::MathConstants<double>::twoPi * flutPhase) * 0.7f + drift * 0.3f;
            wowPhase += wowInc;  if (wowPhase >= 1.0) wowPhase -= 1.0;
            flutPhase += flutInc; if (flutPhase >= 1.0) flutPhase -= 1.0;

            delay[0][(size_t) dlWrite] = L[i];
            delay[1][(size_t) dlWrite] = R[i];
            L[i] = readDelay (0, (float) baseDelay + onV * (wowDepth * wowL + flutDepth * fl));
            R[i] = readDelay (1, (float) baseDelay + onV * (wowDepth * wowR + flutDepth * fl));
            dlWrite = (dlWrite + 1) & dlMask;
        }

        // ---- 2) Drive con oversampling 2x (siempre procesa: latencia constante)
        {
            auto up = oversampling->processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            float* uL = up.getChannelPointer (0);
            float* uR = up.getChannelPointer (1);
            const float onV = onSmooth.getCurrentValue();
            const float d = driveSmooth.getNextValue();
            driveSmooth.skip (n - 1);
            const float g = 1.0f + 9.0f * d;
            const float amt = onV * juce::jmin (1.0f, d * 5.0f);
            const float makeup = 1.0f / std::sqrt (g);
            const float bias = 0.15f * d;
            const float biasOut = std::tanh (bias);
            if (amt > 1.0e-4f)
                for (int i = 0; i < un; ++i)
                {
                    const float yl = (std::tanh (g * uL[i] + bias) - biasOut) * makeup;
                    const float yr = (std::tanh (g * uR[i] + bias) - biasOut) * makeup;
                    uL[i] += amt * (yl - uL[i]);
                    uR[i] += amt * (yr - uR[i]);
                }
            oversampling->processSamplesDown (block);
        }

        // ---- 3) Bitcrush + downsample, 4) Tone, 5) Noise
        const float onV = onSmooth.getCurrentValue();
        const float effBits = 16.0f + (s.bits - 16.0f) * onV;
        const bool crush = effBits < 15.9f;
        const float levels = std::pow (2.0f, effBits - 1.0f);
        const float dsFactor = 1.0f + (s.downsample - 1.0f) * onV;
        const float dsInc = 1.0f / juce::jmax (1.0f, dsFactor);

        const float cutoff = juce::jlimit (20.0f, (float) sampleRate * 0.45f, 20000.0f + (s.toneHz - 20000.0f) * onV);
        tone.setCutoffFrequency (cutoff);
        const bool toneActive = cutoff < (float) sampleRate * 0.44f && cutoff < 19500.0f;

        const float crackleRate = 9.0f / (float) sampleRate;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];

            if (dsFactor > 1.001f)
            {
                holdPhase += dsInc;
                if (holdPhase >= 1.0f) { holdPhase -= 1.0f; holdL = l; holdR = r; }
                l = holdL; r = holdR;
            }
            if (crush)
            {
                l = std::round (l * levels) / levels;
                r = std::round (r * levels) / levels;
            }
            if (toneActive)
            {
                l = tone.processSample (0, l);
                r = tone.processSample (1, r);
            }

            // Ruido: "se calla" cuando la entrada calla
            const float gateTarget = juce::jmin (1.0f, env * 20.0f);
            gate += 0.0005f * (gateTarget - gate);
            const float ng = noiseGain.getNextValue() * onV * gate;
            if (ng > 1.0e-6f)
            {
                hissL = hissCoeff * hissL + (1.0f - hissCoeff) * (rand01() * 2.0f - 1.0f);
                hissR = hissCoeff * hissR + (1.0f - hissCoeff) * (rand01() * 2.0f - 1.0f);
                float nl = hissL * 3.0f, nr = hissR * 3.0f;
                if (s.noiseType == 1)
                {
                    if (rand01() < crackleRate) crackleL = (rand01() * 2.0f - 1.0f) * std::pow (rand01(), 2.0f) * 4.0f;
                    if (rand01() < crackleRate) crackleR = (rand01() * 2.0f - 1.0f) * std::pow (rand01(), 2.0f) * 4.0f;
                    nl = nl * 0.35f + crackleL; nr = nr * 0.35f + crackleR;
                    crackleL *= 0.7f; crackleR *= 0.7f;
                }
                l += nl * ng;
                r += nr * ng;
            }

            L[i] = l; R[i] = r;
        }
    }

private:
    float readDelay (int ch, float d) const noexcept
    {
        const float rp = (float) dlWrite - d;
        const int i0 = (int) std::floor (rp);
        const float t = rp - (float) i0;
        const auto& b = delay[(size_t) ch];
        const float xm1 = b[(size_t) ((i0 - 1) & dlMask)];
        const float x0  = b[(size_t) (i0 & dlMask)];
        const float x1  = b[(size_t) ((i0 + 1) & dlMask)];
        const float x2  = b[(size_t) ((i0 + 2) & dlMask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    float rand01() noexcept
    {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (float) (rngState >> 8) * (1.0f / 16777216.0f);
    }

    double sampleRate = 44100.0;
    std::array<std::vector<float>, 2> delay;
    int dlSize = 1, dlMask = 0, dlWrite = 0, baseDelay = 0;
    double wowPhase = 0.0, flutPhase = 0.0;
    float drift = 0.0f;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::StateVariableTPTFilter<float> tone;

    float holdL = 0, holdR = 0, holdPhase = 1.0f;
    float hissCoeff = 0.5f, hissL = 0, hissR = 0, crackleL = 0, crackleR = 0;
    float env = 0, envAtt = 0, envRel = 0, gate = 0;
    uint32_t rngState = 0xA5A5F00Du;
    juce::SmoothedValue<float> onSmooth, noiseGain, driveSmooth;
};
} // namespace drift
