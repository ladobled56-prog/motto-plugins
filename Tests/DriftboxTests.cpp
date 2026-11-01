// Pruebas automáticas de Driftbox: se ejecutan en CI (macOS / Windows) y localmente.
//   1) Randomizer: rangos, reglas musicales, presupuesto de energía, novedad, tempo, locks, determinismo
//   2) DSP offline: sin NaN, sin clipping, nivel controlado, silencio tras silencio, sync con transporte
#include "PluginProcessor.h"
#include <chrono>
#include <cstdio>

using namespace drift;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: " __VA_ARGS__); std::printf ("  [%s:%d]\n", __FILE__, __LINE__); } } while (0)

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static float get (const PresetValues& v, P p) { return v[(size_t) idx (p)]; }

static double divMs (const PresetValues& v, P p, double bpm) { return divisionAt (get (v, p)).beats * 60000.0 / bpm; }

//==============================================================================
static void testRandomizer()
{
    std::printf ("[1] Randomizer\n");
    SmartRandomizer rnd;
    const std::array<bool, 6> noLocks {};
    int presses = 0, sameArch = 0, lowNovelty = 0, overBudget = 0;
    float minNovelty = 1.0f, maxEnergy = 0.0f;

    for (double bpm : { 70.0, 90.0, 120.0, 140.0, 174.0, 200.0 })
    {
        PresetValues cur {};
        for (const auto& s : specs()) cur[(size_t) idx (s.id)] = s.defV;
        int prevArch = -1;

        for (int k = 0; k < 400; ++k, ++presses)
        {
            const auto g = rnd.generate (cur, bpm, 1.0f, noLocks);
            const auto& v = g.values;

            for (const auto& s : specs())
            {
                const float x = v[(size_t) idx (s.id)];
                CHECK (std::isfinite (x) && x >= s.minV - 1e-4f && x <= s.maxV + 1e-4f,
                       "%s fuera de rango: %f", s.key.toRawUTF8(), (double) x);
                if (s.kind != Kind::Float)
                    CHECK (std::abs (x - std::round (x)) < 1e-4f, "%s no entero: %f", s.key.toRawUTF8(), (double) x);
            }

            // Reglas musicales
            const int onCount = (get (v, P::GrOn) > 0.5f) + (get (v, P::ChOn) > 0.5f) + (get (v, P::ShOn) > 0.5f) + (get (v, P::SpOn) > 0.5f);
            CHECK (onCount >= 2, "menos de 2 módulos activos");
            CHECK (get (v, P::ShHits) >= 1 && get (v, P::ShHits) <= get (v, P::ShSteps), "hits/steps inválidos");
            if (get (v, P::ShOn) > 0.5f)
                CHECK (divMs (v, P::ShDiv, bpm) >= SmartRandomizer::kMinShaperPeriodMs - 1e-6,
                       "shaper demasiado rápido %.1f ms @%.0f BPM", divMs (v, P::ShDiv, bpm), bpm);
            if (get (v, P::GrSync) > 0.5f)
                CHECK (divMs (v, P::GrDiv, bpm) >= SmartRandomizer::kMinGrainPeriodMs - 1e-6,
                       "granos demasiado rápidos %.1f ms @%.0f BPM", divMs (v, P::GrDiv, bpm), bpm);
            if (get (v, P::ChBits) < 8.0f)
                CHECK (get (v, P::ChNoise) <= -45.0f + 1e-3f && get (v, P::ChDrive) <= 0.5f + 1e-4f, "regla bits<8 violada");
            if (get (v, P::SpDelayFb) > 0.5f)
                CHECK (get (v, P::SpDelayMix) <= 0.35f + 1e-4f, "regla delay fb violada");
            const float semis = kPitchSemis[(size_t) std::lround (get (v, P::GrPitch))];
            if (semis >= 19.0f) CHECK (get (v, P::GrSize) >= 60.0f - 1e-3f, "regla pitch alto / grano corto violada");
            if (std::abs (semis) >= 12.0f) CHECK (get (v, P::GrFeedback) <= 0.55f + 1e-4f, "feedback alto con pitch");
            CHECK (get (v, P::GrFreeze) < 0.5f, "freeze no debe aleatorizarse");

            // Energía
            const float e = SmartRandomizer::energyOf (v);
            maxEnergy = std::max (maxEnergy, e);
            if (e > rnd.energyBudgetOf (g.archetype) + 0.02f) ++overBudget;

            // Novedad y carácter
            const float nov = SmartRandomizer::noveltyDistance (v, cur, noLocks);
            minNovelty = std::min (minNovelty, nov);
            if (nov < SmartRandomizer::kMinNovelty) ++lowNovelty;
            if (g.archetype == prevArch) ++sameArch;
            prevArch = g.archetype;

            cur = v;
        }
    }

    std::printf ("    %d pulsaciones | novedad mínima %.3f | energía máx %.2f | arquetipo repetido %.1f%%\n",
                 presses, (double) minNovelty, (double) maxEnergy, 100.0 * sameArch / presses);
    CHECK (overBudget == 0, "%d presets sobre el presupuesto de energía", overBudget);
    CHECK (lowNovelty == 0, "%d presets demasiado parecidos al anterior", lowNovelty);
    CHECK (sameArch < presses / 8, "el arquetipo se repite demasiado (%d)", sameArch);

    // Locks: con Granular bloqueado, sus parámetros no cambian
    {
        PresetValues cur {};
        for (const auto& s : specs()) cur[(size_t) idx (s.id)] = s.defV;
        std::array<bool, 6> locks {};
        locks[(size_t) Module::Granular] = true;
        for (int k = 0; k < 100; ++k)
        {
            const auto g = rnd.generate (cur, 120.0, 1.0f, locks);
            for (const auto& s : specs())
                if (s.module == Module::Granular)
                    CHECK (g.values[(size_t) idx (s.id)] == cur[(size_t) idx (s.id)], "lock granular violado en %s", s.key.toRawUTF8());
            cur = g.values;
        }
    }

    // Variación baja => cambios pequeños
    {
        PresetValues cur {};
        for (const auto& s : specs()) cur[(size_t) idx (s.id)] = s.defV;
        float sum = 0.0f;
        for (int k = 0; k < 50; ++k)
            sum += SmartRandomizer::noveltyDistance (rnd.generate (cur, 120.0, 0.15f, noLocks).values, cur, noLocks);
        std::printf ("    variación 0.15 -> distancia media %.3f\n", (double) (sum / 50.0f));
        CHECK (sum / 50.0f < 0.15f, "variación baja produce cambios demasiado grandes");
    }

    // Determinismo por semilla
    {
        PresetValues cur {};
        for (const auto& s : specs()) cur[(size_t) idx (s.id)] = s.defV;
        const auto a = rnd.generateFromSeed (0xC0FFEEull, cur, 128.0, 1.0f, noLocks, 2);
        const auto b = rnd.generateFromSeed (0xC0FFEEull, cur, 128.0, 1.0f, noLocks, 2);
        CHECK (a.values == b.values, "la misma semilla no reproduce el mismo preset");
    }
}

