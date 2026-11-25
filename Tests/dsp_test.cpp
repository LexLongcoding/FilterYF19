// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// JUCE-free checks of the filter core.  g++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp -o dsp_test

#include "MothFilter.h"
#include <cstdio>
#include <vector>
#include <algorithm>

static int failures = 0;
static void check (bool ok, const char* what, double v = 0.0)
{
    std::printf ("%s  %-58s %9.3f\n", ok ? "ok  " : "FAIL", what, v);
    if (! ok) ++failures;
}

constexpr double fs = 192000.0;

// steady-state gain (dB) of a sine through the filter
static double gainDb (float cutoff, float reso, bool hp, double freq, float drive = 0.0f, float amp = 0.1f, bool linear = true)
{
    moth::Korg35 f;
    f.setSampleRate (fs);
    f.set (cutoff, moth::resoToK (reso), drive, linear);
    const int n = static_cast<int> (fs * 0.5);
    double inSq = 0, outSq = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = amp * static_cast<float> (std::sin (2.0 * moth::pi * freq * i / fs));
        float lo, hi;
        f.process (x, lo, hi);
        if (i > n / 2) { inSq += x * x; const float y = hp ? hi : lo; outSq += y * y; }
    }
    return 10.0 * std::log10 (outSq / inSq);
}

// frequency of the strongest response (coarse sweep)
static double peakFreq (float cutoff, float reso, bool hp)
{
    double best = -1e9, bestF = 0;
    for (double f = cutoff * 0.5; f < cutoff * 2.0; f *= 1.02)
    {
        const double g = gainDb (cutoff, reso, hp, f);
        if (g > best) { best = g; bestF = f; }
    }
    return bestF;
}

