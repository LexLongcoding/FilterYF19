// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <functional>
#include <memory>

// Shared by every widget: the processor plus callbacks into the editor.
struct UiContext
{
    explicit UiContext (Yf19AudioProcessor& p) : proc (p) {}

    Yf19AudioProcessor& proc;
    bool learnMode = false;
    std::function<void (const juce::String&, juce::Colour)> setLcd;
    std::function<void (int param)> armKnobLearn;    // from a control's right-click menu
};

// Any control bound to a parameter carries this property; MIDI learn finds them by it.
constexpr const char* learnProperty = "yfParam";

juce::String paramTitle (int paramIndex);           // "FILTER CUTOFF"
void showParamMenu (UiContext&, juce::Component& target, int paramIndex, std::function<void()> reset);

//==============================================================================
// A parameter knob with its label underneath; right-click for MIDI CC learn.
class Knob : public juce::Component
{
public:
    Knob (UiContext&, const char* paramId, juce::Colour accent, bool large = false);
    ~Knob() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // via listener on the slider
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }

private:
    void reportValue();

    UiContext& ctx;
    const int paramIndex;
    const bool isLarge;
    juce::String label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The HP / LP toggle, drawn as a hardware bat switch (HP up, LP down) like the module's.
// Click to flip; right-click for MIDI learn. A CC below 64 = LP, 64 and up = HP.
class ModeSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModeSwitch (UiContext&, const char* paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    UiContext& ctx;
    const int paramIndex;
    float value = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
// LCD showing a status line, temporarily replaced by messages (knob values, MIDI learn).
class LcdDisplay : public juce::Component
{
public:
    explicit LcdDisplay (juce::Colour c = Theme::cyan) : colour (c) {}
    void setText (const juce::String& t);
    void showMessage (const juce::String& t, juce::Colour c, int holdMs = 2500);   // holdMs < 0: until cleared
    void clearMessage();
    void tick();
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
    juce::String text, message;
    juce::Colour messageColour { Theme::cyan };
    juce::uint32 messageUntil = 0;
    bool persistent = false;
    bool showingMessage() const;
};

class LevelMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
};

// The filter's frequency response, drawn from the same DSP code the audio uses (linear
// model, impulse -> FFT), including the BLEND with the dry signal and the output LEVEL.
class ResponseDisplay : public juce::Component
{
public:
    explicit ResponseDisplay (UiContext&);
    void paint (juce::Graphics&) override;
    void tick();

private:
    void recompute();
    float xForFreq (float hz) const;
    float yForDb (float db) const;
    juce::Rectangle<float> plotArea() const;

    UiContext& ctx;
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    static constexpr double displayRate = 96000.0;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> wetDb, totalDb;   // per FFT bin
    float shownCutoff = -1, shownReso = -1, shownMix = -1, shownLevel = -100, shownMode = -1;
};

// MIDI learn mode: dims the editor, outlines every learnable control with its CC.
// Click one, then move a knob / fader on your controller. Right-click clears.
class LearnOverlay : public juce::Component
{
public:
    LearnOverlay (UiContext&, juce::Component& root);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (int param)> onArmed;
    std::function<void()> onCancelled;

private:
    std::vector<std::pair<int, juce::Rectangle<int>>> targets();
    UiContext& ctx;
    juce::Component& root;
};
