// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"

using namespace Theme;

namespace
{
    const juce::Rectangle<int> filterPanel   { 20, 100, 500, 240 };
    const juce::Rectangle<int> outputPanel   { 530, 100, 270, 240 };
    const juce::Rectangle<int> responsePanel { 20, 350, 780, 160 };
}

//==============================================================================
void Yf19AudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    if (background.isNull())
    {
        background = juce::Image (juce::Image::RGB, designWidth, designHeight, true);
        juce::Graphics bgG (background);
        bgG.fillAll (bg);
        bgG.setColour (cyan.withAlpha (0.035f));   // scanlines
        for (int y = 0; y < designHeight; y += 3)
            bgG.fillRect (0, y, designWidth, 1);

        // logo
        auto logo = juce::Rectangle<int> (20, 20, 320, 40);
        bgG.setFont (display (32.0f, true));
        const juce::String word ("FILTER ");
        const int w = juce::GlyphArrangement::getStringWidthInt (bgG.getCurrentFont(), word);
        bgG.setColour (pink.withAlpha (0.35f));
        bgG.drawText (word, logo.translated (0, 1), juce::Justification::centredLeft, false);
        bgG.setColour (pink);
        bgG.drawText (word, logo, juce::Justification::centredLeft, false);
        bgG.setColour (orange);
        bgG.drawText ("YF-19", logo.withTrimmedLeft (w), juce::Justification::centredLeft, false);
        bgG.setColour (text);
        bgG.setFont (mono (11.0f));
        bgG.drawText ("12 DB OTA FILTER // HP + LP // MIDI CONTROL", 21, 60, 320, 14, juce::Justification::centredLeft, false);

        // tri-colour bar
        const int barY = 82, x0 = 20, bw = designWidth - 40;
        bgG.setColour (orange); bgG.fillRect (x0, barY, bw * 3 / 6, 3);
        bgG.setColour (pink);   bgG.fillRect (x0 + bw * 3 / 6, barY, bw * 2 / 6, 3);
        bgG.setColour (cyan);   bgG.fillRect (x0 + bw * 5 / 6, barY, bw - bw * 5 / 6, 3);

        for (const auto& [r, title, a1, a2] : panels)
            drawPanel (bgG, r, title, a1, a2);

        // meter captions
        bgG.setColour (dim);
        bgG.setFont (mono (10.0f));
        bgG.drawText ("IN",  outputPanel.getRight() - 50, outputPanel.getBottom() - 22, 20, 12, juce::Justification::centred, false);
        bgG.drawText ("OUT", outputPanel.getRight() - 32, outputPanel.getBottom() - 22, 24, 12, juce::Justification::centred, false);
    }
    g.drawImageAt (background, 0, 0);

    // MIDI activity light
    g.setFont (mono (10.0f));
    g.setColour (midiLit ? pink : dim);
    g.drawText ("MIDI", midiDot.withTrimmedRight (12), juce::Justification::centredRight, false);
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ static_cast<float> (midiDot.getRight()) - 4.0f,
                                                                        static_cast<float> (midiDot.getCentreY()) });
    if (midiLit)
    {
        g.setColour (pink.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (3.0f));
    }
    g.setColour (midiLit ? pink : panel);
    g.fillEllipse (dot);
}

