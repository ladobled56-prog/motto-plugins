#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "dsp/HostClock.h"
#include "dsp/Granular.h"
#include "dsp/Character.h"
#include "dsp/Shaper.h"
#include "dsp/Space.h"
#include "dsp/Limiter.h"
#include "random/SmartRandomizer.h"

class DriftboxAudioProcessor : public juce::AudioProcessor
{
public:
    DriftboxAudioProcessor();
    ~DriftboxAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Randomizer (llamar SOLO desde el hilo de mensajes)
    void randomize();
    bool historyBack();
    bool historyForward();
    bool canGoBack() const noexcept    { return historyPos > 0; }
    bool canGoForward() const noexcept { return historyPos + 1 < (int) presetHistory.size(); }
    juce::String getPresetLabel() const { return presetLabel; }

    drift::PresetValues currentValues() const;
    void applyValues (const drift::PresetValues& v);

    juce::AudioProcessorValueTreeState apvts;
    drift::SmartRandomizer randomizer;

    // Datos para la GUI (lectura relajada desde el hilo de mensajes)
    std::atomic<int>   uiStep { -1 };
    std::atomic<float> uiOutLevel { 0.0f };
    std::atomic<float> uiInLevel { 0.0f };
    std::atomic<float> uiGainReduction { 0.0f };
    std::atomic<double> uiBpm { 120.0 };
    const drift::Limiter& getLimiter() const noexcept { return limiter; }

private:
    float get (drift::P p) const noexcept { return raw[(size_t) drift::idx (p)]->load (std::memory_order_relaxed); }
    float modulated (drift::P p, float offset) const noexcept;
    void processChunk (float* L, float* R, int n, double ppqStart, const drift::TransportState& t);

    std::array<std::atomic<float>*, drift::kNumParams> raw {};
    std::array<juce::RangedAudioParameter*, drift::kNumParams> params {};
    std::vector<juce::NormalisableRange<float>> paramRanges;

    // DSP
    drift::HostClock clock;
    drift::Granular  granular;
    drift::Character character;
    drift::Shaper    shaper;
    drift::Space     space;
    drift::Limiter limiter;

    static constexpr int kChunk = 64;
    double sr = 44100.0;
    int latency = 0;

    std::vector<float> dryDelayL, dryDelayR;
    int dryMask = 0, dryWrite = 0;
    std::vector<float> chunkDryL, chunkDryR;

    juce::SmoothedValue<float> inGainS, outGainS, mixS, autoGainS;
    float envIn = 0.0f, envAtt = 0.0f, envRel = 0.0f;
    float rmsIn = 0.0f, rmsOut = 0.0f, rmsCoeff = 0.0f;
    float dcXL = 0, dcYL = 0, dcXR = 0, dcYR = 0, dcCoeff = 0.999f;
    float lfoSH = 0.0f; int64_t lfoCycle = INT64_MIN;

    // Randomizer / historial (hilo de mensajes)
    struct HistoryEntry { drift::PresetValues values; juce::String label; };
    std::vector<HistoryEntry> presetHistory;
    int historyPos = -1;
    static constexpr int kMaxHistory = 64;
    juce::String presetLabel { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftboxAudioProcessor)
};
