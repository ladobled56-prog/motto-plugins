#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace drift;

DriftboxAudioProcessor::DriftboxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DriftboxState", createLayout())
{
    for (const auto& s : specs())
    {
        const auto i = (size_t) idx (s.id);
        raw[i] = apvts.getRawParameterValue (s.key);
        params[i] = apvts.getParameter (s.key);
        jassert (raw[i] != nullptr && params[i] != nullptr);
        paramRanges.push_back (s.range());
    }
}

bool DriftboxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

//==============================================================================
void DriftboxAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    clock.prepare (sr);
    granular.prepare (sr);
    character.prepare (sr, kChunk);
    shaper.prepare (sr, kChunk);
    space.prepare (sr, kChunk);

    latency = character.getLatency();                    // rama dry se alinea a esto
    setLatencySamples (latency + drift::Limiter::kLookahead);

    const int dsize = juce::nextPowerOfTwo (latency + kChunk + 16);
    dryDelayL.assign ((size_t) dsize, 0.0f);
    dryDelayR.assign ((size_t) dsize, 0.0f);
    dryMask = dsize - 1;
    dryWrite = 0;
    chunkDryL.assign ((size_t) kChunk, 0.0f);
    chunkDryR.assign ((size_t) kChunk, 0.0f);

    limiter.prepare (sr, -0.5f);

    inGainS.reset (sr, 0.05);  inGainS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get (P::InputGain)));
    outGainS.reset (sr, 0.05); outGainS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get (P::OutputGain)));
    mixS.reset (sr, 0.05);     mixS.setCurrentAndTargetValue (get (P::Mix));
    autoGainS.reset (sr, 0.3); autoGainS.setCurrentAndTargetValue (1.0f);

    envAtt = std::exp (-1.0f / (0.005f * (float) sr));
    envRel = std::exp (-1.0f / (0.12f * (float) sr));
    rmsCoeff = std::exp (-1.0f / (0.4f * (float) sr));
    dcCoeff = 1.0f - juce::MathConstants<float>::twoPi * 10.0f / (float) sr;
    envIn = rmsIn = rmsOut = 0.0f;
    dcXL = dcYL = dcXR = dcYR = 0.0f;
    lfoCycle = INT64_MIN;
}

float DriftboxAudioProcessor::modulated (P p, float offset) const noexcept
{
    const float base = get (p);
    if (std::abs (offset) < 1.0e-9f) return base;
    const auto& r = paramRanges[(size_t) idx (p)];
    return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, r.convertTo0to1 (base) + offset));
}

//==============================================================================
void DriftboxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2)
        return;

    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    const auto t = clock.update (getPlayHead(), n, get (P::Tempo));
    uiBpm.store (t.bpm, std::memory_order_relaxed);
    if (t.jumped)
    {
        granular.onTransportJump();
        lfoCycle = INT64_MIN;
    }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    for (int start = 0; start < n; start += kChunk)
    {
        const int len = juce::jmin (kChunk, n - start);
        processChunk (L + start, R + start, len, t.ppqAtBlockStart + start * t.ppqPerSample, t);
    }

    uiStep.store (shaper.currentStep.load (std::memory_order_relaxed), std::memory_order_relaxed);
}