//==============================================================================
Yf19AudioProcessorEditor::Yf19AudioProcessorEditor (Yf19AudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), ctx (p),
      modeSwitch (ctx, "flt_mode"),
      cutoff (ctx, "flt_cutoff", cyan, true),
      reso   (ctx, "flt_reso", pink, true),
      drive  (ctx, "flt_drive", orange),
      blend  (ctx, "out_mix", orange, true),
      level  (ctx, "out_level", orange, true),
      response (ctx),
      overlay (ctx, canvas)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    ctx.setLcd       = [this] (const juce::String& t, juce::Colour c) { setLcd (t, c); };
    ctx.armKnobLearn = [this] (int param) { armKnobLearn (param); };

    canvas.panels = {
        { filterPanel,   "FILTER // VCF",   cyan, pink },
        { outputPanel,   "OUTPUT // BLEND", orange, orange },
        { responsePanel, "RESPONSE",        pink, cyan },
    };
    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, designWidth, designHeight);

    learnButton.setClickingTogglesState (true);
    learnButton.setColour (juce::TextButton::buttonOnColourId, learn);
    learnButton.setTooltip ("Click a control, then move a knob or fader on your MIDI controller. "
                            "Right-click any control to learn or clear just that one.");
    learnButton.onClick = [this] { setLearnMode (learnButton.getToggleState()); };
    ccMenuButton.setColour (juce::TextButton::buttonOnColourId, learn);
    ccMenuButton.setTooltip ("MIDI CC map: reset to the defaults or clear it");
    ccMenuButton.onClick = [this] { showLearnMenu(); };

    modeSwitch.setTooltip ("Filter mode: HP (up) / LP (down). CC values 0-63 = LP, 64-127 = HP");
    cutoff.setTooltip ("Cutoff frequency (CC 74 by default)");
    reso.setTooltip ("Resonance - screams and self-oscillates near the top (CC 71 by default)");
    drive.setTooltip ("Level into the filter's saturating resonance loop");
    blend.setTooltip ("Dry / filtered blend (latency-aligned, no phasing)");
    level.setTooltip ("Output level");

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &lcd, &learnButton, &ccMenuButton, &modeSwitch, &cutoff, &reso, &drive, &blend, &level,
             &inMeter, &outMeter, &response })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (overlay);
    overlay.onArmed = [this] (int param) { setLcd ("LEARN: MOVE A KNOB FOR " + paramTitle (param), learn); };
    overlay.onCancelled = [this] { setLcd ("LEARN: CLICK A CONTROL  //  ESC TO EXIT", learn); };

    updateStatusText();
    seenCcLearnCount = proc.getCcLearnCount();
    seenMidi = proc.getMidiActivity();
    sendLookAndFeelChange();

    const float savedScale = juce::jlimit (0.75f, 1.75f, proc.uiScale);
    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 7 / 4, designHeight * 7 / 4);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (juce::roundToInt (designWidth * savedScale), juce::roundToInt (designHeight * savedScale));
    startTimerHz (30);
}

Yf19AudioProcessorEditor::~Yf19AudioProcessorEditor()
{
    proc.armCcLearn (-1);
    setLookAndFeel (nullptr);
}

void Yf19AudioProcessorEditor::setLearnMode (bool on)
{
    ctx.learnMode = on;
    learnButton.setToggleState (on, juce::dontSendNotification);
    proc.armCcLearn (-1);
    overlay.setVisible (on);
    overlay.toFront (false);
    if (on) setLcd ("LEARN: CLICK A CONTROL  //  ESC TO EXIT", learn);
    else    lcd.clearMessage();
}

void Yf19AudioProcessorEditor::armKnobLearn (int param)
{
    proc.armCcLearn (param);
    setLcd ("LEARN: MOVE A KNOB FOR " + paramTitle (param) + "  //  ESC CANCELS", learn);
}

void Yf19AudioProcessorEditor::showLearnMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("MIDI CC MAP");
    for (int i = 0; i < Params::numParams; ++i)
    {
        const int cc = proc.ccForParam (i);
        m.addItem (100 + i, paramTitle (i) + "   " + (cc >= 0 ? "CC " + juce::String (cc) : juce::String ("--")), false);
    }
    m.addSeparator();
    m.addItem (1, "Reset to defaults (CC 74 cutoff, CC 71 reso)");
    m.addItem (2, "Clear all");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&ccMenuButton),
                     [safe = juce::Component::SafePointer<Yf19AudioProcessorEditor> (this)] (int r)
                     {
                         if (safe == nullptr) return;
                         if (r == 1) { safe->proc.resetCcsToDefault(); safe->setLcd ("CC MAP RESET: CC 74 CUTOFF, CC 71 RESO", learn); }
                         if (r == 2) { safe->proc.clearAllCcs(); safe->setLcd ("CC MAP CLEARED", learn); }
                         safe->overlay.repaint();
                     });
}

