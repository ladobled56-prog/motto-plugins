#include "Params.h"
#include "dsp/Divisions.h"

namespace drift
{
juce::NormalisableRange<float> ParamSpec::range() const
{
    if (kind == Kind::Choice || kind == Kind::Int || kind == Kind::Bool)
        return { minV, maxV, 1.0f };

    juce::NormalisableRange<float> r { minV, maxV };
    if (centre > 0.0f)
        r.setSkewForCentre (centre);
    return r;
}

juce::StringArray modTargetNames()
{
    return { "Off", "Grain Size", "Grain Spray", "Grain Density", "Grain Position",
             "Drive", "Tone", "Crush", "Shaper Filter", "Space Mix" };
}

namespace
{
struct Builder
{
    std::vector<ParamSpec> v;

    ParamSpec& add (P id, const char* key, const char* name, Module m, Kind k,
                    float mn, float mx, float def, float centre = 0.0f, const char* unit = "")
    {
        jassert ((int) v.size() == idx (id)); // la tabla debe seguir el orden del enum
        ParamSpec s;
        s.id = id; s.key = key; s.name = name; s.module = m; s.kind = k;
        s.minV = mn; s.maxV = mx; s.defV = def; s.centre = centre; s.unit = unit;
        s.randomize = false; s.rndLo = mn; s.rndHi = mx; s.risk = 0.0f; s.riskInverted = false;
        v.push_back (s);
        return v.back();
    }

    ParamSpec& flt (P id, const char* key, const char* name, Module m, float mn, float mx, float def,
                    float centre = 0.0f, const char* unit = "")
    { return add (id, key, name, m, Kind::Float, mn, mx, def, centre, unit); }

    ParamSpec& bln (P id, const char* key, const char* name, Module m, bool def)
    { return add (id, key, name, m, Kind::Bool, 0.0f, 1.0f, def ? 1.0f : 0.0f); }

    ParamSpec& chc (P id, const char* key, const char* name, Module m, juce::StringArray choices, int def)
    {
        auto& s = add (id, key, name, m, Kind::Choice, 0.0f, (float) (choices.size() - 1), (float) def);
        s.choices = std::move (choices);
        return s;
    }

