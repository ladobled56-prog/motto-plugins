#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

namespace drift
{
struct TransportState
{
    double ppqAtBlockStart = 0.0;
    double ppqPerSample    = 0.0;
    double bpm             = 120.0;
    bool   playing         = false;
    bool   jumped          = false;
};

/** Lee AudioPlayHead una vez por bloque. Si el host no entrega PPQ (o está detenido),
    corre un reloj interno libre al último BPM conocido, así los efectos rítmicos
    siguen funcionando con el transporte parado. */
class HostClock
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        predictedPpq = 0.0;
    }

    TransportState update (juce::AudioPlayHead* ph, int numSamples, double fallbackBpm = -1.0) noexcept
    {
        TransportState t;
        double bpm = fallbackBpm > 0.0 ? fallbackBpm : lastBpm;
        std::optional<double> hostPpq;

        if (ph != nullptr)
        {
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm())         bpm = *b;
                if (auto p = pos->getPpqPosition()) hostPpq = *p;
                t.playing = pos->getIsPlaying();
            }
        }

        t.bpm          = juce::jlimit (20.0, 999.0, std::isfinite (bpm) ? bpm : 120.0);
        t.ppqPerSample = t.bpm / (60.0 * sampleRate);

        const double expected = predictedPpq;

        if (t.playing && hostPpq.has_value() && std::isfinite (*hostPpq))
        {
            t.jumped = std::abs (*hostPpq - expected) > 1.0e-3;
            t.ppqAtBlockStart = *hostPpq;
        }
        else
        {
            t.ppqAtBlockStart = expected;
        }

        predictedPpq = t.ppqAtBlockStart + numSamples * t.ppqPerSample;
        lastBpm = t.bpm;
        return t;
    }

    double getLastBpm() const noexcept { return lastBpm; }

private:
    double sampleRate   = 44100.0;
    double predictedPpq = 0.0;
    double lastBpm      = 120.0;
};
} // namespace drift