bool Yf19AudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (proc.getArmedCcParam() >= 0)
    {
        proc.armCcLearn (-1);
        setLcd (ctx.learnMode ? "LEARN: CANCELLED  //  CLICK A CONTROL" : "LEARN: CANCELLED", learn);
        overlay.repaint();
        return true;
    }
    if (ctx.learnMode) { setLearnMode (false); return true; }
    return false;
}

void Yf19AudioProcessorEditor::updateStatusText()
{
    auto& s = proc.apvts;
    const bool hp = s.getRawParameterValue ("flt_mode")->load() >= 0.5f;
    auto txt = [&] (const char* id) { return s.getParameter (id)->getCurrentValueAsText(); };
    lcd.setText (juce::String (hp ? "HP 12" : "LP 12") + " // " + txt ("flt_cutoff") + " // RESO " + txt ("flt_reso"));
}

void Yf19AudioProcessorEditor::timerCallback()
{
    updateStatusText();
    lcd.tick();
    response.tick();
    inMeter.setLevel (proc.takeInputPeak());
    outMeter.setLevel (proc.takeOutputPeak());

    if (const int m = proc.getMidiActivity(); m != seenMidi) { seenMidi = m; midiHold = 5; }
    const bool lit = midiHold > 0;
    if (midiHold > 0) --midiHold;
    if (lit != canvas.midiLit) { canvas.midiLit = lit; canvas.repaint (canvas.midiDot.expanded (4)); }

    bool learnChanged = false;
    if (const int n = proc.getCcLearnCount(); n != seenCcLearnCount)
    {
        seenCcLearnCount = n;
        learnChanged = true;
        const int cc = proc.getLastLearnedCc();
        for (int i = 0; i < Params::numParams; ++i)
            if (proc.ccForParam (i) == cc)
                setLcd (paramTitle (i) + " <- CC " + juce::String (cc), learn);
    }
    if (overlay.isVisible() && (learnChanged || ++tickCount % 3 == 0))
        overlay.repaint();
}

void Yf19AudioProcessorEditor::resized()
{
    if (getWidth() <= 0) return;
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (designWidth);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    proc.uiScale = scale;

    // header, right-aligned
    learnButton.setBounds (690, 28, 110, 32);
    ccMenuButton.setBounds (644, 28, 40, 32);
    canvas.midiDot = { 586, 28, 52, 32 };
    lcd.setBounds (350, 28, 230, 32);

    // FILTER // VCF
    const int top = filterPanel.getY() + 40;
    modeSwitch.setBounds (filterPanel.getX() + 16, top, 64, filterPanel.getBottom() - top - 12);
    cutoff.setBounds (filterPanel.getX() + 92, top + 6, 150, 180);
    reso.setBounds   (filterPanel.getX() + 250, top + 6, 150, 180);
    drive.setBounds  (filterPanel.getX() + 408, top + 50, 82, 96);

    // OUTPUT // BLEND
    blend.setBounds (outputPanel.getX() + 10, top + 30, 100, 130);
    level.setBounds (outputPanel.getX() + 112, top + 30, 100, 130);
    inMeter.setBounds  (outputPanel.getRight() - 44, top + 4, 8, outputPanel.getBottom() - top - 30);
    outMeter.setBounds (outputPanel.getRight() - 24, top + 4, 8, outputPanel.getBottom() - top - 30);

    response.setBounds (responsePanel.getX() + 12, responsePanel.getY() + 36, responsePanel.getWidth() - 24, responsePanel.getHeight() - 46);
    overlay.setBounds (0, 92, designWidth, designHeight - 92);   // below the header, so MIDI LEARN stays clickable
}
