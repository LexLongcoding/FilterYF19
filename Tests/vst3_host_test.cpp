// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Loads the built .vst3 the way a DAW does, filters a bright signal, moves the cutoff
// with MIDI CC 74 and checks the output gets darker. Usage: YF19HostTest "path/to/Filter YF-19.vst3"

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: YF19HostTest <plugin.vst3>\n"); return 2; }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::String (argv[1]));
    if (found.isEmpty()) { std::printf ("FAIL  no plugin found in %s\n", argv[1]); return 1; }
    const auto& d = *found[0];
    std::printf ("found: %s by %s, category=%s, instrument=%d, inputs=%d outputs=%d\n", d.name.toRawUTF8(),
                 d.manufacturerName.toRawUTF8(), d.category.toRawUTF8(), d.isInstrument ? 1 : 0, d.numInputChannels, d.numOutputChannels);

    juce::String error;
    auto plugin = format.createInstanceFromDescription (d, 48000.0, 512, error);
    if (plugin == nullptr) { std::printf ("FAIL  couldn't load: %s\n", error.toRawUTF8()); return 1; }
    plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    std::printf ("parameters: %d, latency: %d samples, accepts MIDI: %d\n",
                 plugin->getParameters().size(), plugin->getLatencySamples(), plugin->acceptsMidi() ? 1 : 0);

    // a bright saw at 220 Hz; RMS over the second half of each phase
    auto runPhase = [&] (int ccValue, double& rmsOut)
    {
        juce::AudioBuffer<float> buf (2, 512);
        double sumSq = 0; long n = 0; bool finite = true;
        static long phase = 0;
        for (int b = 0; b < 120; ++b)
        {
            for (int i = 0; i < 512; ++i, ++phase)
            {
                const float saw = 0.3f * (2.0f * static_cast<float> (std::fmod (phase * 220.0 / 48000.0, 1.0)) - 1.0f);
                buf.setSample (0, i, saw); buf.setSample (1, i, saw);
            }
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, ccValue), 0);
            plugin->processBlock (buf, midi);
            if (b >= 60)
                for (int i = 0; i < 512; ++i) { const float v = buf.getSample (0, i); finite = finite && std::isfinite (v); sumSq += v * v; ++n; }
        }
        rmsOut = std::sqrt (sumSq / n);
        return finite;
    };
    double open = 0, closed = 0;
    const bool finite = runPhase (127, open) && runPhase (20, closed);
    std::printf ("rms with CC74=127: %.4f, with CC74=20: %.4f\n", open, closed);

    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
    const bool editorOk = ed != nullptr && ed->getWidth() > 300;
    std::printf ("editor: %dx%d\n", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
    ed.reset();
    plugin->releaseResources();

    const bool ok = finite && open > 0.1 && closed < open * 0.5 && ! d.isInstrument
                    && plugin->getTotalNumInputChannels() == 2 && state.getSize() > 100 && editorOk;
    std::printf ("%s  VST3 host test (effect, MIDI CC 74 darkens: %.1f dB)\n", ok ? "ok  " : "FAIL",
                 20.0 * std::log10 (closed / open));
    return ok ? 0 : 1;
}
