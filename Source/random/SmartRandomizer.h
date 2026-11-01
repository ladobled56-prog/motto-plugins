#pragma once

#include "../Params.h"
#include "../dsp/Divisions.h"
#include "Rng.h"
#include <array>
#include <deque>
#include <vector>

namespace drift
{
using PresetValues = std::array<float, kNumParams>; // valores en unidades reales (índice para choice/int/bool)

struct GeneratedPreset
{
    PresetValues values {};
    uint64_t seed = 0;
    int archetype = -1;
    float energy = 0.0f;
};

struct Archetype
{
    const char* name;
    std::array<float, 4> moduleOn;                 // Granular, Character, Shaper, Space
    std::array<float, 3> grooveW;                  // Straight, Triplet, Dotted
    std::array<float, kNumDivisions> grainDivW;
    std::array<float, kNumDivisions> shaperDivW;
    std::array<float, kPitchSemis.size()> pitchW;
    std::array<float, 3> windowW;
    float syncProb;
    float energyBudget;
    struct Override { P p; float lo, hi; };
    std::vector<Override> overrides;
};

/** Aleatorización restringida en 5 capas:
    1) arquetipo (cadena de Markov)  2) distribuciones por parámetro
    3) gramática rítmica según tempo  4) reglas + presupuesto de energía
    5) novedad respecto al historial.
    Corre en el hilo de mensajes (microsegundos); nunca en el de audio. */
class SmartRandomizer
{
public:
    SmartRandomizer();

    /** current = valores actuales; locked[m] = módulo bloqueado; variation 0..1 */
    GeneratedPreset generate (const PresetValues& current, double bpm, float variation,
                              const std::array<bool, 6>& lockedModules);

    /** Genera con una semilla concreta (reproducible, sin chequeo de novedad). */
    GeneratedPreset generateFromSeed (uint64_t seed, const PresetValues& current, double bpm,
                                      float variation, const std::array<bool, 6>& lockedModules,
                                      int previousArchetype) const;

    static float noveltyDistance (const PresetValues& a, const PresetValues& b,
                                  const std::array<bool, 6>& lockedModules);
    static float energyOf (const PresetValues& v);

    const std::vector<Archetype>& getArchetypes() const noexcept { return archetypes; }
    float energyBudgetOf (int archetype) const noexcept { return archetypes[(size_t) archetype].energyBudget; }

    static constexpr int   kMaxAttempts  = 12;
    static constexpr float kMinNovelty   = 0.12f;
    static constexpr int   kHistorySize  = 16;
    static constexpr double kMinShaperPeriodMs = 40.0;
    static constexpr double kMinGrainPeriodMs  = 25.0;

private:
    std::vector<Archetype> archetypes;
    std::array<std::array<float, 5>, 5> transition {};
    std::deque<PresetValues> history;
    int lastArchetype = -1;
    uint64_t seedCounter = 0;
    uint64_t seedBase = 0;
};
} // namespace drift
