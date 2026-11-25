// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Widgets.h"

using namespace Theme;

juce::String paramTitle (int paramIndex)
{
    if (paramIndex < 0 || paramIndex >= Params::numParams) return {};
    const auto& s = Params::specs[paramIndex];
    const juce::String id (s.id);
    const auto prefix = id.upToFirstOccurrenceOf ("_", false, false);
    for (const auto& g : Params::groups)
        if (prefix == g.prefix)
            return juce::String (g.title) + " " + s.knob;
    return s.knob;
}

void showParamMenu (UiContext& ctx, juce::Component& target, int paramIndex, std::function<void()> reset)
{
    const int cc = ctx.proc.ccForParam (paramIndex);
    juce::PopupMenu m;
    m.addSectionHeader (Params::specs[paramIndex].name);
    m.addItem (1, "Learn MIDI CC...");
    m.addItem (2, cc >= 0 ? "Clear MIDI CC (CC " + juce::String (cc) + ")" : juce::String ("Clear MIDI CC"), cc >= 0);
    if (reset) m.addItem (3, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                     [&ctx, paramIndex, reset, safe = juce::Component::SafePointer<juce::Component> (&target)] (int r)
                     {
                         if (safe == nullptr) return;
                         if (r == 1 && ctx.armKnobLearn) ctx.armKnobLearn (paramIndex);
                         if (r == 2) ctx.proc.clearCcForParam (paramIndex);
                         if (r == 3 && reset) reset();
                     });
}

//==============================================================================
Knob::Knob (UiContext& c, const char* paramId, juce::Colour accent, bool large)
    : ctx (c), paramIndex (Yf19AudioProcessor::paramIndexForId (paramId)), isLarge (large)
{
    jassert (paramIndex >= 0);
    const auto& spec = Params::specs[paramIndex];
    label = spec.knob;
    getProperties().set (learnProperty, paramIndex);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setTooltip (spec.name);
    slider.setMouseDragSensitivity (large ? 320 : 220);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, spec.id, slider);
    slider.setDoubleClickReturnValue (true, spec.def);
    slider.onDragStart   = [this] { reportValue(); };
    slider.onValueChange = [this] { if (slider.isMouseButtonDown()) reportValue(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

Knob::~Knob() { slider.removeMouseListener (this); }

void Knob::reportValue()
{
    if (ctx.setLcd == nullptr) return;
    if (auto* p = ctx.proc.apvts.getParameter (Params::specs[paramIndex].id))
        ctx.setLcd (paramTitle (paramIndex) + "  " + p->getCurrentValueAsText(), cyan);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showParamMenu (ctx, slider, paramIndex, [safe = juce::Component::SafePointer<Knob> (this)]
                       { if (safe) safe->slider.setValue (Params::specs[safe->paramIndex].def, juce::sendNotificationSync); });
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (isLarge ? 24 : 16);
    const int size = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (text);
    g.setFont (isLarge ? display (12.0f) : mono (10.5f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (isLarge ? 20 : 14), juce::Justification::centred, 1, 0.55f);
}

//==============================================================================
ModeSwitch::ModeSwitch (UiContext& c, const char* paramId)
    : ctx (c), paramIndex (Yf19AudioProcessor::paramIndexForId (paramId))
{
    jassert (paramIndex >= 0);
    getProperties().set (learnProperty, paramIndex);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Filter mode: HP / LP");
    attachment = std::make_unique<juce::ParameterAttachment> (
        *ctx.proc.apvts.getParameter (paramId),
        [this] (float v) { value = v; repaint(); });
    attachment->sendInitialUpdate();
}

void ModeSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showParamMenu (ctx, *this, paramIndex, {}); return; }
    const float y = static_cast<float> (e.y) / static_cast<float> (juce::jmax (1, getHeight()));
    const float next = y < 0.3f ? 1.0f : (y > 0.7f ? 0.0f : (value >= 0.5f ? 0.0f : 1.0f));
    attachment->setValueAsCompleteGesture (next);
    if (ctx.setLcd) ctx.setLcd (next >= 0.5f ? "FILTER MODE  HIGH PASS 12 DB" : "FILTER MODE  LOW PASS 12 DB",
                                next >= 0.5f ? pink : cyan);
}

void ModeSwitch::paint (juce::Graphics& g)
{
    const bool hp = value >= 0.5f;
    auto r = getLocalBounds().toFloat();
    const float labelH = 22.0f;
    auto top = r.removeFromTop (labelH), bottom = r.removeFromBottom (labelH);

    g.setFont (display (14.0f, true));
    g.setColour (hp ? pink : dim);
    g.drawText ("HP", top, juce::Justification::centred, false);
    g.setColour (hp ? dim : cyan);
    g.drawText ("LP", bottom, juce::Justification::centred, false);

    // switch plate
    auto plate = r.reduced (0.0f, 6.0f).withSizeKeepingCentre (juce::jmin (r.getWidth(), 46.0f), r.getHeight() - 12.0f);
    g.setColour (lcdBg);
    g.fillRoundedRectangle (plate, 6.0f);
    g.setColour ((hp ? pink : cyan).withAlpha (0.55f));
    g.drawRoundedRectangle (plate.reduced (0.5f), 6.0f, 1.2f);
    // tick marks at both ends
    g.setColour (dim.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float> (12.0f, 2.0f).withCentre ({ plate.getCentreX(), plate.getY() + 8.0f }));
    g.fillRect (juce::Rectangle<float> (12.0f, 2.0f).withCentre ({ plate.getCentreX(), plate.getBottom() - 8.0f }));

    // nut, then the bat lever pointing up (HP) or down (LP)
    const auto c = plate.getCentre();
    const float nut = juce::jmin (plate.getWidth() * 0.62f, 28.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a3d8c), c.x - nut * 0.3f, c.y - nut * 0.3f,
                                             juce::Colour (0xff17133a), c.x + nut * 0.5f, c.y + nut * 0.5f, true));
    g.fillEllipse (juce::Rectangle<float> (nut, nut).withCentre (c));
    g.setColour (text.withAlpha (0.8f));
    g.drawEllipse (juce::Rectangle<float> (nut, nut).withCentre (c), 1.5f);

    const auto accent = hp ? pink : cyan;
    const float reach = plate.getHeight() * 0.5f - 16.0f;
    const juce::Point<float> tip (c.x, hp ? c.y - reach : c.y + reach);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (accent.withAlpha (0.10f * static_cast<float> (4 - i)));
        g.fillEllipse (juce::Rectangle<float> (14.0f + i * 3.0f, 14.0f + i * 3.0f).withCentre (tip));
    }
    g.setColour (text);
    g.drawLine ({ c, tip }, 6.0f);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (tip));
    g.setColour (accent);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (tip));
}

