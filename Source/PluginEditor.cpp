#include "PluginEditor.h"

using namespace drift;

namespace ui
{
//==============================================================================
DriftLookAndFeel::DriftLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff23232d));
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d1d26));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff34344a));
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::Label::textColourId, colours::dim);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::text);
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xee1d1d26));
    setColour (juce::BubbleComponent::outlineColourId, colours::panelEdge);
    setColour (juce::TooltipWindow::textColourId, colours::text);
}

void DriftLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = 3.5f;
    const float arcR = radius - lineW;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Cuerpo
    g.setColour (juce::Colour (0xff262632));
    g.fillEllipse (juce::Rectangle<float> (arcR * 1.5f, arcR * 1.5f).withCentre (centre));

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff30303e));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar (rango simétrico): el arco parte del centro
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? accent : accent.withAlpha (0.3f));
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const juce::Point<float> tip (centre.x + std::sin (angle) * arcR * 0.62f, centre.y - std::cos (angle) * arcR * 0.62f);
    g.setColour (colours::text);
    g.drawLine ({ centre.getPointOnCircumference (arcR * 0.15f, angle), tip }, 2.0f);
}

void DriftLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent.withAlpha (0.22f) : juce::Colour (0xff23232d).brighter (highlighted ? 0.08f : 0.0f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? accent : colours::panelEdge);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);
    g.setColour (on ? accent : colours::dim);
    g.setFont (ui::font (11.0f, true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

void DriftLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff23232d).brighter (down ? 0.25f : highlighted ? 0.12f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

//==============================================================================
void RandomizeButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    if (down) r = r.reduced (1.5f);
    const float rad = r.getHeight() * 0.5f;

    juce::ColourGradient grad (colours::shaper, r.getX(), r.getY(), colours::granular, r.getRight(), r.getBottom(), false);
    grad.addColour (0.5, colours::mod);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, rad);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path pill;
        pill.addRoundedRectangle (r, rad);
        g.reduceClipRegion (pill);
        g.setColour (juce::Colours::white.withAlpha (0.12f + 0.35f * flashAmount + (highlighted ? 0.08f : 0.0f)));
        g.fillRect (r.withHeight (r.getHeight() * 0.5f));
    }

    // Dado
    const auto die = juce::Rectangle<float> (r.getHeight() * 0.56f, r.getHeight() * 0.56f)
                         .withCentre ({ r.getX() + rad + 8.0f, r.getCentreY() });
    g.setColour (juce::Colour (0xff111116).withAlpha (0.85f));
    g.fillRoundedRectangle (die, 5.0f);
    g.setColour (juce::Colours::white);
    const float d = die.getWidth() * 0.16f;
    auto dot = [&] (float fx, float fy) { g.fillEllipse (die.getX() + die.getWidth() * fx - d * 0.5f, die.getY() + die.getHeight() * fy - d * 0.5f, d, d); };
    dot (0.27f, 0.27f); dot (0.73f, 0.73f); dot (0.5f, 0.5f); dot (0.73f, 0.27f); dot (0.27f, 0.73f);

    g.setColour (juce::Colour (0xff111116));
    g.setFont (ui::font (20.0f, true));
    g.drawText ("RANDOMIZE", r.withTrimmedLeft (die.getRight() - r.getX() + 4.0f), juce::Justification::centred);
}

