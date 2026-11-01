#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ui
{
/** Fuente compatible con JUCE 7 y JUCE 8. */
inline juce::Font font (float size, bool bold = false)
{
   #if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font (size, bold ? juce::Font::bold : juce::Font::plain);
   #endif
}

namespace colours
{
    inline const juce::Colour bg        { 0xff111116 };
    inline const juce::Colour panel     { 0xff1a1a22 };
    inline const juce::Colour panelEdge { 0xff2a2a36 };
    inline const juce::Colour text      { 0xffe8e8f0 };
    inline const juce::Colour dim       { 0xff8a8a9a };
    inline const juce::Colour granular  { 0xff3ddbd9 };
    inline const juce::Colour character { 0xfff2a541 };
    inline const juce::Colour shaper    { 0xffff5c8a };
    inline const juce::Colour space     { 0xff5c9dff };
    inline const juce::Colour mod       { 0xffa77bff };
    inline const juce::Colour global    { 0xffd8d8e8 };
}

class DriftLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DriftLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return ui::font (12.0f); }
    juce::Font getPopupMenuFont() override { return ui::font (13.0f); }
};

/** Botón mágico. */
class RandomizeButton : public juce::Button, private juce::Timer
{
public:
    RandomizeButton() : juce::Button ("Randomize") {}
    void flash() { flashAmount = 1.0f; startTimerHz (60); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    void timerCallback() override { flashAmount *= 0.88f; if (flashAmount < 0.01f) { flashAmount = 0.0f; stopTimer(); } repaint(); }
    float flashAmount = 0.0f;
};

/** Visor del patrón euclidiano del Shaper con el paso actual resaltado. */
class PatternDisplay : public juce::Component
{
public:
    explicit PatternDisplay (DriftboxAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    DriftboxAudioProcessor& proc;
};

/** Panel de módulo: controles generados automáticamente desde la tabla de parámetros. */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (DriftboxAudioProcessor&, drift::Module, const juce::String& title, juce::Colour accent,
                 drift::P onFlag, drift::P lockFlag, int columns);
    void setExtra (juce::Component* c, int width) { extra = c; extraWidth = width; if (c) addAndMakeVisible (c); }
    void paint (juce::Graphics&) override;
    void resized() override;
    int getPreferredWidth() const;

    static constexpr int kCellW = 72, kCellH = 84, kHeader = 30, kPad = 10;

private:
    struct Control
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    };

    juce::String title;
    juce::Colour accent;
    int columns;
    std::vector<std::unique_ptr<Control>> controls;
    juce::ToggleButton onButton { "ON" }, lockButton { "LOCK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, lockAtt;
    bool hasOn = false;
    juce::Component* extra = nullptr;
    int extraWidth = 0;
};
} // namespace ui

class DriftboxAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DriftboxAudioProcessorEditor (DriftboxAudioProcessor&);
    ~DriftboxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DriftboxAudioProcessor& proc;
    ui::DriftLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 700 };

    ui::RandomizeButton randomButton;
    juce::TextButton backButton { "<" }, fwdButton { ">" };
    juce::Label presetLabel;

    juce::Slider variationKnob, mixKnob, inKnob, outKnob, tempoSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAtt;
    bool isStandalone = false;
    juce::Label variationLabel, mixLabel, inLabel, outLabel;
    juce::ToggleButton autoGainButton { "AUTO GAIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> variationAtt, mixAtt, inAtt, outAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAtt;

    ui::PatternDisplay pattern;
    ui::ModulePanel granularPanel, characterPanel, shaperPanel, spacePanel, modPanel;

    float meterIn = 0.0f, meterOut = 0.0f;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftboxAudioProcessorEditor)
};