//==============================================================================
void LcdDisplay::setText (const juce::String& t)
{
    if (t != text) { text = t; if (! showingMessage()) repaint(); }
}

void LcdDisplay::showMessage (const juce::String& t, juce::Colour c, int holdMs)
{
    message = t;
    messageColour = c;
    persistent = holdMs < 0;
    messageUntil = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (juce::jmax (0, holdMs));
    repaint();
}

void LcdDisplay::clearMessage() { message.clear(); persistent = false; repaint(); }

bool LcdDisplay::showingMessage() const
{
    return message.isNotEmpty() && (persistent || juce::Time::getMillisecondCounter() < messageUntil);
}

void LcdDisplay::tick()
{
    if (message.isNotEmpty() && ! showingMessage())
        clearMessage();
}

void LcdDisplay::paint (juce::Graphics& g)
{
    drawLcd (g, getLocalBounds().toFloat(), colour);
    const bool msg = showingMessage();
    g.setColour (msg ? messageColour : colour);
    g.setFont (mono (13.0f));
    g.drawFittedText ((msg ? message : text).toUpperCase(), getLocalBounds().reduced (12, 0),
                      juce::Justification::centredLeft, 1, 0.75f);
}

//==============================================================================
void LevelMeter::setLevel (float peak)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float target = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    const float next = juce::jmax (target, level * 0.86f);
    if (std::abs (next - level) > 0.002f) { level = next; repaint(); }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad (cyan, 0.0f, r.getBottom(), pink, 0.0f, r.getY(), false);
    grad.addColour (0.75, orange);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (bg);
    g.fillRect (r.withHeight (r.getHeight() * (1.0f - level)));
}

//==============================================================================
ResponseDisplay::ResponseDisplay (UiContext& c) : ctx (c)
{
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<float> ResponseDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (12.0f).withTrimmedLeft (22.0f);
}

float ResponseDisplay::xForFreq (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * (std::log10 (hz) - std::log10 (20.0f)) / 3.0f;   // 20 Hz .. 20 kHz
}

