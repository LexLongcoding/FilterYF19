// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "MothFilter.h"
#include <array>
#include <atomic>

// Insert effect: stereo (or mono) audio in -> filter at 4x -> blend with the dry signal
// (latency-aligned) -> output level. MIDI input is used for CC control of every parameter.
class Yf19AudioProcessor : public juce::AudioProcessor
{
public:
    Yf19AudioProcessor();
    ~Yf19AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static int paramIndexForId (const juce::String& id);

    // MIDI CC learn. Params are addressed by their index in Params::specs.
    void armCcLearn (int paramIndex)    { learnCcParam.store (paramIndex); }   // -1 cancels
    int  getArmedCcParam() const        { return learnCcParam.load(); }
    int  getCcLearnCount() const        { return ccLearnCount.load(); }
    int  getLastLearnedCc() const       { return lastLearnedCc.load(); }
    int  ccForParam (int paramIndex) const;
    void clearCcForParam (int paramIndex);
    void clearAllCcs();
    void resetCcsToDefault();
    int  getMidiActivity() const        { return midiActivity.load(); }   // bumps on every CC received

    // meters (peak since the last read, linear)
    float takeInputPeak()  { return inPeak.exchange (0.0f); }
    float takeOutputPeak() { return outPeak.exchange (0.0f); }

    juce::AudioProcessorValueTreeState apvts;
    float uiScale = 1.0f;   // editor window scale, saved with the project

    // exposed for tests
    void handleController (int cc, int value);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processChunk (juce::AudioBuffer<float>&, int numCh);
    juce::String ccMapToString() const;
    void ccMapFromString (const juce::String&);

    static constexpr int numCcs = 120;
    std::array<juce::RangedAudioParameter*, Params::numParams> paramList {};
    std::array<std::atomic<float>*, Params::numParams> raw {};
    int ixMode = 0, ixCutoff = 0, ixReso = 0, ixDrive = 0, ixMix = 0, ixLevel = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<moth::Channel, 2> channels;
    juce::AudioBuffer<float> dryDelay;   // ring buffer that delays the dry path by the oversampler's latency
    int dryWrite = 0, latency = 0, maxBlock = 512;
    juce::SmoothedValue<float> mixSmoothed, levelSmoothed;

    std::atomic<int> learnCcParam { -1 }, ccLearnCount { 0 }, lastLearnedCc { -1 }, midiActivity { 0 };
    std::array<std::atomic<int>, numCcs> ccToParam;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Yf19AudioProcessor)
};