//==============================================================================
struct RenderStats { float peak = 0, rmsIn = 0, rmsOut = 0, tailPeak = 0; bool finite = true; };

static RenderStats render (DriftboxAudioProcessor& proc, FakePlayHead& ph, double sr, int block, double seconds,
                           double silenceSeconds, uint32_t seed)
{
    RenderStats st;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    juce::Random r ((juce::int64) seed);
    const int total = (int) (seconds * sr), silent = (int) (silenceSeconds * sr);
    double inSq = 0, outSq = 0; long cnt = 0;
    double phaseK = 0.0;

    for (int pos = 0; pos < total + silent; pos += block)
    {
        const int n = std::min (block, total + silent - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            if (pos + i < total)
            {
                // Señal de prueba: bombo en cada negra + hi-hat en corcheas + acorde
                const double t = (pos + i) / sr;
                const double beat = t * ph.bpm / 60.0;
                const double fb = beat - std::floor (beat);
                const double f8 = beat * 2.0 - std::floor (beat * 2.0);
                phaseK += (50.0 + 120.0 * std::exp (-fb * 40.0)) / sr;
                x = 0.5f * (float) (std::sin (juce::MathConstants<double>::twoPi * phaseK) * std::exp (-fb * 8.0))
                  + 0.08f * (r.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-f8 * 30.0)
                  + 0.08f * (float) (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                                   + std::sin (juce::MathConstants<double>::twoPi * 277.18 * t)
                                   + std::sin (juce::MathConstants<double>::twoPi * 329.63 * t));
            }
            buf.setSample (0, i, x);
            buf.setSample (1, i, x * 0.9f);
        }

        const bool measure = pos >= total / 2 && pos + n <= total;
        if (measure) for (int i = 0; i < n; ++i) inSq += (double) buf.getSample (0, i) * buf.getSample (0, i);

        proc.processBlock (buf, midi);
        ph.ppq += n * ph.bpm / 60.0 / sr;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float y = buf.getSample (ch, i);
                if (! std::isfinite (y)) st.finite = false;
                st.peak = std::max (st.peak, std::abs (y));
                if (pos >= total + silent - (int) sr) st.tailPeak = std::max (st.tailPeak, std::abs (y));
            }
        if (measure) { for (int i = 0; i < n; ++i) outSq += (double) buf.getSample (0, i) * buf.getSample (0, i); cnt += n; }
    }
    st.rmsIn = (float) std::sqrt (inSq / std::max (1L, cnt));
    st.rmsOut = (float) std::sqrt (outSq / std::max (1L, cnt));
    return st;
}