float ResponseDisplay::yForDb (float db) const
{
    const auto a = plotArea();
    constexpr float top = 24.0f, bottom = -30.0f;
    return a.getY() + a.getHeight() * (top - juce::jlimit (bottom, top, db)) / (top - bottom);
}

void ResponseDisplay::tick()
{
    auto& s = ctx.proc.apvts;
    const float cut = s.getRawParameterValue ("flt_cutoff")->load();
    const float res = s.getRawParameterValue ("flt_reso")->load();
    const float mix = s.getRawParameterValue ("out_mix")->load();
    const float lvl = s.getRawParameterValue ("out_level")->load();
    const float mode = s.getRawParameterValue ("flt_mode")->load();
    using juce::exactlyEqual;
    if (! exactlyEqual (cut, shownCutoff) || ! exactlyEqual (res, shownReso) || ! exactlyEqual (mix, shownMix)
        || ! exactlyEqual (lvl, shownLevel) || ! exactlyEqual (mode, shownMode))
    {
        shownCutoff = cut; shownReso = res; shownMix = mix; shownLevel = lvl; shownMode = mode;
        recompute();
        repaint();
    }
}

void ResponseDisplay::recompute()
{
    // the audio's filter, linearised, pinged once
    moth::Korg35 f;
    f.setSampleRate (displayRate);
    f.set (shownCutoff, juce::jmin (moth::resoToK (shownReso), 1.93f), 0.0f, true);
    std::vector<float> data (static_cast<size_t> (fftSize * 2), 0.0f);
    const bool hp = shownMode >= 0.5f;
    for (int i = 0; i < fftSize; ++i)
    {
        float lo, hi;
        f.process (i == 0 ? 1.0f : 0.0f, lo, hi);
        data[static_cast<size_t> (i)] = hp ? hi : lo;
    }
    fft.performRealOnlyForwardTransform (data.data(), true);

    const int bins = fftSize / 2 + 1;
    wetDb.resize (static_cast<size_t> (bins));
    totalDb.resize (static_cast<size_t> (bins));
    const float lvl = juce::Decibels::decibelsToGain (shownLevel, -100.0f);
    for (int b = 0; b < bins; ++b)
    {
        const std::complex<float> h (data[static_cast<size_t> (2 * b)], data[static_cast<size_t> (2 * b + 1)]);
        const auto total = (h * shownMix + (1.0f - shownMix)) * lvl;
        wetDb[static_cast<size_t> (b)] = juce::Decibels::gainToDecibels (std::abs (h) * lvl, -60.0f);
        totalDb[static_cast<size_t> (b)] = juce::Decibels::gainToDecibels (std::abs (total), -60.0f);
    }
}