void DriftboxAudioProcessor::processChunk (float* L, float* R, int n, double ppqStart, const TransportState& t)
{
    // ---- Entrada: ganancia, rama dry (alineada a la latencia), envolvente, RMS de referencia
    inGainS.setTargetValue (juce::Decibels::decibelsToGain (get (P::InputGain)));
    float peakIn = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainS.getNextValue();
        L[i] *= g; R[i] *= g;
        const float a = juce::jmax (std::abs (L[i]), std::abs (R[i]));
        peakIn = juce::jmax (peakIn, a);
        envIn = a > envIn ? envAtt * envIn + (1.0f - envAtt) * a : envRel * envIn + (1.0f - envRel) * a;
        rmsIn = rmsCoeff * rmsIn + (1.0f - rmsCoeff) * 0.5f * (L[i] * L[i] + R[i] * R[i]);

        dryDelayL[(size_t) dryWrite] = L[i];
        dryDelayR[(size_t) dryWrite] = R[i];
        const int rd = (dryWrite - latency) & dryMask;
        chunkDryL[(size_t) i] = dryDelayL[(size_t) rd];
        chunkDryR[(size_t) i] = dryDelayR[(size_t) rd];
        dryWrite = (dryWrite + 1) & dryMask;
    }

    // ---- Motor de modulación (tasa de control: 1 valor por chunk de 64 muestras)
    std::array<float, (size_t) ModTarget::Count> off {};
    {
        const double divB = divisionAt (get (P::MdDiv)).beats;
        const double ph = phaseFor (ppqStart, divB);
        float lfo = 0.0f;
        switch ((int) std::lround (get (P::MdShape)))
        {
            case 0:  lfo = (float) std::sin (juce::MathConstants<double>::twoPi * ph); break;
            case 1:  lfo = 1.0f - 4.0f * (float) std::abs (ph - 0.5); break;
            case 2:  lfo = 2.0f * (float) ph - 1.0f; break;
            case 3:  lfo = 1.0f - 2.0f * (float) ph; break;
            case 4:  lfo = ph < 0.5 ? 1.0f : -1.0f; break;
            default:
            {
                const auto c = cycleIndex (ppqStart, divB);
                if (c != lfoCycle) { lfoSH = hashToUnit (c, 77u) * 2.0f - 1.0f; lfoCycle = c; }
                lfo = lfoSH;
            }
        }
        const int tgt = (int) std::lround (get (P::MdTarget));
        if (tgt > 0 && tgt < (int) ModTarget::Count)
            off[(size_t) tgt] += get (P::MdDepth) * 0.5f * lfo;

        const int etgt = (int) std::lround (get (P::MdEnvTarget));
        if (etgt > 0 && etgt < (int) ModTarget::Count)
            off[(size_t) etgt] += get (P::MdEnvAmount) * juce::jmin (1.0f, envIn * 3.0f) * 0.5f;
    }
    auto mo = [&] (ModTarget m) { return off[(size_t) m]; };

    // ---- 1) Granular
    {
        Granular::Settings s;
        s.on = get (P::GrOn) > 0.5f ? 1.0f : 0.0f;
        s.mix = get (P::GrMix);
        s.sync = get (P::GrSync) > 0.5f;
        s.divBeats = divisionAt (get (P::GrDiv)).beats;
        s.densityHz = modulated (P::GrDensity, mo (ModTarget::GrainDensity));
        s.sizeMs = modulated (P::GrSize, mo (ModTarget::GrainSize));
        s.pitchSemis = kPitchSemis[(size_t) juce::jlimit (0, (int) kPitchSemis.size() - 1, (int) std::lround (get (P::GrPitch)))];
        s.detuneCents = get (P::GrDetune);
        s.position = modulated (P::GrPosition, mo (ModTarget::GrainPosition));
        s.spray = modulated (P::GrSpray, mo (ModTarget::GrainSpray));
        s.reverse = get (P::GrReverse);
        s.spread = get (P::GrSpread);
        s.feedback = get (P::GrFeedback);
        s.window = (int) std::lround (get (P::GrWindow));
        s.freeze = get (P::GrFreeze) > 0.5f;
        granular.process (L, R, n, s, ppqStart, t.ppqPerSample, t.bpm);
    }

    // ---- 2) Character / Lo-fi
    {
        Character::Settings s;
        s.on = get (P::ChOn) > 0.5f ? 1.0f : 0.0f;
        s.wow = get (P::ChWow);
        s.flutter = get (P::ChFlutter);
        s.drive = modulated (P::ChDrive, mo (ModTarget::Drive));
        s.bits = modulated (P::ChBits, -mo (ModTarget::Crush));
        s.downsample = get (P::ChDownsample);
        s.toneHz = modulated (P::ChTone, mo (ModTarget::Tone));
        s.noiseDb = get (P::ChNoise);
        s.noiseType = (int) std::lround (get (P::ChNoiseType));
        float* chans[2] = { L, R };
        character.process (juce::dsp::AudioBlock<float> (chans, 2, (size_t) n), s);
    }

    // ---- 3) Shaper rítmico
    {
        Shaper::Settings s;
        s.on = get (P::ShOn) > 0.5f ? 1.0f : 0.0f;
        s.divBeats = divisionAt (get (P::ShDiv)).beats;
        s.steps = (int) std::lround (get (P::ShSteps));
        s.hits = (int) std::lround (get (P::ShHits));
        s.rotate = (int) std::lround (get (P::ShRotate));
        s.depth = get (P::ShDepth);
        s.curve = get (P::ShCurve);
        s.smoothMs = get (P::ShSmooth);
        s.filtDepth = get (P::ShFiltDepth);
        s.filtFreq = modulated (P::ShFiltFreq, mo (ModTarget::ShaperFilter));
        s.filtRes = get (P::ShFiltRes);
        s.panDepth = get (P::ShPanDepth);
        s.panDivBeats = divisionAt (get (P::ShPanDiv)).beats;
        shaper.process (L, R, n, s, ppqStart, t.ppqPerSample);
    }

    // ---- 4) Space
    {
        Space::Settings s;
        s.on = get (P::SpOn) > 0.5f ? 1.0f : 0.0f;
        s.delaySeconds = divisionAt (get (P::SpDelayDiv)).beats * 60.0 / t.bpm;
        s.delayFb = get (P::SpDelayFb);
        s.delayMix = get (P::SpDelayMix);
        s.revSize = get (P::SpRevSize);
        s.revDamp = get (P::SpRevDamp);
        s.revMix = modulated (P::SpRevMix, mo (ModTarget::SpaceMix));
        space.process (L, R, n, s);
    }

    // ---- Salida: mezcla, auto-gain, ganancia, DC block, limitador, guardia anti-NaN
    mixS.setTargetValue (get (P::Mix));
    outGainS.setTargetValue (juce::Decibels::decibelsToGain (get (P::OutputGain)));

    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.getNextValue();
        float l = chunkDryL[(size_t) i] + (L[i] - chunkDryL[(size_t) i]) * m;
        float r = chunkDryR[(size_t) i] + (R[i] - chunkDryR[(size_t) i]) * m;
        rmsOut = rmsCoeff * rmsOut + (1.0f - rmsCoeff) * 0.5f * (l * l + r * r);

        const float g = autoGainS.getNextValue() * outGainS.getNextValue();
        l *= g; r *= g;

        dcYL = l - dcXL + dcCoeff * dcYL; dcXL = l;
        dcYR = r - dcXR + dcCoeff * dcYR; dcXR = r;
        L[i] = dcYL; R[i] = dcYR;
    }

    if (get (P::AutoGain) > 0.5f)
    {
        if (rmsIn > 1.0e-6f) // > -60 dBFS: con silencio se congela la ganancia
        {
            const float ratio = std::sqrt (rmsIn / (rmsOut + 1.0e-12f));
            autoGainS.setTargetValue (juce::jlimit (0.125f, 2.0f, ratio));
        }
    }
    else
    {
        autoGainS.setTargetValue (1.0f);
    }

    limiter.process (L, R, n);

    bool finite = true;
    float peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { finite = false; break; }
        peakOut = juce::jmax (peakOut, std::abs (L[i]), std::abs (R[i]));
    }
    if (! finite)
    {
        // Protección de parlantes: silencio y reinicio de estados
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        granular.reset(); character.reset(); shaper.reset(); space.reset(); limiter.reset();
        dcXL = dcYL = dcXR = dcYR = 0.0f;
        rmsOut = rmsIn = 0.0f;
        autoGainS.setCurrentAndTargetValue (1.0f);
        peakOut = 0.0f;
    }

    uiInLevel.store (juce::jmax (peakIn, uiInLevel.load (std::memory_order_relaxed) * 0.97f), std::memory_order_relaxed);
    uiOutLevel.store (juce::jmax (peakOut, uiOutLevel.load (std::memory_order_relaxed) * 0.97f), std::memory_order_relaxed);
    uiGainReduction.store (limiter.takeGainReductionDb(), std::memory_order_relaxed);
}

