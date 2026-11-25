// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

// One page, YF-21 style: header (logo, status LCD, MIDI activity, MIDI LEARN), the FILTER
// and OUTPUT panels, and the response display. Fixed 820x520 canvas scaled to the window.
class Yf19AudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit Yf19AudioProcessorEditor (Yf19AudioProcessor&);
    ~Yf19AudioProcessorEditor() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void setLearnMode (bool on);

private:
    class Canvas : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
        std::vector<std::tuple<juce::Rectangle<int>, juce::String, juce::Colour, juce::Colour>> panels;
        bool midiLit = false;
        juce::Rectangle<int> midiDot;
    private:
        juce::Image background;
    };

    void timerCallback() override;
    void armKnobLearn (int param);
    void setLcd (const juce::String& t, juce::Colour c = Theme::cyan)
    {
        lcd.showMessage (t, c, ctx.learnMode && c == Theme::learn ? -1 : 2500);
    }
    void updateStatusText();
    void showLearnMenu();

    Yf19AudioProcessor& proc;
    Theme::FontKeeper fonts;
    Theme::LookAndFeel lnf;
    UiContext ctx;

    Canvas canvas;
    LcdDisplay lcd;
    juce::TextButton learnButton { "MIDI LEARN" }, ccMenuButton { "CC" };

    ModeSwitch modeSwitch;
    Knob cutoff, reso, drive, blend, level;
    LevelMeter inMeter, outMeter;
    ResponseDisplay response;
    LearnOverlay overlay;
    juce::TooltipWindow tooltips { this, 700 };

    int seenCcLearnCount = 0, seenMidi = 0, midiHold = 0, tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Yf19AudioProcessorEditor)
};
