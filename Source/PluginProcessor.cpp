// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioParameterFloatAttributes attributesFor (Params::Unit unit)
    {
        using U = Params::Unit;
        auto a = juce::AudioParameterFloatAttributes();
        switch (unit)
        {
            case U::Hz:
                return a.withLabel ("Hz")
                    .withStringFromValueFunction ([] (float v, int)
                    {
                        if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
                        return juce::String (juce::roundToInt (v)) + " Hz";
                    })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        const float v = t.getFloatValue();
                        return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                    });
            case U::Db:
                return a.withLabel ("dB")
                    .withStringFromValueFunction ([] (float v, int)
                    {
                        if (v <= -23.95f) return juce::String ("-24.0 dB");
                        return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
                    })
                    .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
            case U::Percent:
            case U::Choice:
            default:
                break;
        }
        return a.withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Yf19AudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : Params::specs)
    {
        const juce::ParameterID pid { s.id, 1 };
        if (s.unit == Params::Unit::Choice)
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name,
                            juce::StringArray::fromTokens (s.choices, "|", ""), static_cast<int> (s.def)));
        else
        {
            juce::NormalisableRange<float> range (s.min, s.max);
            if (s.centre > 0.0f)
                range.setSkewForCentre (s.centre);
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, range, s.def, attributesFor (s.unit)));
        }
    }
    return layout;
}

int Yf19AudioProcessor::paramIndexForId (const juce::String& id)
{
    for (int i = 0; i < Params::numParams; ++i)
        if (id == Params::specs[i].id)
            return i;
    return -1;
}

Yf19AudioProcessor::Yf19AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FILTER_YF19", createLayout())
{
    for (int i = 0; i < Params::numParams; ++i)
    {
        paramList[static_cast<size_t> (i)] = apvts.getParameter (Params::specs[i].id);
        raw[static_cast<size_t> (i)] = apvts.getRawParameterValue (Params::specs[i].id);
    }
    ixMode = paramIndexForId ("flt_mode");   ixCutoff = paramIndexForId ("flt_cutoff");
    ixReso = paramIndexForId ("flt_reso");   ixDrive = paramIndexForId ("flt_drive");
    ixMix = paramIndexForId ("out_mix");     ixLevel = paramIndexForId ("out_level");
    resetCcsToDefault();
}

bool Yf19AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void Yf19AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = juce::jmax (1, juce::jmin (2, getTotalNumOutputChannels()));

    // 4x, linear-phase FIR halfband with integer latency, so the dry path can be
    // delayed by exactly the same amount and BLEND never combs.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        static_cast<size_t> (numCh), 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    maxBlock = juce::jmax (1, samplesPerBlock);
    oversampler->initProcessing (static_cast<size_t> (maxBlock));
    latency = static_cast<int> (std::lround (oversampler->getLatencyInSamples()));
    setLatencySamples (latency);

    for (auto& c : channels)
        c.prepare (sampleRate * 4.0);

    dryDelay.setSize (2, latency + maxBlock + 1);
    dryDelay.clear();
    dryWrite = 0;

    mixSmoothed.reset (sampleRate, 0.03);
    levelSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (raw[static_cast<size_t> (ixMix)]->load());
    levelSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw[static_cast<size_t> (ixLevel)]->load(), -100.0f));
}

void Yf19AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
            handleController (m.getControllerNumber(), m.getControllerValue());
    }

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2, getTotalNumInputChannels());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numCh == 0 || numSamples == 0 || oversampler == nullptr)
        return;

    // Hosts occasionally send more samples than announced: process in prepared-size chunks.
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - start);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), numCh, start, n);
        processChunk (chunk, numCh);
    }
}