//==============================================================================
PresetValues DriftboxAudioProcessor::currentValues() const
{
    PresetValues v {};
    for (int i = 0; i < kNumParams; ++i)
        v[(size_t) i] = raw[(size_t) i]->load (std::memory_order_relaxed);
    return v;
}

void DriftboxAudioProcessor::applyValues (const PresetValues& v)
{
    for (const auto& s : specs())
    {
        if (! s.randomize) continue;
        const auto i = (size_t) idx (s.id);
        auto* p = params[i];
        const float norm = p->convertTo0to1 (v[i]);
        if (std::abs (norm - p->getValue()) < 1.0e-6f) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void DriftboxAudioProcessor::randomize()
{
    const auto cur = currentValues();
    const std::array<bool, 6> locked {
        false,
        get (P::LockGr) > 0.5f, get (P::LockCh) > 0.5f, get (P::LockSh) > 0.5f,
        get (P::LockSp) > 0.5f, get (P::LockMd) > 0.5f
    };

    const auto gen = randomizer.generate (cur, uiBpm.load (std::memory_order_relaxed), get (P::Variation), locked);

    if (historyPos < 0)
    {
        presetHistory.push_back ({ cur, presetLabel });
        historyPos = 0;
    }
    presetHistory.resize ((size_t) historyPos + 1);

    const auto& arch = randomizer.getArchetypes()[(size_t) gen.archetype];
    const juce::String label = juce::String (arch.name) + "  #" + juce::String::toHexString ((juce::int64) (gen.seed & 0xFFFFFF)).toUpperCase();
    presetHistory.push_back ({ gen.values, label });
    historyPos = (int) presetHistory.size() - 1;

    if ((int) presetHistory.size() > kMaxHistory)
    {
        presetHistory.erase (presetHistory.begin());
        --historyPos;
    }

    applyValues (gen.values);
    presetLabel = label;
}

bool DriftboxAudioProcessor::historyBack()
{
    if (! canGoBack()) return false;
    --historyPos;
    applyValues (presetHistory[(size_t) historyPos].values);
    presetLabel = presetHistory[(size_t) historyPos].label;
    return true;
}

bool DriftboxAudioProcessor::historyForward()
{
    if (! canGoForward()) return false;
    ++historyPos;
    applyValues (presetHistory[(size_t) historyPos].values);
    presetLabel = presetHistory[(size_t) historyPos].label;
    return true;
}

//==============================================================================
void DriftboxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr);
    state.setProperty ("presetLabel", presetLabel, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DriftboxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            presetLabel = tree.getProperty ("presetLabel", "Init").toString();
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* DriftboxAudioProcessor::createEditor()
{
    return new DriftboxAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DriftboxAudioProcessor();
}