static void testDsp()
{
    std::printf ("[2] DSP offline\n");
    DriftboxAudioProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);

    const double sr = 48000.0;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    const int latency = proc.getLatencySamples();
    std::printf ("    latencia reportada: %d muestras\n", latency);
    CHECK (latency > 0 && latency < 1024, "latencia fuera de lo esperado");

    // Preset por defecto
    {
        const auto st = render (proc, ph, sr, 512, 3.0, 0.0, 1);
        CHECK (st.finite && st.peak <= 0.945f, "default: no finito o clipping (%.3f)", (double) st.peak);
    }

    int n = 0;
    float worstPeak = 0.0f, minRatioDb = 100.0f, maxRatioDb = -100.0f, worstTailDb = -200.0f;
    const auto t0 = std::chrono::steady_clock::now();
    double audioSeconds = 0.0;

    for (int k = 0; k < 40; ++k, ++n)
    {
        ph.bpm = 80.0 + (k * 37) % 100;
        ph.ppq = 0.0;
        proc.randomize();
        const int block = (k % 3 == 0) ? 333 : (k % 3 == 1 ? 512 : 64);
        const auto st = render (proc, ph, sr, block, 3.0, 9.0, (uint32_t) k);
        audioSeconds += 12.0;

        CHECK (st.finite, "preset %d (%s): NaN/Inf", k, proc.getPresetLabel().toRawUTF8());
        CHECK (st.peak <= 0.945f, "preset %d (%s): clipping %.3f", k, proc.getPresetLabel().toRawUTF8(), (double) st.peak);
        worstPeak = std::max (worstPeak, st.peak);

        if (st.rmsIn > 0)
        {
            const float ratioDb = juce::Decibels::gainToDecibels (st.rmsOut / st.rmsIn, -100.0f);
            minRatioDb = std::min (minRatioDb, ratioDb);
            maxRatioDb = std::max (maxRatioDb, ratioDb);
            CHECK (ratioDb > -14.0f && ratioDb < 8.0f, "preset %d (%s): nivel %.1f dB respecto a la entrada",
                   k, proc.getPresetLabel().toRawUTF8(), (double) ratioDb);
        }
        const float tailDb = juce::Decibels::gainToDecibels (st.tailPeak, -200.0f);
        worstTailDb = std::max (worstTailDb, tailDb);
        CHECK (tailDb < -40.0f, "preset %d (%s): no vuelve al silencio (%.1f dBFS)", k, proc.getPresetLabel().toRawUTF8(), (double) tailDb);
        CHECK (proc.getLatencySamples() == latency, "la latencia cambió");
    }

    const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("    %d presets | pico máx %.3f | nivel salida/entrada %.1f..%.1f dB | cola peor %.1f dBFS | %.1fx tiempo real\n",
                 n, (double) worstPeak, (double) minRatioDb, (double) maxRatioDb, (double) worstTailDb, audioSeconds / cpu);

    {
        const auto& lim = proc.getLimiter();
        const double clipRatio = (double) lim.hardClips / (double) std::max<uint64_t> (1, lim.samplesProcessed);
        std::printf ("    limitador: recorte duro en %.4f%% de las muestras\n", 100.0 * clipRatio);
        CHECK (clipRatio < 0.001, "el limitador recorta demasiado (%.4f%%)", 100.0 * clipRatio);
    }

    // Historial atrás/adelante restaura exactamente
    {
        proc.randomize();
        const auto a = proc.currentValues();
        proc.randomize();
        CHECK (proc.historyBack(), "historial atrás no disponible");
        const auto b = proc.currentValues();
        bool same = true;
        for (const auto& s : specs()) if (s.randomize && std::abs (a[(size_t) idx (s.id)] - b[(size_t) idx (s.id)]) > 1e-3f) same = false;
        CHECK (same, "historial atrás no restaura el preset");
        CHECK (proc.historyForward(), "historial adelante no disponible");
    }

    // Transporte: salto de posición y transporte detenido no rompen nada
    {
        ph.playing = true; ph.ppq = 37.25;
        auto st = render (proc, ph, sr, 512, 1.0, 0.0, 99);
        ph.ppq = 3.0;  // salto (loop del DAW)
        st = render (proc, ph, sr, 512, 1.0, 0.0, 98);
        CHECK (st.finite && st.peak <= 1.0f, "salto de transporte rompe el audio");
        ph.playing = false;
        st = render (proc, ph, sr, 512, 1.0, 0.0, 97);
        CHECK (st.finite && st.peak <= 1.0f, "transporte detenido rompe el audio");
    }

    // Estado: guardar y restaurar
    {
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        const auto before = proc.currentValues();
        proc.randomize();
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto after = proc.currentValues();
        bool same = true;
        for (int i = 0; i < kNumParams; ++i) if (std::abs (before[(size_t) i] - after[(size_t) i]) > 1e-3f) same = false;
        CHECK (same, "getState/setState no restaura los parámetros");
    }
}