//==============================================================================
void PatternDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (juce::Colour (0xff15151b));
    g.fillRoundedRectangle (r, 8.0f);

    const auto rawVal = [&] (P p) { return proc.apvts.getRawParameterValue (spec (p).key)->load(); };
    const int steps = juce::jlimit (1, 32, (int) std::lround (rawVal (P::ShSteps)));
    const int hits  = juce::jlimit (0, steps, (int) std::lround (rawVal (P::ShHits)));
    const int rot   = (int) std::lround (rawVal (P::ShRotate));
    const bool on   = rawVal (P::ShOn) > 0.5f;
    const float curve = rawVal (P::ShCurve);
    const float depth = rawVal (P::ShDepth);
    const uint32_t mask = euclidMask (steps, hits, rot);
    const int current = proc.uiStep.load();

    g.setColour (colours::dim);
    g.setFont (ui::font (11.0f, true));
    g.drawText (juce::String (hits) + " / " + juce::String (steps) + "  @ " + divisionAt (rawVal (P::ShDiv)).name,
                r.removeFromTop (20.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft);

    auto area = r.reduced (8.0f, 6.0f);
    const float cellW = area.getWidth() / (float) steps;
    for (int i = 0; i < steps; ++i)
    {
        auto cell = juce::Rectangle<float> (area.getX() + cellW * (float) i, area.getY(), cellW, area.getHeight()).reduced (2.0f, 0.0f);
        const bool hit = (mask >> i) & 1u;
        const bool isCurrent = on && i == current;

        g.setColour (juce::Colour (0xff22222c));
        g.fillRoundedRectangle (cell, 3.0f);

        if (hit)
        {
            // Envolvente del paso (decay)
            juce::Path env;
            const int nPts = 12;
            env.startNewSubPath (cell.getX(), cell.getBottom());
            for (int k = 0; k <= nPts; ++k)
            {
                const float ph = (float) k / (float) nPts;
                const float gv = std::exp (-ph * curve * 6.0f);
                const float level = 1.0f - depth * (1.0f - gv);
                env.lineTo (cell.getX() + cell.getWidth() * ph, cell.getBottom() - cell.getHeight() * level);
            }
            env.lineTo (cell.getRight(), cell.getBottom());
            env.closeSubPath();
            g.setColour (colours::shaper.withAlpha (on ? (isCurrent ? 1.0f : 0.55f) : 0.2f));
            g.fillPath (env);
        }
        else
        {
            const float level = 1.0f - depth;
            g.setColour (colours::shaper.withAlpha (on ? 0.15f : 0.06f));
            g.fillRect (cell.withTop (cell.getBottom() - cell.getHeight() * level));
        }

        if (isCurrent)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (cell, 3.0f, 1.2f);
        }
    }
}

//==============================================================================
ModulePanel::ModulePanel (DriftboxAudioProcessor& proc, Module module, const juce::String& t, juce::Colour a,
                          P onFlag, P lockFlag, int cols)
    : title (t), accent (a), columns (cols)
{
    for (const auto& s : specs())
    {
        if (s.module != module || s.id == onFlag) continue;

        auto c = std::make_unique<Control>();
        c->label = std::make_unique<juce::Label> ("", s.name.toUpperCase());
        c->label->setJustificationType (juce::Justification::centred);
        c->label->setFont (ui::font (10.0f, true));
        c->label->setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (*c->label);

        switch (s.kind)
        {
            case Kind::Float:
            case Kind::Int:
                c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
                c->slider->setColour (juce::Slider::rotarySliderFillColourId, accent);
                c->slider->setPopupDisplayEnabled (true, true, nullptr);
                c->slider->setTextValueSuffix (s.unit.isNotEmpty() ? " " + s.unit : juce::String());
                addAndMakeVisible (*c->slider);
                c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, s.key, *c->slider);
                break;
            case Kind::Choice:
                c->combo = std::make_unique<juce::ComboBox>();
                c->combo->addItemList (s.choices, 1);
                addAndMakeVisible (*c->combo);
                c->cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, s.key, *c->combo);
                break;
            case Kind::Bool:
                const auto shortName = s.name.containsChar (' ') ? s.name.fromLastOccurrenceOf (" ", false, false) : s.name;
                c->toggle = std::make_unique<juce::ToggleButton> (shortName.toUpperCase());
                c->toggle->setColour (juce::ToggleButton::tickColourId, accent);
                addAndMakeVisible (*c->toggle);
                c->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, s.key, *c->toggle);
                break;
        }
        controls.push_back (std::move (c));
    }

    if (onFlag != P::Count)
    {
        hasOn = true;
        onButton.setColour (juce::ToggleButton::tickColourId, accent);
        addAndMakeVisible (onButton);
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, spec (onFlag).key, onButton);
    }
    lockButton.setColour (juce::ToggleButton::tickColourId, colours::global);
    lockButton.setTooltip ("Bloquea este módulo: el Randomizer no lo modifica");
    addAndMakeVisible (lockButton);
    lockAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, spec (lockFlag).key, lockButton);
}

int ModulePanel::getPreferredWidth() const
{
    return 2 * kPad + columns * kCellW + (extra != nullptr ? extraWidth + kPad : 0);
}

void ModulePanel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (kPad, 11.0f, 4.0f, 12.0f), 2.0f);
    g.setColour (colours::text);
    g.setFont (ui::font (13.0f, true));
    g.drawText (title.toUpperCase(), juce::Rectangle<int> (kPad + 10, 0, 200, kHeader + 4), juce::Justification::centredLeft);
}