void ResponseDisplay::paint (juce::Graphics& g)
{
    drawLcd (g, getLocalBounds().toFloat(), cyan);
    const auto a = plotArea();

    // grid
    g.setFont (mono (10.0f));
    for (int hz : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
    {
        const float x = xForFreq (static_cast<float> (hz));
        const bool major = hz == 100 || hz == 1000 || hz == 10000;
        g.setColour (cyan.withAlpha (major ? 0.18f : 0.08f));
        g.fillRect (x, a.getY(), 1.0f, a.getHeight());
        if (major || hz == 500 || hz == 5000 || hz == 50)
        {
            g.setColour (dim);
            const juce::String t = hz >= 1000 ? juce::String (hz / 1000) + "K" : juce::String (hz);
            g.drawText (t, juce::Rectangle<float> (x - 20.0f, a.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred, false);
        }
    }
    for (int db : { 12, 0, -12, -24 })
    {
        const float y = yForDb (static_cast<float> (db));
        g.setColour (cyan.withAlpha (db == 0 ? 0.25f : 0.08f));
        g.fillRect (a.getX(), y, a.getWidth(), 1.0f);
        g.setColour (dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (a.getX() - 28.0f, y - 6.0f, 24.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }
    if (totalDb.empty()) return;

    auto curve = [&] (const std::vector<float>& db)
    {
        juce::Path p;
        const float binHz = static_cast<float> (displayRate / fftSize);
        bool first = true;
        for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
        {
            const float hz = std::pow (10.0f, std::log10 (20.0f) + 3.0f * (x - a.getX()) / a.getWidth());
            const float pos = hz / binHz;
            const int b0 = juce::jlimit (0, static_cast<int> (db.size()) - 2, static_cast<int> (pos));
            const float frac = juce::jlimit (0.0f, 1.0f, pos - static_cast<float> (b0));
            const float v = db[static_cast<size_t> (b0)] * (1.0f - frac) + db[static_cast<size_t> (b0 + 1)] * frac;
            if (first) p.startNewSubPath (x, yForDb (v)); else p.lineTo (x, yForDb (v));
            first = false;
        }
        return p;
    };

    const bool hp = shownMode >= 0.5f;
    const auto accent = hp ? pink : cyan;
    if (shownMix < 0.995f)   // the filter alone, faint, when the blend lets dry through
    {
        g.setColour (accent.withAlpha (0.35f));
        g.strokePath (curve (wetDb), juce::PathStrokeType (1.0f));
    }
    const auto main = curve (totalDb);
    juce::Path fill (main);
    fill.lineTo (a.getRight(), a.getBottom());
    fill.lineTo (a.getX(), a.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), 0.0f, a.getY(), accent.withAlpha (0.0f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (main, juce::PathStrokeType (4.0f));
    g.setColour (accent);
    g.strokePath (main, juce::PathStrokeType (1.6f));

    // cutoff marker
    const float cx = xForFreq (juce::jlimit (20.0f, 20000.0f, shownCutoff));
    g.setColour (orange.withAlpha (0.5f));
    for (float y = a.getY(); y < a.getBottom(); y += 6.0f)
        g.fillRect (cx, y, 1.0f, 3.0f);
    g.setColour (orange);
    g.setFont (mono (11.0f));
    const juce::String label = shownCutoff >= 1000.0f ? juce::String (shownCutoff / 1000.0f, 2) + " kHz"
                                                      : juce::String (juce::roundToInt (shownCutoff)) + " Hz";
    const bool right = cx < a.getRight() - 80.0f;
    g.drawText (label, juce::Rectangle<float> (right ? cx + 5.0f : cx - 75.0f, a.getY() + 2.0f, 70.0f, 13.0f),
                right ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
}

//==============================================================================
LearnOverlay::LearnOverlay (UiContext& c, juce::Component& r) : ctx (c), root (r) {}

std::vector<std::pair<int, juce::Rectangle<int>>> LearnOverlay::targets()
{
    std::vector<std::pair<int, juce::Rectangle<int>>> out;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (child == this || ! child->isVisible()) continue;
            if (child->getProperties().contains (learnProperty))
                out.emplace_back (static_cast<int> (child->getProperties()[learnProperty]),
                                  getLocalArea (child, child->getLocalBounds()));
            else
                walk (*child);
        }
    };
    walk (root);
    return out;
}

void LearnOverlay::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.45f));
    const int armed = ctx.proc.getArmedCcParam();
    for (const auto& [param, r] : targets())
    {
        const bool isArmed = param == armed;
        const int cc = ctx.proc.ccForParam (param);
        const bool mapped = cc >= 0;
        const auto rf = r.toFloat().reduced (1.0f);
        if (isArmed)
        {
            g.setColour (learn.withAlpha (0.28f));
            g.fillRoundedRectangle (rf, 4.0f);
        }
        g.setColour (learn.withAlpha (isArmed ? 1.0f : (mapped ? 0.85f : 0.4f)));
        g.drawRoundedRectangle (rf, 4.0f, isArmed ? 2.5f : 1.2f);

        const juce::String badge = isArmed ? "MOVE CC" : (mapped ? "CC " + juce::String (cc) : juce::String ("--"));
        g.setFont (mono (10.0f));
        const int bw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), badge) + 8;
        auto br = juce::Rectangle<int> (r.getRight() - bw - 2, r.getY() + 2, bw, 13).toFloat();
        g.setColour (mapped || isArmed ? learn : panelLo);
        g.fillRoundedRectangle (br, 3.0f);
        g.setColour (mapped || isArmed ? bg : dim);
        g.drawText (badge, br.toNearestInt(), juce::Justification::centred, false);
    }
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    int best = -1, bestArea = 1 << 30;
    for (const auto& [param, r] : targets())
        if (r.contains (e.getPosition()) && r.getWidth() * r.getHeight() < bestArea)
        {
            best = param;
            bestArea = r.getWidth() * r.getHeight();
        }

    if (best >= 0 && e.mods.isPopupMenu())
        ctx.proc.clearCcForParam (best);
    else if (best >= 0 && ctx.proc.getArmedCcParam() != best)
    {
        ctx.proc.armCcLearn (best);
        if (onArmed) onArmed (best);
    }
    else
    {
        ctx.proc.armCcLearn (-1);
        if (onCancelled) onCancelled();
    }
    repaint();
}
