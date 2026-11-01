#include "SmartRandomizer.h"
#include <algorithm>

namespace drift
{
namespace
{
const std::vector<juce::NormalisableRange<float>>& ranges()
{
    static const std::vector<juce::NormalisableRange<float>> r = []
    {
        std::vector<juce::NormalisableRange<float>> out;
        for (const auto& s : specs()) out.push_back (s.range());
        return out;
    }();
    return r;
}

float toNorm (P p, float v)
{
    const auto& r = ranges()[(size_t) idx (p)];
    return r.convertTo0to1 (juce::jlimit (r.start, r.end, v));
}

float fromNorm (P p, float n)
{
    const auto& r = ranges()[(size_t) idx (p)];
    return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
}

int moduleIndex (Module m) { return (int) m; }

P onFlagOf (Module m)
{
    switch (m)
    {
        case Module::Granular:  return P::GrOn;
        case Module::Character: return P::ChOn;
        case Module::Shaper:    return P::ShOn;
        case Module::Space:     return P::SpOn;
        case Module::Global:
        case Module::Mod:       return P::Count;
    }
    return P::Count;
}

bool moduleIsOn (const PresetValues& v, Module m)
{
    const P f = onFlagOf (m);
    return f == P::Count || v[(size_t) idx (f)] > 0.5f;
}

using DivW = std::array<float, kNumDivisions>;

//                       1/1  1/2  1/4  1/8  1/16 1/32 1/2T 1/4T 1/8T 1/16T 1/4D 1/8D 1/16D
constexpr DivW kGrainFine  { 0,   0,   0,   0.5f,1,   1,   0,   0,   0.6f,1,    0,   0.3f,0.6f };
constexpr DivW kGrainMid   { 0,   0,   0.3f,1,   1,   0.4f,0,   0.3f,0.8f,0.5f, 0.2f,0.6f,0.5f };
constexpr DivW kGrainSlow  { 0.2f,0.5f,1,   1,   0.5f,0,   0.3f,0.6f,0.5f,0,    0.5f,0.6f,0.2f };
constexpr DivW kShapeBusy  { 0,   0,   0.2f,0.8f,1,   0.3f,0,   0.2f,0.9f,0.6f, 0,   0.4f,0.6f };
constexpr DivW kShapeMid   { 0,   0.1f,0.6f,1,   0.8f,0,   0.1f,0.5f,0.7f,0.2f, 0.3f,0.6f,0.3f };
constexpr DivW kShapeSlow  { 0.3f,0.6f,1,   0.7f,0.2f,0,   0.4f,0.6f,0.3f,0,    0.6f,0.5f,0   };
constexpr DivW kPanDivs    { 0.6f,1,   1,   0.4f,0,   0,   0.6f,0.6f,0.2f,0,    0.7f,0.3f,0   };
constexpr DivW kLfoDivs    { 1,   1,   0.6f,0,   0,   0,   0.5f,0.3f,0,   0,    0.6f,0,   0   };
constexpr DivW kDelayDivs  { 0,   0,   0.6f,0.8f,0.4f,0,   0,   0.6f,0.7f,0,    0.7f,1,   0.6f };

/** Elige una división respetando el groove del preset y el período en ms al BPM actual. */
int pickDivision (Rng& r, const DivW& base, Groove groove, double bpm, double minMs, double maxMs)
{
    const double msPerBeat = 60000.0 / bpm;
    DivW w {};
    for (int i = 0; i < kNumDivisions; ++i)
    {
        const auto& d = kDivisions[(size_t) i];
        const double ms = d.beats * msPerBeat;
        const bool inRange = ms >= minMs && ms <= maxMs;
        w[(size_t) i] = inRange ? base[(size_t) i] * (d.groove == groove ? 1.0f : 0.1f) : 0.0f;
    }

    int pick = r.pickWeighted (w.data(), kNumDivisions);
    if (pick >= 0)
        return pick;

    // Fallback: la división recta más cercana al centro del rango permitido
    const double targetMs = std::sqrt (std::max (1.0, minMs) * maxMs);
    double best = 1e18;
    for (int i = 0; i < kNumDivisions; ++i)
    {
        const double ms = kDivisions[(size_t) i].beats * msPerBeat;
        const double d = std::abs (std::log (ms / targetMs));
        if (ms >= minMs && d < best) { best = d; pick = i; }
    }
    return pick >= 0 ? pick : 4; // 1/16
}

double divisionMs (float rawIndex, double bpm) { return divisionAt (rawIndex).beats * 60000.0 / bpm; }
} // namespace

//==============================================================================
SmartRandomizer::SmartRandomizer()
{
    using O = Archetype::Override;
    //                         -12  -7   -5   0    +5   +7   +12  +19  +24
    const std::array<float, 9> pitchNeutral { 0.5f,0.1f,0.2f,3.0f,0.2f,0.5f,1.0f,0.2f,0.1f };
    const std::array<float, 9> pitchBright  { 0.2f,0,   0,   0.8f,0.2f,0.8f,2.0f,1.0f,0.8f };
    const std::array<float, 9> pitchDark    { 1.5f,0.3f,0.5f,2.0f,0,   0.2f,0.3f,0,   0    };

    archetypes = {
        { "Granular Cloud", { 1.0f, 0.6f, 0.25f, 0.9f }, { 0.7f, 0.2f, 0.1f },
          kGrainMid, kShapeSlow, pitchNeutral, { 1.0f, 0.6f, 0.1f }, 0.4f, 2.6f,
          { O { P::GrSize, 120.f, 450.f }, O { P::GrMix, 0.55f, 0.9f }, O { P::GrSpray, 0.3f, 0.8f },
            O { P::GrDensity, 10.f, 40.f }, O { P::GrFeedback, 0.2f, 0.6f }, O { P::ShDepth, 0.2f, 0.5f } } },

        { "Glitch Chop", { 0.8f, 0.7f, 1.0f, 0.5f }, { 0.6f, 0.3f, 0.1f },
          kGrainFine, kShapeBusy, pitchNeutral, { 0.2f, 0.5f, 1.0f }, 0.95f, 3.0f,
          { O { P::GrSize, 15.f, 80.f }, O { P::GrReverse, 0.2f, 0.6f }, O { P::ChBits, 6.f, 12.f },
            O { P::ChDownsample, 2.f, 10.f }, O { P::ShDepth, 0.7f, 1.f }, O { P::ShCurve, 0.f, 0.4f },
            O { P::ShSmooth, 1.f, 4.f } } },

        { "Dusty Tape", { 0.35f, 1.0f, 0.3f, 0.7f }, { 0.7f, 0.15f, 0.15f },
          kGrainSlow, kShapeMid, pitchDark, { 1.0f, 0.5f, 0.2f }, 0.5f, 2.4f,
          { O { P::ChWow, 0.2f, 0.6f }, O { P::ChFlutter, 0.1f, 0.5f }, O { P::ChDrive, 0.2f, 0.6f },
            O { P::ChTone, 2500.f, 9000.f }, O { P::ChNoise, -60.f, -38.f }, O { P::ChBits, 10.f, 16.f },
            O { P::GrMix, 0.2f, 0.5f } } },

        { "Rhythmic Stutter", { 0.6f, 0.5f, 1.0f, 0.6f }, { 0.55f, 0.3f, 0.15f },
          kGrainMid, kShapeBusy, pitchNeutral, { 0.5f, 1.0f, 0.6f }, 1.0f, 2.8f,
          { O { P::ShDepth, 0.6f, 1.f }, O { P::ShFiltDepth, 0.3f, 0.8f }, O { P::ShPanDepth, 0.2f, 0.7f },
            O { P::GrSize, 30.f, 150.f } } },

        { "Shimmer Pad", { 1.0f, 0.3f, 0.15f, 1.0f }, { 0.8f, 0.1f, 0.1f },
          kGrainSlow, kShapeSlow, pitchBright, { 1.0f, 0.4f, 0.0f }, 0.3f, 2.6f,
          { O { P::GrSize, 200.f, 450.f }, O { P::GrFeedback, 0.3f, 0.65f }, O { P::SpRevMix, 0.25f, 0.45f },
            O { P::SpRevSize, 0.6f, 0.9f }, O { P::ChDrive, 0.f, 0.3f }, O { P::GrMix, 0.5f, 0.85f } } },
    };

    // Matriz de Markov: diagonal baja => casi nunca repite el carácter anterior
    transition = {{
        { 0.05f, 0.25f, 0.20f, 0.20f, 0.30f },
        { 0.20f, 0.05f, 0.25f, 0.35f, 0.15f },
        { 0.25f, 0.25f, 0.05f, 0.20f, 0.25f },
        { 0.20f, 0.35f, 0.20f, 0.05f, 0.20f },
        { 0.30f, 0.15f, 0.30f, 0.20f, 0.05f },
    }};

    seedBase = (uint64_t) juce::Random::getSystemRandom().nextInt64()
             ^ (uint64_t) juce::Time::getHighResolutionTicks();
}

//==============================================================================
GeneratedPreset SmartRandomizer::generateFromSeed (uint64_t seed, const PresetValues& current, double bpm,
                                                   float variation, const std::array<bool, 6>& locked,
                                                   int previousArchetype) const
{
    Rng r (seed);
    bpm = juce::jlimit (20.0, 999.0, bpm);
    variation = juce::jlimit (0.0f, 1.0f, variation);

    GeneratedPreset out;
    out.seed = seed;

    // ---- Capa 1: arquetipo por cadena de Markov
    int a;
    if (previousArchetype >= 0 && previousArchetype < (int) archetypes.size())
        a = r.pickWeighted (transition[(size_t) previousArchetype].data(), (int) archetypes.size());
    else
        a = r.below ((int) archetypes.size());
    out.archetype = a;
    const Archetype& arch = archetypes[(size_t) a];

    PresetValues v = current;
    auto isLocked = [&] (P p) { return locked[(size_t) moduleIndex (spec (p).module)]; };
    auto set = [&] (P p, float value) { if (! isLocked (p)) v[(size_t) idx (p)] = value; };
    auto get = [&] (P p) { return v[(size_t) idx (p)]; };

    const auto grooveIdx = r.pickWeighted (arch.grooveW.data(), 3);
    const Groove groove = (Groove) juce::jmax (0, grooveIdx);

    // ---- Módulos activos (con al menos 2 encendidos entre los desbloqueados)
    const Module mods[4] = { Module::Granular, Module::Character, Module::Shaper, Module::Space };
    int onCount = 0;
    for (int m = 0; m < 4; ++m)
    {
        const P flag = onFlagOf (mods[m]);
        if (! locked[(size_t) moduleIndex (mods[m])])
            set (flag, r.chance (arch.moduleOn[(size_t) m]) ? 1.0f : 0.0f);
        onCount += get (flag) > 0.5f ? 1 : 0;
    }
    while (onCount < 2)
    {
        int best = -1; float bestP = -1.0f;
        for (int m = 0; m < 4; ++m)
            if (! locked[(size_t) moduleIndex (mods[m])] && get (onFlagOf (mods[m])) < 0.5f && arch.moduleOn[(size_t) m] > bestP)
                { best = m; bestP = arch.moduleOn[(size_t) m]; }
        if (best < 0) break;
        set (onFlagOf (mods[best]), 1.0f);
        ++onCount;
    }

    // ---- Capa 2: continuos (uniforme en dominio normalizado; triangular si el arquetipo lo acota)
    for (const auto& s : specs())
    {
        if (! s.randomize || s.kind != Kind::Float || isLocked (s.id))
            continue;

        float lo = s.rndLo, hi = s.rndHi;
        bool overridden = false;
        for (const auto& o : arch.overrides)
            if (o.p == s.id) { lo = o.lo; hi = o.hi; overridden = true; }

        const float nlo = toNorm (s.id, lo), nhi = toNorm (s.id, hi);
        const float n = overridden ? r.triangular (nlo, nhi) : r.uniform (nlo, nhi);
        set (s.id, fromNorm (s.id, n));
    }

    // Casos especiales: lo-fi digital suele quedar limpio salvo que el arquetipo lo pida
    auto hasOverride = [&] (P p) { for (auto& o : arch.overrides) if (o.p == p) return true; return false; };
    if (! hasOverride (P::ChBits) && r.chance (0.55f))        set (P::ChBits, 16.0f);
    if (! hasOverride (P::ChDownsample) && r.chance (0.6f))   set (P::ChDownsample, 1.0f);

    // ---- Capa 3: gramática rítmica (todo comparte el groove del preset)
    set (P::GrSync, r.chance (arch.syncProb) ? 1.0f : 0.0f);
    set (P::GrDiv, (float) pickDivision (r, arch.grainDivW, groove, bpm, kMinGrainPeriodMs, 1000.0));
    set (P::GrPitch, (float) juce::jmax (0, r.pickWeighted (arch.pitchW.data(), (int) arch.pitchW.size())));
    set (P::GrWindow, (float) juce::jmax (0, r.pickWeighted (arch.windowW.data(), 3)));
    set (P::ChNoiseType, r.chance (0.5f) ? 1.0f : 0.0f);

    set (P::ShDiv, (float) pickDivision (r, arch.shaperDivW, groove, bpm, kMinShaperPeriodMs, 2000.0));
    {
        int steps;
        if (groove == Groove::Triplet) steps = r.chance (0.6f) ? 12 : 6;
        else                           steps = r.chance (0.65f) ? 16 : 8;
        const int kMin = juce::jmax (1, (int) std::ceil (steps * 0.3));
        const int kMax = juce::jmax (kMin, (int) std::floor (steps * 0.8));
        set (P::ShSteps, (float) steps);
        set (P::ShHits, (float) (kMin + r.below (kMax - kMin + 1)));
        set (P::ShRotate, r.chance (0.8f) ? 0.0f : (float) r.below (steps)); // tiempo fuerte garantizado 80%
    }
    set (P::ShPanDiv, (float) pickDivision (r, kPanDivs, groove, bpm, 150.0, 8000.0));
    set (P::MdDiv, (float) pickDivision (r, kLfoDivs, groove, bpm, 200.0, 16000.0));
    set (P::MdShape, (float) r.below (6));
    set (P::SpDelayDiv, (float) pickDivision (r, kDelayDivs, groove, bpm, 80.0, 2000.0));

    // Destinos de modulación: sólo hacia módulos encendidos
    {
        auto targetModule = [] (int t) -> Module
        {
            switch ((ModTarget) t)
            {
                case ModTarget::GrainSize: case ModTarget::GrainSpray:
                case ModTarget::GrainDensity: case ModTarget::GrainPosition: return Module::Granular;
                case ModTarget::Drive: case ModTarget::Tone: case ModTarget::Crush: return Module::Character;
                case ModTarget::ShaperFilter: return Module::Shaper;
                case ModTarget::SpaceMix: return Module::Space;
                case ModTarget::Off: case ModTarget::Count: return Module::Global;
            }
            return Module::Global;
        };
        std::array<float, (size_t) ModTarget::Count> w {};
        std::array<float, (size_t) ModTarget::Count> we {};
        w[0] = 0.25f; we[0] = 1.0f;
        for (int t = 1; t < (int) ModTarget::Count; ++t)
        {
            const bool ok = moduleIsOn (v, targetModule (t));
            w[(size_t) t]  = ok ? 0.1f : 0.0f;
            we[(size_t) t] = ok ? 0.1f : 0.0f;
        }
        w[(size_t) ModTarget::Crush] *= 0.3f; // el crush modulado es agresivo: menos probable
        set (P::MdTarget,    (float) juce::jmax (0, r.pickWeighted (w.data(), (int) w.size())));
        set (P::MdEnvTarget, (float) juce::jmax (0, r.pickWeighted (we.data(), (int) we.size())));
    }

    // ---- Variación: mezcla con el preset actual (1 = totalmente nuevo)
    if (variation < 0.999f)
    {
        for (const auto& s : specs())
        {
            if (! s.randomize || isLocked (s.id)) continue;
            const size_t i = (size_t) idx (s.id);
            if (s.kind == Kind::Float)
            {
                const float nc = toNorm (s.id, current[i]), nn = toNorm (s.id, v[i]);
                v[i] = fromNorm (s.id, nc + variation * (nn - nc));
            }
            else if (! r.chance (variation))
            {
                v[i] = current[i];
            }
        }
    }

    // ---- Capa 4a: reglas condicionales
    auto clampMax = [&] (P p, float mx) { if (get (p) > mx) set (p, mx); };
    auto clampMin = [&] (P p, float mn) { if (get (p) < mn) set (p, mn); };
    const float semis = kPitchSemis[(size_t) juce::jlimit (0, (int) kPitchSemis.size() - 1, (int) get (P::GrPitch))];

    if (get (P::GrFeedback) > 0.55f) { clampMax (P::GrDensity, 20.f); clampMax (P::SpRevMix, 0.35f); }
    if (get (P::GrFeedback) > 0.5f && std::abs (semis) >= 12.f) clampMax (P::GrFeedback, 0.5f);
    if (get (P::ChBits) < 8.f) { clampMax (P::ChNoise, -45.f); clampMax (P::ChDrive, 0.5f); }
    if (semis >= 19.f) clampMin (P::GrSize, 60.f);
    if (get (P::ChDownsample) > 6.f) clampMax (P::ChTone, 12000.f);
    if (get (P::SpDelayFb) > 0.5f) clampMax (P::SpDelayMix, 0.35f);
    if (get (P::ShFiltRes) > 0.45f) clampMin (P::ShFiltFreq, 1500.f);
    if (get (P::ShOn) > 0.5f && get (P::ShDepth) > 0.7f)
    {
        const int steps = (int) get (P::ShSteps);
        if (get (P::ShHits) < std::ceil ((float) steps * 0.25f)) set (P::ShHits, std::ceil ((float) steps * 0.25f));
        clampMin (P::ShSmooth, 1.5f);
    }
    set (P::ShHits, juce::jmin (get (P::ShHits), get (P::ShSteps)));
    if (get (P::GrSync) > 0.5f && divisionMs (get (P::GrDiv), bpm) < kMinGrainPeriodMs)
        set (P::GrDiv, (float) pickDivision (r, kGrainMid, groove, bpm, kMinGrainPeriodMs, 1000.0));
    if (get (P::ShOn) > 0.5f && divisionMs (get (P::ShDiv), bpm) < kMinShaperPeriodMs)
        set (P::ShDiv, (float) pickDivision (r, kShapeMid, groove, bpm, kMinShaperPeriodMs, 2000.0));
    if (moduleIsOn (v, Module::Space) && get (P::SpRevMix) > 0.35f && get (P::GrMix) > 0.7f) clampMax (P::Mix, 0.8f);

    // ---- Capa 4b: presupuesto de energía
    for (int iter = 0; iter < 80 && energyOf (v) > arch.energyBudget; ++iter)
    {
        int worst = -1; float worstC = 0.0f;
        for (const auto& s : specs())
        {
            if (s.risk <= 0.0f || ! s.randomize || isLocked (s.id) || ! moduleIsOn (v, s.module)) continue;
            const float n = toNorm (s.id, get (s.id));
            const float c = s.risk * (s.riskInverted ? 1.0f - n : n);
            if (c > worstC) { worstC = c; worst = idx (s.id); }
        }
        if (worst < 0) break;
        const auto& s = specs()[(size_t) worst];
        const float safeN = toNorm (s.id, s.riskInverted ? s.rndHi : s.rndLo);
        const float n = toNorm (s.id, get (s.id));
        set (s.id, fromNorm (s.id, n + 0.15f * (safeN - n)));
    }

    out.values = v;
    out.energy = energyOf (v);
    return out;
}

GeneratedPreset SmartRandomizer::generate (const PresetValues& current, double bpm, float variation,
                                           const std::array<bool, 6>& locked)
{
    if (history.empty() || noveltyDistance (history.back(), current, locked) > 1.0e-4f)
        history.push_back (current);

    const float threshold = kMinNovelty * juce::jlimit (0.0f, 1.0f, variation);
    GeneratedPreset best;
    float bestDist = -1.0f;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
    {
        uint64_t x = seedBase + (++seedCounter) * 0xD1B54A32D192ED03ull;
        const uint64_t seed = Rng::splitmix (x);
        auto candidate = generateFromSeed (seed, current, bpm, variation, locked, lastArchetype);

        float minDist = 1.0f;
        for (const auto& h : history)
            minDist = std::min (minDist, noveltyDistance (candidate.values, h, locked));

        if (minDist > bestDist) { bestDist = minDist; best = candidate; }
        if (minDist > threshold) break;
    }

    lastArchetype = best.archetype;
    history.push_back (best.values);
    while ((int) history.size() > kHistorySize)
        history.pop_front();
    return best;
}

float SmartRandomizer::noveltyDistance (const PresetValues& a, const PresetValues& b,
                                        const std::array<bool, 6>& locked)
{
    float sum = 0.0f;
    int n = 0;
    for (const auto& s : specs())
    {
        if (! s.randomize || locked[(size_t) moduleIndex (s.module)]) continue;
        const size_t i = (size_t) idx (s.id);
        if (s.kind == Kind::Float)
            sum += std::abs (toNorm (s.id, a[i]) - toNorm (s.id, b[i]));
        else
            sum += std::lround (a[i]) != std::lround (b[i]) ? 1.0f : 0.0f;
        ++n;
    }
    return n > 0 ? sum / (float) n : 0.0f;
}

float SmartRandomizer::energyOf (const PresetValues& v)
{
    float e = 0.0f;
    for (const auto& s : specs())
    {
        if (s.risk <= 0.0f || ! moduleIsOn (v, s.module)) continue;
        if (s.id == P::MdDepth && std::lround (v[(size_t) idx (P::MdTarget)]) == 0) continue;
        const float n = toNorm (s.id, v[(size_t) idx (s.id)]);
        e += s.risk * (s.riskInverted ? 1.0f - n : n);
    }
    return e;
}
} // namespace drift