void ModulePanel::resized()
{
    auto header = getLocalBounds().removeFromTop (kHeader + 4).reduced (kPad, 7);
    lockButton.setBounds (header.removeFromRight (54));
    if (hasOn)
    {
        header.removeFromRight (6);
        onButton.setBounds (header.removeFromRight (42));
    }

    auto body = getLocalBounds().withTrimmedTop (kHeader).reduced (kPad, 0).withTrimmedBottom (kPad);
    if (extra != nullptr)
        extra->setBounds (body.withTrimmedLeft (columns * kCellW + kPad));

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const int col = (int) i % columns, row = (int) i / columns;
        auto cell = juce::Rectangle<int> (body.getX() + col * kCellW, body.getY() + row * kCellH, kCellW, kCellH).reduced (3, 2);
        auto& c = *controls[i];
        c.label->setBounds (cell.removeFromBottom (16));
        if (c.slider)       c.slider->setBounds (cell.reduced (4, 0));
        else if (c.combo)   c.combo->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24));
        else if (c.toggle)  c.toggle->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 6, 26));
    }
}
} // namespace ui

//==============================================================================
namespace
{
void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Component& parent, juce::Colour accent)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    s.setPopupDisplayEnabled (true, true, nullptr);
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setFont (ui::font (10.0f, true));
    l.setJustificationType (juce::Justification::centred);
    parent.addAndMakeVisible (l);
}
} // namespace

DriftboxAudioProcessorEditor::DriftboxAudioProcessorEditor (DriftboxAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pattern (p),
      granularPanel  (p, Module::Granular,  "Granular",  ui::colours::granular,  P::GrOn, P::LockGr, 7),
      characterPanel (p, Module::Character, "Character", ui::colours::character, P::ChOn, P::LockCh, 4),
      shaperPanel    (p, Module::Shaper,    "Shaper",    ui::colours::shaper,    P::ShOn, P::LockSh, 6),
      spacePanel     (p, Module::Space,     "Space",     ui::colours::space,     P::SpOn, P::LockSp, 3),
      modPanel       (p, Module::Mod,       "Modulation", ui::colours::mod,      P::Count, P::LockMd, 3)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (randomButton);
    randomButton.onClick = [this]
    {
        proc.randomize();
        randomButton.flash();
        presetLabel.setText (proc.getPresetLabel(), juce::dontSendNotification);
    };

    addAndMakeVisible (backButton);
    addAndMakeVisible (fwdButton);
    backButton.setTooltip ("Preset anterior");
    fwdButton.setTooltip ("Preset siguiente");
    backButton.onClick = [this] { proc.historyBack(); };
    fwdButton.onClick  = [this] { proc.historyForward(); };

    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setFont (ui::font (13.0f, true));
    presetLabel.setColour (juce::Label::textColourId, ui::colours::text);
    addAndMakeVisible (presetLabel);

    setupKnob (variationKnob, variationLabel, "VARIATION", *this, ui::colours::global);
    setupKnob (mixKnob, mixLabel, "MIX", *this, ui::colours::global);
    setupKnob (inKnob, inLabel, "INPUT", *this, ui::colours::global);
    setupKnob (outKnob, outLabel, "OUTPUT", *this, ui::colours::global);
    variationKnob.setTooltip ("Cuánto cambia cada Randomize: bajo = mutación sutil, alto = preset totalmente nuevo");
    variationAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, spec (P::Variation).key, variationKnob);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, spec (P::Mix).key, mixKnob);
    inAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, spec (P::InputGain).key, inKnob);
    outAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, spec (P::OutputGain).key, outKnob);
    inKnob.setTextValueSuffix (" dB");
    outKnob.setTextValueSuffix (" dB");

    isStandalone = proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    if (isStandalone)
    {
        // Sin DAW no hay tempo del host: se fija aquí (arrastra o doble clic para escribir)
        tempoSlider.setSliderStyle (juce::Slider::LinearBar);
        tempoSlider.setColour (juce::Slider::trackColourId, ui::colours::mod.withAlpha (0.35f));
        tempoSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff23232d));
        tempoSlider.setColour (juce::Slider::textBoxTextColourId, ui::colours::text);
        tempoSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        tempoSlider.setTooltip ("Tempo para los efectos rítmicos (en un DAW se usa el tempo del proyecto)");
        addAndMakeVisible (tempoSlider);
        tempoAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, spec (P::Tempo).key, tempoSlider);
        tempoSlider.setNumDecimalPlacesToDisplay (0);
        tempoSlider.setTextValueSuffix (" BPM");
    }

    autoGainButton.setColour (juce::ToggleButton::tickColourId, ui::colours::global);
    addAndMakeVisible (autoGainButton);
    autoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, spec (P::AutoGain).key, autoGainButton);

    shaperPanel.setExtra (&pattern, 250);
    for (auto* panel : { &granularPanel, &characterPanel, &shaperPanel, &spacePanel, &modPanel })
        addAndMakeVisible (*panel);

    presetLabel.setText (proc.getPresetLabel(), juce::dontSendNotification);
    setSize (1110, 552);
    startTimerHz (30);
}

