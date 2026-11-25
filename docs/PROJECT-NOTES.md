# Filter YF-19 - project notes (for contributors and for resuming work)

Created 2026-11-25, version 0.1.0. License: AGPL-3.0-or-later, copyright LexLongcoding.
Sister projects: Bassline YF-21 (synth), Decu6000 (drums) - same fonts, colours, knob drawing, LCD, MIDI learn.

## What it is
Stereo/mono insert filter, VST3 + Standalone, JUCE 8.0.4 (fetched by CMake). Owner's brief: HP/LP switch, cutoff,
resonance, level, blend; MIDI for every parameter; based on the Blue Lantern Spacemoth v2; good on pads; fixes
Spire patches whose own filter doesn't respond as expected (it goes in the FX section, after the synth's effects).

## Design decisions (and why)
- **Circuit**: Spacemoth v2 = same core as the v1 Spacemoth, described as MS-20-like, LM13700 OTA, with an HP mode and
  line-level I/O. Modelled as a ZDF/TPT Korg-35 (Zavalishin / Pirkle), LP: LPF1 -> loop(LPF2 fwd, HPF3 fb);
  HP is the dual: HPF1 -> loop(HPF2 fwd, LPF3 fb). Both are run every sample so the switch can crossfade (12 ms,
  equal power). Saturation (scaled Pade tanh) is on the loop node u, so resonance compresses and self-oscillation
  is bounded. K = 2 is the linear self-oscillation edge; RESO maps 0..1 to K 0.01..2.08 with a cubic tail so the
  lower half is gentle (pads).
- **DRIVE** = input gain + lower clipper headroom (the module's trimmers). At 0, -12 dBFS passes clean (< 0.03 dB).
- **4x oversampling**, linear-phase FIR halfband with integer latency (61 samples at 48 kHz, reported to the host).
  The dry path is delayed by the same amount, so BLEND 0 % nulls exactly. (IIR oversampling would phase the blend.)
- **Smoothing** at the oversampled rate: cutoff in log2 space (25 ms), K and drive (30 ms); blend / level 30 ms.
  Coefficients refreshed every 4th oversampled sample.
- Host blocks larger than announced are processed in prepared-size chunks.
- **MIDI**: same CC learn as YF-21 (`yfParam` component property, overlay, right-click menu), plus defaults
  CC 74 cutoff / CC 71 reso for fresh instances, a CC button with the map / reset / clear, and a MIDI activity light.
  In VST3, JUCE delivers CCs through hidden parameters and turns them back into MIDI - verified in the host test.
- No patch system (DAW presets cover six knobs). Canvas 820x520, scaled 0.75x-1.75x, saved with the project.

## Source map
| File | Role |
|---|---|
| `Source/MothFilter.h` | Filter core (Korg-35 LP/HP, saturation, smoothing, crossfade). JUCE-free. |
| `Source/Parameters.h` | The 6 parameters + default CCs |
| `Source/PluginProcessor.*` | Oversampling, latency-aligned blend, level, meters, CC learn, state |
| `Source/PluginEditor.*` | Header, panels, layout |
| `Source/UI/Theme.*` | From YF-21 (colours, fonts, knob / panel / LCD drawing) |
| `Source/UI/Widgets.*` | Knob, ModeSwitch (bat switch), LcdDisplay, LevelMeter, ResponseDisplay, LearnOverlay |

## Testing (all passing 2026-11-25, Linux Release)
- `YF19DspTest` - 17 checks: slopes, peak at cutoff, gentle low reso, bass at high reso, self-oscillation (LP + HP),
  clean at DRIVE 0, abuse, click-free switch.
- `YF19Tests` - 32 checks: layouts, latency + null test, LP/HP through the chain, level, odd blocks, CC defaults /
  learn / switch CCs / one-CC-one-param, state, screenshots.
- `YF19HostTest` - loads the .vst3 as an effect, CC 74 darkens a saw by ~40 dB, state, editor.
- Headless: `-DYF19_HEADLESS_TEST_BUILD=ON` and
  `CXXFLAGS="-DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XRENDER=0"`.

## Not yet
- Windows build run (CI workflow included), a real DAW session with Spire, ears on real pads.

## Ideas
- Key tracking from MIDI notes, an envelope follower / LFO on cutoff, the v3's ping input, 24 dB mode.
