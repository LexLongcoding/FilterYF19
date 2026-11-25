// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Plugin-level tests: bus layouts, latency + BLEND null test, LP/HP through the full
// oversampled chain, LEVEL, MIDI CC defaults / learn / choice CCs, state round trip,
// odd block sizes, and editor screenshots. Usage: YF19Tests <output folder>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

namespace
{
    int failures = 0, checks = 0;
    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) ++failures;
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    }

    constexpr double fs = 48000.0;
    constexpr int block = 512;

    std::unique_ptr<Yf19AudioProcessor> fresh (int channels = 2)
    {
        auto p = std::make_unique<Yf19AudioProcessor>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (set);
        l.outputBuses.add (set);
        p->setBusesLayout (l);
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    void setParam (Yf19AudioProcessor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getParam (Yf19AudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

    // Runs a signal through, returns the left output. gen(i) gives the input sample.
    std::vector<float> run (Yf19AudioProcessor& p, int total, std::function<float (int)> gen,
                            int blockSize = block, std::vector<std::pair<int, juce::MidiMessage>> midiEvents = {})
    {
        std::vector<float> out;
        const int ch = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> buf (ch, blockSize);
        for (int t = 0; t < total; t += blockSize)
        {
            const int n = juce::jmin (blockSize, total - t);
            buf.setSize (ch, n, false, false, true);
            for (int c = 0; c < ch; ++c)
                for (int i = 0; i < n; ++i)
                    buf.setSample (c, i, gen (t + i));
            juce::MidiBuffer midi;
            for (auto& [at, m] : midiEvents)
                if (at >= t && at < t + n) midi.addEvent (m, at - t);
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i) out.push_back (buf.getSample (0, i));
        }
        return out;
    }

    std::function<float (int)> sine (double hz, float amp = 0.25f)
    {
        return [hz, amp] (int i) { return amp * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / fs)); };
    }

    double rmsDb (const std::vector<float>& v, size_t from)
    {
        double s = 0;
        for (size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
        return 10.0 * std::log10 (s / static_cast<double> (v.size() - from) + 1e-30);
    }

    // gain of a sine through the plugin, in dB
    double gainDb (Yf19AudioProcessor& p, double hz)
    {
        const auto out = run (p, static_cast<int> (fs * 0.6), sine (hz));
        std::vector<float> in;
        for (int i = 0; i < static_cast<int> (fs * 0.6); ++i) in.push_back (sine (hz) (i));
        return rmsDb (out, out.size() / 2) - rmsDb (in, in.size() / 2);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    // ---------------------------------------------------------------- layouts
    {
        Yf19AudioProcessor p;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (l), "stereo in/out supported");
        l.inputBuses.clear(); l.outputBuses.clear();
        l.inputBuses.add (juce::AudioChannelSet::mono()); l.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.checkBusesLayoutSupported (l), "mono in/out supported");
        l.outputBuses.clear(); l.outputBuses.add (juce::AudioChannelSet::stereo());
        check (! p.checkBusesLayoutSupported (l), "mono in / stereo out rejected");
        check (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "accepts MIDI (for CC control), audio effect");
    }

    // ---------------------------------------------------------------- latency + BLEND = 0 null test
    {
        auto p = fresh();
        const int lat = p->getLatencySamples();
        check (lat > 0 && lat < 200, "reports oversampler latency to the host (" + juce::String (lat) + " samples)");
        setParam (*p, "out_mix", 0.0f);
        setParam (*p, "flt_reso", 1.0f);   // even screaming, a fully dry blend must null
        p->prepareToPlay (fs, block);      // snap the smoothers
        auto gen = sine (1234.5, 0.5f);
        const auto out = run (*p, 48000, gen);
        double maxErr = 0;
        for (int i = lat + 10; i < 48000; ++i) maxErr = std::max (maxErr, static_cast<double> (std::abs (out[static_cast<size_t> (i)] - gen (i - lat))));
        check (maxErr < 1e-5, "BLEND 0%: output = input delayed by the reported latency (max err " + juce::String (maxErr, 8) + ")");
    }
    {
        // BLEND 50% with a wide-open low-pass, no reso: dry and wet are aligned, so no comb dip
        auto p = fresh();
        setParam (*p, "flt_cutoff", 20000.0f);
        setParam (*p, "flt_reso", 0.0f);
        setParam (*p, "flt_drive", 0.0f);
        setParam (*p, "out_mix", 0.5f);
        p->prepareToPlay (fs, block);
        bool flat = true;
        juce::String worst;
        for (double hz : { 100.0, 1000.0, 5000.0 })   // above ~5k the 20 kHz pole's own phase shift shows (not latency)
        {
            const double g = gainDb (*p, hz);
            if (std::abs (g) > 1.0) { flat = false; worst << hz << " Hz: " << juce::String (g, 2) << " dB  "; }
        }
        check (flat, "BLEND 50% with an open filter is flat (no phasing) " + worst);
    }

    // ---------------------------------------------------------------- LP / HP through the whole chain
    {
        auto p = fresh();
        setParam (*p, "flt_cutoff", 500.0f);
        setParam (*p, "flt_reso", 0.0f);
        setParam (*p, "flt_drive", 0.0f);
        p->prepareToPlay (fs, block);
        const double pass = gainDb (*p, 80), stop = gainDb (*p, 5000);
        check (std::abs (pass) < 1.0, "LP 500 Hz: 80 Hz passes (" + juce::String (pass, 2) + " dB)");
        check (stop < -30.0, "LP 500 Hz: 5 kHz cut (" + juce::String (stop, 1) + " dB)");

        setParam (*p, "flt_mode", 1.0f);
        p->prepareToPlay (fs, block);
        const double hpPass = gainDb (*p, 8000), hpStop = gainDb (*p, 60);
        check (std::abs (hpPass) < 1.0, "HP 500 Hz: 8 kHz passes (" + juce::String (hpPass, 2) + " dB)");
        check (hpStop < -25.0, "HP 500 Hz: 60 Hz cut (" + juce::String (hpStop, 1) + " dB)");
    }
    {
        auto p = fresh();
        setParam (*p, "flt_cutoff", 20000.0f);
        setParam (*p, "flt_reso", 0.0f);
        setParam (*p, "flt_drive", 0.0f);
        setParam (*p, "out_level", -6.0f);
        p->prepareToPlay (fs, block);
        const double g = gainDb (*p, 300);
        check (std::abs (g + 6.0) < 0.5, "LEVEL -6 dB (" + juce::String (g, 2) + " dB)");
    }
    {
        auto p = fresh();
        setParam (*p, "flt_reso", 0.5f);
        const auto out = run (*p, 48000, [] (int) { return 0.0f; });
        double pk = 0;
        for (auto v : out) pk = std::max (pk, static_cast<double> (std::abs (v)));
        check (pk == 0.0, "silence in, moderate RESO -> silence out");
    }
    {
        auto p = fresh (1);
        setParam (*p, "flt_cutoff", 400.0f);
        const auto out = run (*p, 24000, sine (6000));
        check (rmsDb (out, 12000) < -35.0, "mono instance filters");
    }
    {
        // the host sends blocks of odd sizes, including bigger than announced
        auto p = fresh();
        bool finite = true;
        for (int bs : { 1, 7, 333, 512, 2048, 64 })
        {
            const auto out = run (*p, bs * 3, sine (220), bs);
            for (auto v : out) finite = finite && std::isfinite (v);
        }
        check (finite, "odd and oversize block sizes are handled");
    }

    // ---------------------------------------------------------------- MIDI
    {
        auto p = fresh();
        check (p->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_cutoff")) == 74, "fresh instance: CC 74 -> cutoff");
        check (p->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_reso")) == 71, "fresh instance: CC 71 -> resonance");

        run (*p, 512, [] (int) { return 0.0f; }, 512, { { 10, juce::MidiMessage::controllerEvent (1, 74, 0) },
                                                        { 20, juce::MidiMessage::controllerEvent (1, 71, 127) } });
        check (std::abs (getParam (*p, "flt_cutoff") - 20.0f) < 0.5f, "CC 74 = 0 -> cutoff 20 Hz");
        check (getParam (*p, "flt_reso") > 0.99f, "CC 71 = 127 -> resonance 100%");
        run (*p, 512, [] (int) { return 0.0f; }, 512, { { 0, juce::MidiMessage::controllerEvent (1, 74, 127) } });
        check (getParam (*p, "flt_cutoff") > 19900.0f, "CC 74 = 127 -> cutoff 20 kHz");

        // learn: arm DRIVE, the next CC is assigned
        p->armCcLearn (Yf19AudioProcessor::paramIndexForId ("flt_drive"));
        run (*p, 512, [] (int) { return 0.0f; }, 512, { { 0, juce::MidiMessage::controllerEvent (3, 21, 64) } });
        check (p->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_drive")) == 21, "learn: DRIVE <- CC 21");
        check (std::abs (getParam (*p, "flt_drive") - 64.0f / 127.0f) < 0.01f, "learned CC moves DRIVE immediately");

        // the switch: learn, then low values = LP, high = HP
        p->armCcLearn (Yf19AudioProcessor::paramIndexForId ("flt_mode"));
        run (*p, 512, [] (int) { return 0.0f; }, 512, { { 0, juce::MidiMessage::controllerEvent (1, 80, 127) } });
        check (getParam (*p, "flt_mode") >= 0.5f, "mode switch CC 80 = 127 -> HP");
        run (*p, 512, [] (int) { return 0.0f; }, 512, { { 0, juce::MidiMessage::controllerEvent (1, 80, 10) } });
        check (getParam (*p, "flt_mode") < 0.5f, "mode switch CC 80 = 10 -> LP");

        // re-learning CC 74 for BLEND takes it away from cutoff
        p->armCcLearn (Yf19AudioProcessor::paramIndexForId ("out_mix"));
        run (*p, 512, [] (int) { return 0.0f; }, 512, { { 0, juce::MidiMessage::controllerEvent (1, 74, 32) } });
        check (p->ccForParam (Yf19AudioProcessor::paramIndexForId ("out_mix")) == 74
               && p->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_cutoff")) == -1, "a CC drives one parameter at a time");

        // state round trip: parameters + CC map + window scale
        setParam (*p, "flt_cutoff", 777.0f);
        p->uiScale = 1.25f;
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = fresh();
        q->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (std::abs (getParam (*q, "flt_cutoff") - 777.0f) < 1.0f && getParam (*q, "flt_mode") < 0.5f,
               "state restores parameters");
        check (q->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_drive")) == 21
               && q->ccForParam (Yf19AudioProcessor::paramIndexForId ("out_mix")) == 74
               && q->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_cutoff")) == -1, "state restores the CC map");
        check (std::abs (q->uiScale - 1.25f) < 0.001f, "state restores the window scale");
        q->resetCcsToDefault();
        check (q->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_cutoff")) == 74
               && q->ccForParam (Yf19AudioProcessor::paramIndexForId ("flt_drive")) == -1, "reset CC map to defaults");
    }

    // ---------------------------------------------------------------- editor screenshots
    auto shoot = [&] (Yf19AudioProcessor& p, juce::AudioProcessorEditor& ed, const char* name)
    {
        for (int i = 0; i < 4; ++i)
        {
            run (p, 2400, sine (110, 0.4f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        return f.getSize() > 10000;
    };
    {
        auto p = fresh();
        setParam (*p, "flt_cutoff", 1200.0f);
        setParam (*p, "flt_reso", 0.72f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (Theme::designWidth * 3 / 2, Theme::designHeight * 3 / 2);
        check (shoot (*p, *ed, "editor.png"), "editor screenshot (LP)");

        setParam (*p, "flt_mode", 1.0f);
        setParam (*p, "flt_cutoff", 350.0f);
        setParam (*p, "out_mix", 0.6f);
        check (shoot (*p, *ed, "editor_hp.png"), "editor screenshot (HP, blend 60%)");

        if (auto* e = dynamic_cast<Yf19AudioProcessorEditor*> (ed.get()))
        {
            e->setLearnMode (true);
            check (shoot (*p, *ed, "editor_learn.png"), "editor screenshot (MIDI learn)");
        }
        ed.reset();
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
