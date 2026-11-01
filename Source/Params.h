#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

namespace drift
{
/** Índices de parámetro. El orden aquí ES el orden de la tabla en Params.cpp. */
enum class P : int
{
    // Global
    Mix, InputGain, OutputGain, AutoGain, Variation,
    LockGr, LockCh, LockSh, LockSp, LockMd, Tempo,
    // Granular
    GrOn, GrMix, GrSync, GrDiv, GrDensity, GrSize, GrPitch, GrDetune,
    GrPosition, GrSpray, GrReverse, GrSpread, GrFeedback, GrWindow, GrFreeze,
    // Character / Lo-fi
    ChOn, ChWow, ChFlutter, ChDrive, ChBits, ChDownsample, ChTone, ChNoise, ChNoiseType,
    // Shaper rítmico
    ShOn, ShDiv, ShSteps, ShHits, ShRotate, ShDepth, ShCurve, ShSmooth,
    ShFiltDepth, ShFiltFreq, ShFiltRes, ShPanDepth, ShPanDiv,
    // Modulación
    MdDiv, MdShape, MdTarget, MdDepth, MdEnvTarget, MdEnvAmount,
    // Space
    SpOn, SpDelayDiv, SpDelayFb, SpDelayMix, SpRevSize, SpRevDamp, SpRevMix,

    Count
};

inline constexpr int kNumParams = (int) P::Count;
inline constexpr int idx (P p) noexcept { return (int) p; }

enum class Module : uint8_t { Global, Granular, Character, Shaper, Space, Mod };
enum class Kind   : uint8_t { Float, Bool, Choice, Int };

struct ParamSpec
{
    P id;
    juce::String key;
    juce::String name;
    Module module;
    Kind kind;
    float minV, maxV, defV;
    float centre;              // > 0: skew para que este valor quede al centro del control
    juce::String unit;
    juce::StringArray choices;

    // Metadatos del randomizer
    bool  randomize;           // false = el randomizer nunca lo toca
    float rndLo, rndHi;        // rango "musical" (unidades reales), subconjunto del rango duro
    float risk;                // peso en el presupuesto de energía
    bool  riskInverted;        // true = valores BAJOS son los peligrosos (ej. bits)

    juce::NormalisableRange<float> range() const;
};

const std::vector<ParamSpec>& specs();
inline const ParamSpec& spec (P p) { return specs()[(size_t) idx (p)]; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Tablas auxiliares
inline constexpr std::array<float, 9> kPitchSemis { -12.f, -7.f, -5.f, 0.f, 5.f, 7.f, 12.f, 19.f, 24.f };

enum class ModTarget : int
{
    Off = 0, GrainSize, GrainSpray, GrainDensity, GrainPosition, Drive, Tone, Crush, ShaperFilter, SpaceMix, Count
};

/** Parámetro al que apunta cada destino de modulación (ignorado para Off). */
inline constexpr std::array<P, (size_t) ModTarget::Count> kModTargetParam {
    P::Mix, P::GrSize, P::GrSpray, P::GrDensity, P::GrPosition, P::ChDrive, P::ChTone, P::ChBits, P::ShFiltFreq, P::SpRevMix
};

juce::StringArray modTargetNames();
} // namespace drift