//==============================================================================
static void testSync()
{
    std::printf ("[3] Sincronía\n");
    CHECK (std::abs (phaseFor (1.5, 1.0) - 0.5) < 1e-9, "phaseFor");
    CHECK (std::abs (phaseFor (-0.25, 1.0) - 0.75) < 1e-9, "phaseFor negativo");
    CHECK (euclidMask (16, 4, 0) == 0x1111u, "euclid 4/16 = 0x%x", euclidMask (16, 4, 0));
    CHECK (euclidMask (8, 3, 0) == 0x25u || euclidMask (8, 3, 0) == 0x49u, "euclid 3/8 = 0x%x", euclidMask (8, 3, 0));
    CHECK (std::abs (kDivisions[8].beats * 3.0 - 1.0) < 1e-12, "1/8T");

    HostClock c;
    c.prepare (48000.0);
    FakePlayHead ph; ph.bpm = 120.0; ph.ppq = 4.0;
    auto t = c.update (&ph, 480);
    CHECK (std::abs (t.ppqAtBlockStart - 4.0) < 1e-9, "ppq del host");
    CHECK (std::abs (t.ppqPerSample - 120.0 / 60.0 / 48000.0) < 1e-12, "ppq por muestra");
    ph.ppq += 480 * t.ppqPerSample;
    t = c.update (&ph, 480);
    CHECK (! t.jumped, "falso salto");
    ph.ppq = 0.0;
    t = c.update (&ph, 480);
    CHECK (t.jumped, "salto no detectado");
    ph.playing = false;
    const auto t2 = c.update (&ph, 480);
    CHECK (std::abs (t2.ppqAtBlockStart - (0.0 + 480 * t.ppqPerSample)) < 1e-9, "reloj libre no continúa");
}

static int screenshot (const juce::File& out)
{
    DriftboxAudioProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.randomize();
    render (proc, ph, 48000.0, 512, 1.3, 0.0, 5);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("screenshot: %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String (argv[1]) == "--screenshot")
        return screenshot (juce::File (juce::String (argv[2])));

    testSync();
    testRandomizer();
    testDsp();
    std::printf (failures == 0 ? "\nTODAS LAS PRUEBAS PASARON\n" : "\n%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