DriftboxAudioProcessorEditor::~DriftboxAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DriftboxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff18181f), 0.0f, 0.0f,
                                             juce::Colour (0xff0d0d11), 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (ui::colours::text);
    g.setFont (ui::font (28.0f, true));
    g.drawText ("DRIFTBOX", 20, 22, 260, 34, juce::Justification::centredLeft);
    g.setColour (ui::colours::dim);
    g.setFont (ui::font (12.0f));
    g.drawText ("texture  /  motion  /  character", 22, 56, 260, 18, juce::Justification::centredLeft);
    g.setFont (ui::font (11.0f));
    if (! isStandalone)
        g.drawText (juce::String (proc.uiBpm.load(), 1) + " BPM", 22, 76, 120, 16, juce::Justification::centredLeft);

    // Medidores
    auto drawMeter = [&] (juce::Rectangle<int> r, float level, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff1d1d26));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = r.toFloat().reduced (2.0f);
        fill = fill.withTop (fill.getBottom() - fill.getHeight() * frac);
        g.setColour (db > -1.0f ? juce::Colour (0xffff5c5c) : ui::colours::granular);
        g.fillRoundedRectangle (fill, 2.0f);
        g.setColour (ui::colours::dim);
        g.setFont (ui::font (9.0f, true));
        g.drawText (name, r.withY (r.getBottom() + 2).withHeight (12).expanded (6, 0), juce::Justification::centred);
    };
    if (! meterArea.isEmpty())
    {
        auto m = meterArea;
        drawMeter (m.removeFromLeft (10), meterIn, "IN");
        m.removeFromLeft (8);
        drawMeter (m.removeFromLeft (10), meterOut, "OUT");
    }
}

void DriftboxAudioProcessorEditor::resized()
{
    tempoSlider.setBounds (20, 76, 110, 20);
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (100);

    // Centro: botón mágico + historial + etiqueta
    auto centre = header.withSizeKeepingCentre (330, 100);
    auto row = centre.removeFromTop (64).withSizeKeepingCentre (330, 52);
    backButton.setBounds (row.removeFromLeft (34).withSizeKeepingCentre (34, 34));
    fwdButton.setBounds (row.removeFromRight (34).withSizeKeepingCentre (34, 34));
    randomButton.setBounds (row.reduced (8, 0));
    presetLabel.setBounds (centre.removeFromTop (22));

    // Derecha: variación, mix, in, out, auto gain, medidores
    auto right = header.withTrimmedLeft (header.getWidth() / 2 + 175);
    meterArea = right.removeFromRight (28).withTrimmedTop (14).withTrimmedBottom (30);
    right.removeFromRight (10);
    auto knobs = right.removeFromTop (76);
    const int kw = 64;
    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        auto c = knobs.removeFromLeft (kw);
        l.setBounds (c.removeFromBottom (14));
        s.setBounds (c.reduced (4, 0));
    };
    place (variationKnob, variationLabel);
    knobs.removeFromLeft (6);
    place (mixKnob, mixLabel);
    place (inKnob, inLabel);
    place (outKnob, outLabel);
    autoGainButton.setBounds (right.removeFromTop (22).withSizeKeepingCentre (96, 22).withX (right.getX() + kw + 6 + kw));

    area.removeFromTop (6);
    const int panelH = ui::ModulePanel::kHeader + 2 * ui::ModulePanel::kCellH + ui::ModulePanel::kPad;
    const int gap = 8;

    auto row1 = area.removeFromTop (panelH);
    granularPanel.setBounds (row1.removeFromLeft (granularPanel.getPreferredWidth()));
    row1.removeFromLeft (gap);
    characterPanel.setBounds (row1.removeFromLeft (characterPanel.getPreferredWidth()));
    row1.removeFromLeft (gap);
    modPanel.setBounds (row1);

    area.removeFromTop (gap);
    auto row2 = area.removeFromTop (panelH);
    spacePanel.setBounds (row2.removeFromRight (spacePanel.getPreferredWidth()));
    row2.removeFromRight (gap);
    shaperPanel.setBounds (row2);
}

void DriftboxAudioProcessorEditor::timerCallback()
{
    const auto label = proc.getPresetLabel();
    if (presetLabel.getText() != label)
        presetLabel.setText (label, juce::dontSendNotification);

    backButton.setEnabled (proc.canGoBack());
    fwdButton.setEnabled (proc.canGoForward());

    meterIn  = proc.uiInLevel.load();
    meterOut = proc.uiOutLevel.load();
    repaint (meterArea.expanded (8, 16));
    repaint (0, 70, 200, 30);
    pattern.repaint();
}
