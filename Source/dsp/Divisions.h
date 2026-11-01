#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace drift
{
enum class Groove : uint8_t { Straight = 0, Triplet = 1, Dotted = 2 };

struct Division
{
    const char* name;
    double beats;   // duración en negras (quarter notes)
    Groove groove;
};

inline constexpr std::array<Division, 13> kDivisions {{
    { "1/1",   4.0,        Groove::Straight },
    { "1/2",   2.0,        Groove::Straight },
    { "1/4",   1.0,        Groove::Straight },
    { "1/8",   0.5,        Groove::Straight },
    { "1/16",  0.25,       Groove::Straight },
    { "1/32",  0.125,      Groove::Straight },
    { "1/2T",  4.0 / 3.0,  Groove::Triplet  },
    { "1/4T",  2.0 / 3.0,  Groove::Triplet  },
    { "1/8T",  1.0 / 3.0,  Groove::Triplet  },
    { "1/16T", 1.0 / 6.0,  Groove::Triplet  },
    { "1/4D",  1.5,        Groove::Dotted   },
    { "1/8D",  0.75,       Groove::Dotted   },
    { "1/16D", 0.375,      Groove::Dotted   },
}};

inline constexpr int kNumDivisions = (int) kDivisions.size();

inline juce::StringArray divisionNames()
{
    juce::StringArray s;
    for (const auto& d : kDivisions)
        s.add (d.name);
    return s;
}

inline const Division& divisionAt (float rawIndex) noexcept
{
    const int i = juce::jlimit (0, kNumDivisions - 1, (int) std::lround (rawIndex));
    return kDivisions[(size_t) i];
}

/** Fase [0,1) derivada sin estado de la posición PPQ: deriva cero respecto al DAW. */
inline double phaseFor (double ppq, double divisionBeats) noexcept
{
    const double ph = std::fmod (ppq / divisionBeats, 1.0);
    return ph < 0.0 ? ph + 1.0 : ph;
}

/** Índice entero de ciclo (para S&H y pasos). */
inline int64_t cycleIndex (double ppq, double divisionBeats) noexcept
{
    return (int64_t) std::floor (ppq / divisionBeats);
}

/** Hash determinista -> [0,1). Mismo ciclo del DAW = mismo valor (S&H reproducible). */
inline float hashToUnit (int64_t n, uint32_t salt = 0) noexcept
{
    uint64_t x = (uint64_t) n * 0x9E3779B97F4A7C15ull + salt;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (float) (x >> 40) / (float) (1ull << 24);
}

/** Patrón euclidiano (Bresenham) como máscara de bits. */
inline uint32_t euclidMask (int steps, int hits, int rotate) noexcept
{
    steps = juce::jlimit (1, 32, steps);
    hits  = juce::jlimit (0, steps, hits);
    uint32_t mask = 0;
    for (int i = 0; i < steps; ++i)
        if (((i * hits) % steps) < hits)
            mask |= (1u << ((i + rotate) % steps));
    return mask;
}
} // namespace drift