void Yf19AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int numCh)
{
    const int numSamples = buffer.getNumSamples();
    // input meter + the dry copy (written into the delay ring)
    float inPk = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        inPk = juce::jmax (inPk, buffer.getMagnitude (ch, 0, numSamples));
        int w = dryWrite;
        const float* src = buffer.getReadPointer (ch);
        float* ring = dryDelay.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            ring[w] = src[i];
            if (++w == dryDelay.getNumSamples()) w = 0;
        }
    }

    // filter at 4x
    moth::Settings s;
    s.cutoffHz = raw[static_cast<size_t> (ixCutoff)]->load();
    s.reso     = raw[static_cast<size_t> (ixReso)]->load();
    s.drive    = raw[static_cast<size_t> (ixDrive)]->load();
    s.highPass = raw[static_cast<size_t> (ixMode)]->load() >= 0.5f ? 1.0f : 0.0f;

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), static_cast<size_t> (numCh), static_cast<size_t> (numSamples));
    auto up = oversampler->processSamplesUp (block);
    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
    {
        float* d = up.getChannelPointer (ch);
        auto& filter = channels[ch];
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            d[i] = filter.process (d[i], s);
    }
    oversampler->processSamplesDown (block);

    // blend with the (latency-aligned) dry signal, then output level
    mixSmoothed.setTargetValue (raw[static_cast<size_t> (ixMix)]->load());
    levelSmoothed.setTargetValue (juce::Decibels::decibelsToGain (raw[static_cast<size_t> (ixLevel)]->load(), -100.0f));
    const int size = dryDelay.getNumSamples();
    int r = dryWrite - latency;
    while (r < 0) r += size;
    float outPk = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSmoothed.getNextValue();
        const float lvl = levelSmoothed.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float dry = dryDelay.getSample (ch, r);
            float y = (d[i] * mix + dry * (1.0f - mix)) * lvl;
            if (! std::isfinite (y)) y = 0.0f;
            d[i] = y;
            outPk = juce::jmax (outPk, std::abs (y));
        }
        if (++r == size) r = 0;
    }
    dryWrite = (dryWrite + numSamples) % size;

    inPeak.store (juce::jmax (inPeak.load (std::memory_order_relaxed), inPk), std::memory_order_relaxed);
    outPeak.store (juce::jmax (outPeak.load (std::memory_order_relaxed), outPk), std::memory_order_relaxed);
}

//==============================================================================
// MIDI CC learn (same behaviour as Bassline YF-21)
void Yf19AudioProcessor::handleController (int cc, int value)
{
    if (cc < 0 || cc >= numCcs)
        return;
    midiActivity.fetch_add (1, std::memory_order_relaxed);
    const int armed = learnCcParam.exchange (-1);
    if (armed >= 0 && armed < Params::numParams)
    {
        for (auto& c : ccToParam)
            if (c.load() == armed) c.store (-1);
        ccToParam[static_cast<size_t> (cc)].store (armed);
        lastLearnedCc.store (cc);
        ccLearnCount.fetch_add (1);
    }
    const int idx = ccToParam[static_cast<size_t> (cc)].load();
    if (idx >= 0 && idx < Params::numParams)
        paramList[static_cast<size_t> (idx)]->setValueNotifyingHost (static_cast<float> (value) / 127.0f);
}

int Yf19AudioProcessor::ccForParam (int paramIndex) const
{
    for (int cc = 0; cc < numCcs; ++cc)
        if (ccToParam[static_cast<size_t> (cc)].load() == paramIndex)
            return cc;
    return -1;
}

void Yf19AudioProcessor::clearCcForParam (int paramIndex)
{
    for (auto& c : ccToParam)
        if (c.load() == paramIndex) c.store (-1);
}

void Yf19AudioProcessor::clearAllCcs()
{
    for (auto& c : ccToParam) c.store (-1);
}

void Yf19AudioProcessor::resetCcsToDefault()
{
    clearAllCcs();
    for (const auto& d : Params::defaultCcs)
        ccToParam[static_cast<size_t> (d.cc)].store (paramIndexForId (d.id));
}

juce::String Yf19AudioProcessor::ccMapToString() const
{
    juce::StringArray items;
    for (int cc = 0; cc < numCcs; ++cc)
    {
        const int idx = ccToParam[static_cast<size_t> (cc)].load();
        if (idx >= 0 && idx < Params::numParams)
            items.add (juce::String (cc) + "=" + Params::specs[idx].id);
    }
    return items.joinIntoString (",");
}

void Yf19AudioProcessor::ccMapFromString (const juce::String& text)
{
    clearAllCcs();
    for (const auto& item : juce::StringArray::fromTokens (text, ",", ""))
    {
        const int cc = item.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const int idx = paramIndexForId (item.fromFirstOccurrenceOf ("=", false, false));
        if (cc >= 0 && cc < numCcs && idx >= 0 && item.containsChar ('='))
            ccToParam[static_cast<size_t> (cc)].store (idx);
    }
}

//==============================================================================
juce::AudioProcessorEditor* Yf19AudioProcessor::createEditor()
{
    return new Yf19AudioProcessorEditor (*this);
}

void Yf19AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree extra ("YF19Extra");
    extra.setProperty ("ccMap", ccMapToString(), nullptr);
    extra.setProperty ("uiScale", uiScale, nullptr);
    state.removeChild (state.getChildWithName ("YF19Extra"), nullptr);
    state.appendChild (extra, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Yf19AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    const auto extra = state.getChildWithName ("YF19Extra");
    state.removeChild (extra, nullptr);
    apvts.replaceState (state);
    if (extra.isValid())
    {
        ccMapFromString (extra.getProperty ("ccMap").toString());
        uiScale = juce::jlimit (0.75f, 1.75f, static_cast<float> (extra.getProperty ("uiScale", 1.0f)));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Yf19AudioProcessor();
}
