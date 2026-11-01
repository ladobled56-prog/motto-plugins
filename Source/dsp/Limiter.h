#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace drift
{
/** Limitador estéreo con lookahead (techo real, sin distorsión de clip duro en uso normal).
    Mínimo deslizante de la ganancia requerida + ataque suave dentro del lookahead + release.
    La latencia (kLookahead) se suma a la reportada al host. */
class Limiter
{
public:
    static constexpr int kLookahead = 64;

    void prepare (double sr, float ceilingDb = -0.5f)
    {
        ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        attackCoeff = 1.0f - std::exp (-6.0f / (float) kLookahead);
        releaseCoeff = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
        reset();
    }

    void reset() noexcept
    {
        std::fill (std::begin (delayL), std::end (delayL), 0.0f);
        std::fill (std::begin (delayR), std::end (delayR), 0.0f);
        std::fill (std::begin (req), std::end (req), 1.0f);
        pos = 0; rpos = 0;
        env = 1.0f;
        minGain = 1.0f;
    }

    void process (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float peak = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            // Ventana = muestra que sale ahora (hace kLookahead muestras) ... muestra que entra
            req[rpos] = peak > ceiling ? ceiling / peak : 1.0f;
            rpos = (rpos + 1) % (kLookahead + 1);

            float target = 1.0f;
            for (int k = 0; k <= kLookahead; ++k) target = juce::jmin (target, req[k]);

            env += (target < env ? attackCoeff : releaseCoeff) * (target - env);
            const float g = env;

            const float dl = delayL[pos], dr = delayR[pos];
            delayL[pos] = L[i]; delayR[pos] = R[i];
            pos = (pos + 1) % kLookahead;

            // Red de seguridad final (sólo actúa si algo se escapa al lookahead)
            if (std::abs (dl * g) > ceiling * 1.001f || std::abs (dr * g) > ceiling * 1.001f) ++hardClips;
            ++samplesProcessed;
            L[i] = juce::jlimit (-ceiling, ceiling, dl * g);
            R[i] = juce::jlimit (-ceiling, ceiling, dr * g);
            minGain = juce::jmin (minGain, g);
        }
    }

    /** Reducción de ganancia máxima desde la última consulta (dB, <= 0). */
    float takeGainReductionDb() noexcept
    {
        const float db = juce::Decibels::gainToDecibels (minGain, -60.0f);
        minGain = 1.0f;
        return db;
    }

    float getCeiling() const noexcept { return ceiling; }

    // Diagnóstico (pruebas): cuántas muestras tuvo que recortar la red de seguridad
    uint64_t hardClips = 0, samplesProcessed = 0;

private:
    float delayL[kLookahead] {}, delayR[kLookahead] {}, req[kLookahead + 1] {};
    int pos = 0, rpos = 0;
    float env = 1.0f, minGain = 1.0f;
    float ceiling = 0.944f, attackCoeff = 0.1f, releaseCoeff = 0.001f;
};
} // namespace drift