int main()
{
    // ---- low-pass shape
    check (std::abs (gainDb (1000, 0.0f, false, 50)) < 0.5, "LP passband (50 Hz @ fc 1k, no reso) ~0 dB", gainDb (1000, 0.0f, false, 50));
    const double lp4k = gainDb (1000, 0.0f, false, 4000), lp8k = gainDb (1000, 0.0f, false, 8000);
    check (lp8k - lp4k < -10.5 && lp8k - lp4k > -13.5, "LP slope 4k->8k ~ -12 dB/oct", lp8k - lp4k);
    // ---- high-pass shape
    check (std::abs (gainDb (1000, 0.0f, true, 15000)) < 0.6, "HP passband (15 kHz @ fc 1k) ~0 dB", gainDb (1000, 0.0f, true, 15000));
    const double hp250 = gainDb (1000, 0.0f, true, 250), hp125 = gainDb (1000, 0.0f, true, 125);
    check (hp125 - hp250 < -10.5 && hp125 - hp250 > -13.5, "HP slope 250->125 ~ -12 dB/oct", hp125 - hp250);

    // ---- resonance
    const double peakLo = gainDb (1000, 0.0f, false, 1000), peakHi = gainDb (1000, 0.85f, false, 1000);
    check (peakHi - peakLo > 9.0, "RESO 85% lifts the LP peak > 9 dB", peakHi - peakLo);
    const double pf = peakFreq (1000, 0.85f, false);
    check (pf > 900 && pf < 1100, "LP resonant peak sits at the cutoff (Hz)", pf);
    const double pfh = peakFreq (2000, 0.85f, true);
    check (pfh > 1800 && pfh < 2200, "HP resonant peak sits at the cutoff (Hz)", pfh);
    check (gainDb (1000, 0.3f, false, 1000) - peakLo < 4.0, "RESO 30% is a gentle lift (< 4 dB, pad friendly)", gainDb (1000, 0.3f, false, 1000) - peakLo);
    const double bassLoss = gainDb (1000, 0.85f, false, 50);
    check (bassLoss > -7.0, "LP bass at high RESO stays within 7 dB (MS-20 thins a bit)", bassLoss);

    // ---- self-oscillation: ping it, then silence. Bounded and sustained at max, decays at 80%.
    auto ringAfter = [] (float reso, float drive, bool hp = false)
    {
        moth::Korg35 f;
        f.setSampleRate (fs);
        f.set (800, moth::resoToK (reso), drive);
        double tailPeak = 0;
        bool finite = true;
        for (int i = 0; i < static_cast<int> (fs * 2); ++i)
        {
            float lo, hi;
            f.process (i < 20 ? 0.5f : 0.0f, lo, hi);
            finite = finite && std::isfinite (lo) && std::isfinite (hi);
            if (i > fs * 1.5) tailPeak = std::max (tailPeak, static_cast<double> (std::abs (hp ? hi : lo)));
        }
        return finite ? tailPeak : 1e9;
    };
    const double selfOsc = ringAfter (1.0f, 0.0f);
    check (selfOsc > 0.05 && selfOsc < 4.0, "RESO 100% self-oscillates at a bounded level", selfOsc);
    check (ringAfter (0.8f, 0.0f) < 1e-3, "RESO 80% rings out (no self-oscillation)", ringAfter (0.8f, 0.0f));
    check (ringAfter (1.0f, 0.0f, true) > 0.05 && ringAfter (1.0f, 0.0f, true) < 4.0, "HP mode self-oscillates too, bounded", ringAfter (1.0f, 0.0f, true));
    check (ringAfter (1.0f, 1.0f) < 4.0, "RESO 100% + DRIVE 100% stays bounded", ringAfter (1.0f, 1.0f));

    // ---- saturation: clean at normal levels with DRIVE 0, audible with DRIVE up
    const double cleanLin = gainDb (1000, 0.5f, false, 200, 0.0f, 0.25f, true);
    const double cleanSat = gainDb (1000, 0.5f, false, 200, 0.0f, 0.25f, false);
    check (std::abs (cleanLin - cleanSat) < 0.2, "DRIVE 0: -12 dBFS input passes ~clean", cleanLin - cleanSat);
    const double hotSat = gainDb (1000, 0.95f, false, 1000, 1.0f, 0.5f, false);
    const double hotLin = gainDb (1000, 0.95f, false, 1000, 1.0f, 0.5f, true);
    check (hotLin - hotSat > 3.0, "DRIVE 100% + high RESO: the loop compresses the peak (dB)", hotLin - hotSat);

    // ---- abuse: loud noise, extreme settings, sweeping fast
    {
        moth::Channel ch;
        ch.prepare (fs);
        moth::Settings s;
        unsigned seed = 1;
        bool finite = true;
        double peak = 0;
        for (int i = 0; i < static_cast<int> (fs * 2); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float noise = (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f) * 8.0f;   // +-8 (+18 dBFS)
            s.cutoffHz = (i / 2000) % 2 ? 20000.0f : 20.0f;
            s.reso = 1.0f; s.drive = 1.0f; s.highPass = (i / 30000) % 2 ? 1.0f : 0.0f;
            const float y = ch.process (noise, s);
            finite = finite && std::isfinite (y);
            peak = std::max (peak, static_cast<double> (std::abs (y)));
        }
        check (finite && peak < 20.0, "+18 dBFS noise, max RESO/DRIVE, square-wave sweeps: finite", peak);
    }

    // ---- mode switch is click-free: max sample-to-sample jump while switching a 100 Hz sine
    {
        moth::Channel ch;
        ch.prepare (fs);
        moth::Settings s;
        s.cutoffHz = 500.0f; s.reso = 0.4f; s.drive = 0.0f;
        float prev = 0; double maxJump = 0;
        for (int i = 0; i < static_cast<int> (fs); ++i)
        {
            s.highPass = i > fs / 2 ? 1.0f : 0.0f;
            const float y = ch.process (0.5f * static_cast<float> (std::sin (2.0 * moth::pi * 100.0 * i / fs)), s);
            if (i > 1000) maxJump = std::max (maxJump, static_cast<double> (std::abs (y - prev)));
            prev = y;
        }
        check (maxJump < 0.01, "LP->HP switch has no step (max jump per sample)", maxJump);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