    ParamSpec& itg (P id, const char* key, const char* name, Module m, int mn, int mx, int def)
    { return add (id, key, name, m, Kind::Int, (float) mn, (float) mx, (float) def); }
};

ParamSpec& rnd (ParamSpec& s, float lo, float hi, float risk = 0.0f, bool inverted = false)
{
    s.randomize = true; s.rndLo = lo; s.rndHi = hi; s.risk = risk; s.riskInverted = inverted;
    return s;
}

ParamSpec& rndOn (ParamSpec& s) { s.randomize = true; return s; } // lógica especial en el randomizer

std::vector<ParamSpec> buildSpecs()
{
    Builder b;
    using M = Module;
    const auto divs = divisionNames();

    // ---------------- Global
    rnd (b.flt (P::Mix,        "gl_mix",       "Mix",        M::Global, 0.f, 1.f, 0.6f), 0.45f, 0.9f);
    b.flt (P::InputGain,  "gl_input",     "Input",      M::Global, -24.f, 12.f, 0.f, 0.f, "dB");
    b.flt (P::OutputGain, "gl_output",    "Output",     M::Global, -24.f, 12.f, 0.f, 0.f, "dB");
    b.bln (P::AutoGain,   "gl_autogain",  "Auto Gain",  M::Global, true);
    b.flt (P::Variation,  "gl_variation", "Variation",  M::Global, 0.f, 1.f, 1.f);
    b.bln (P::LockGr,     "lk_gr",        "Lock Granular",  M::Global, false);
    b.bln (P::LockCh,     "lk_ch",        "Lock Character", M::Global, false);
    b.bln (P::LockSh,     "lk_sh",        "Lock Shaper",    M::Global, false);
    b.bln (P::LockSp,     "lk_sp",        "Lock Space",     M::Global, false);
    b.bln (P::LockMd,     "lk_md",        "Lock Mod",       M::Global, false);
    b.flt (P::Tempo,      "gl_tempo",     "Tempo",          M::Global, 40.f, 240.f, 120.f, 0.f, "BPM"); // sólo si el host no da BPM (Standalone)

    // ---------------- Granular
    rndOn (b.bln (P::GrOn, "gr_on", "Granular On", M::Granular, true));
    rnd (b.flt (P::GrMix,      "gr_mix",      "Grain Mix",   M::Granular, 0.f, 1.f, 0.5f), 0.3f, 0.85f);
    rndOn (b.bln (P::GrSync,   "gr_sync",     "Grain Sync",  M::Granular, true));
    rndOn (b.chc (P::GrDiv,    "gr_div",      "Grain Rate",  M::Granular, divs, 4));
    rnd (b.flt (P::GrDensity,  "gr_density",  "Density",     M::Granular, 1.f, 60.f, 12.f, 10.f, "Hz"), 4.f, 40.f, 0.1f);
    rnd (b.flt (P::GrSize,     "gr_size",     "Grain Size",  M::Granular, 10.f, 500.f, 120.f, 90.f, "ms"), 40.f, 400.f);
    rndOn (b.chc (P::GrPitch,  "gr_pitch",    "Pitch",       M::Granular,
                  { "-12", "-7", "-5", "0", "+5", "+7", "+12", "+19", "+24" }, 3));
    rnd (b.flt (P::GrDetune,   "gr_detune",   "Detune",      M::Granular, 0.f, 50.f, 6.f, 0.f, "ct"), 0.f, 25.f, 0.1f);
    rnd (b.flt (P::GrPosition, "gr_position", "Position",    M::Granular, 0.f, 1.f, 0.2f), 0.f, 0.7f);
    rnd (b.flt (P::GrSpray,    "gr_spray",    "Spray",       M::Granular, 0.f, 1.f, 0.3f), 0.f, 0.8f, 0.15f);
    rnd (b.flt (P::GrReverse,  "gr_reverse",  "Reverse",     M::Granular, 0.f, 1.f, 0.2f), 0.f, 0.6f);
    rnd (b.flt (P::GrSpread,   "gr_spread",   "Spread",      M::Granular, 0.f, 1.f, 0.6f), 0.2f, 1.f);
    rnd (b.flt (P::GrFeedback, "gr_feedback", "Feedback",    M::Granular, 0.f, 0.85f, 0.2f), 0.f, 0.7f, 1.0f);
    rndOn (b.chc (P::GrWindow, "gr_window",   "Window",      M::Granular, { "Smooth", "Tukey", "Perc" }, 0));
    b.bln (P::GrFreeze,        "gr_freeze",   "Freeze",      M::Granular, false);

    // ---------------- Character / Lo-fi
    rndOn (b.bln (P::ChOn, "ch_on", "Character On", M::Character, true));
    rnd (b.flt (P::ChWow,        "ch_wow",        "Wow",        M::Character, 0.f, 1.f, 0.15f), 0.f, 0.6f, 0.5f);
    rnd (b.flt (P::ChFlutter,    "ch_flutter",    "Flutter",    M::Character, 0.f, 1.f, 0.1f), 0.f, 0.5f, 0.4f);
    rnd (b.flt (P::ChDrive,      "ch_drive",      "Drive",      M::Character, 0.f, 1.f, 0.25f), 0.f, 0.8f, 0.8f);
    rnd (b.flt (P::ChBits,       "ch_bits",       "Bits",       M::Character, 4.f, 16.f, 16.f), 6.f, 16.f, 0.9f, true);
    rnd (b.flt (P::ChDownsample, "ch_downsample", "Downsample", M::Character, 1.f, 24.f, 1.f, 4.f, "x"), 1.f, 12.f, 0.7f);
    rnd (b.flt (P::ChTone,       "ch_tone",       "Tone",       M::Character, 800.f, 20000.f, 16000.f, 5000.f, "Hz"), 2500.f, 20000.f, 0.2f, true);
    rnd (b.flt (P::ChNoise,      "ch_noise",      "Noise",      M::Character, -80.f, -20.f, -60.f, 0.f, "dB"), -75.f, -36.f, 0.6f);
    rndOn (b.chc (P::ChNoiseType, "ch_noisetype", "Noise Type", M::Character, { "Hiss", "Vinyl" }, 1));

    // ---------------- Shaper rítmico
    rndOn (b.bln (P::ShOn, "sh_on", "Shaper On", M::Shaper, true));
    rndOn (b.chc (P::ShDiv,    "sh_div",    "Step Rate", M::Shaper, divs, 4));
    rndOn (b.itg (P::ShSteps,  "sh_steps",  "Steps",     M::Shaper, 2, 16, 16));
    rndOn (b.itg (P::ShHits,   "sh_hits",   "Hits",      M::Shaper, 1, 16, 11));
    rndOn (b.itg (P::ShRotate, "sh_rotate", "Rotate",    M::Shaper, 0, 15, 0));
    rnd (b.flt (P::ShDepth,     "sh_depth",     "Depth",       M::Shaper, 0.f, 1.f, 0.5f), 0.2f, 1.f, 0.6f);
    rnd (b.flt (P::ShCurve,     "sh_curve",     "Decay",       M::Shaper, 0.f, 1.f, 0.3f), 0.f, 1.f);
    rnd (b.flt (P::ShSmooth,    "sh_smooth",    "Smooth",      M::Shaper, 0.5f, 50.f, 3.f, 5.f, "ms"), 1.f, 20.f);
    rnd (b.flt (P::ShFiltDepth, "sh_filtdepth", "Filter Mod",  M::Shaper, 0.f, 1.f, 0.f), 0.f, 0.8f, 0.2f);
    rnd (b.flt (P::ShFiltFreq,  "sh_filtfreq",  "Cutoff",      M::Shaper, 200.f, 20000.f, 20000.f, 2000.f, "Hz"), 1500.f, 16000.f);
    rnd (b.flt (P::ShFiltRes,   "sh_filtres",   "Resonance",   M::Shaper, 0.f, 0.9f, 0.1f), 0.f, 0.6f, 0.8f);
    rnd (b.flt (P::ShPanDepth,  "sh_pandepth",  "Auto Pan",    M::Shaper, 0.f, 1.f, 0.f), 0.f, 0.7f);
    rndOn (b.chc (P::ShPanDiv,  "sh_pandiv",    "Pan Rate",    M::Shaper, divs, 2));

    // ---------------- Modulación
    rndOn (b.chc (P::MdDiv,       "md_div",       "LFO Rate",   M::Mod, divs, 0));
    rndOn (b.chc (P::MdShape,     "md_shape",     "LFO Shape",  M::Mod,
                  { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H" }, 0));
    rndOn (b.chc (P::MdTarget,    "md_target",    "LFO Target", M::Mod, modTargetNames(), 0));
    rnd (b.flt (P::MdDepth,       "md_depth",     "LFO Depth",  M::Mod, 0.f, 1.f, 0.3f), 0.1f, 0.6f, 0.2f);
    rndOn (b.chc (P::MdEnvTarget, "md_envtarget", "Env Target", M::Mod, modTargetNames(), 0));
    rnd (b.flt (P::MdEnvAmount,   "md_envamount", "Env Amount", M::Mod, -1.f, 1.f, 0.f), -0.6f, 0.6f);

    // ---------------- Space
    rndOn (b.bln (P::SpOn, "sp_on", "Space On", M::Space, true));
    rndOn (b.chc (P::SpDelayDiv, "sp_delaydiv", "Delay Time", M::Space, divs, 11));
    rnd (b.flt (P::SpDelayFb,  "sp_delayfb",  "Delay FB",   M::Space, 0.f, 0.8f, 0.35f), 0.1f, 0.65f, 0.8f);
    rnd (b.flt (P::SpDelayMix, "sp_delaymix", "Delay Mix",  M::Space, 0.f, 1.f, 0.2f), 0.f, 0.45f, 0.3f);
    rnd (b.flt (P::SpRevSize,  "sp_revsize",  "Room Size",  M::Space, 0.f, 1.f, 0.5f), 0.2f, 0.9f, 0.3f);
    rnd (b.flt (P::SpRevDamp,  "sp_revdamp",  "Damping",    M::Space, 0.f, 1.f, 0.5f), 0.2f, 0.8f);
    rnd (b.flt (P::SpRevMix,   "sp_revmix",   "Reverb Mix", M::Space, 0.f, 1.f, 0.2f), 0.05f, 0.45f, 0.5f);

    jassert ((int) b.v.size() == kNumParams);
    return b.v;
}
} // namespace

const std::vector<ParamSpec>& specs()
{
    static const std::vector<ParamSpec> table = buildSpecs();
    return table;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    struct GroupInfo { Module m; const char* id; const char* name; };
    const GroupInfo groups[] = {
        { Module::Global,    "global",    "Global" },
        { Module::Granular,  "granular",  "Granular" },
        { Module::Character, "character", "Character" },
        { Module::Shaper,    "shaper",    "Shaper" },
        { Module::Mod,       "mod",       "Modulation" },
        { Module::Space,     "space",     "Space" },
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& g : groups)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (g.id, g.name, "|");

        for (const auto& s : specs())
        {
            if (s.module != g.m)
                continue;

            const juce::ParameterID pid { s.key, 1 };

            switch (s.kind)
            {
                case Kind::Float:
                {
                    // Decimales según la escala del parámetro (120 BPM, 4.5 kHz -> sin decimales; 0.35 -> 2)
                    const float span = s.maxV - s.minV;
                    const int decimals = span >= 100.0f ? 0 : span >= 10.0f ? 1 : 2;
                    group->addChild (std::make_unique<juce::AudioParameterFloat> (
                        pid, s.name, s.range(), s.defV,
                        juce::AudioParameterFloatAttributes()
                            .withLabel (s.unit)
                            .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); })));
                }
                    break;
                case Kind::Bool:
                    group->addChild (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.defV > 0.5f));
                    break;
                case Kind::Choice:
                    group->addChild (std::make_unique<juce::AudioParameterChoice> (pid, s.name, s.choices, (int) s.defV));
                    break;
                case Kind::Int:
                    group->addChild (std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.minV, (int) s.maxV, (int) s.defV));
                    break;
            }
        }

        layout.add (std::move (group));
    }

    return layout;
}
} // namespace drift
