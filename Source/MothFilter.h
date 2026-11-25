// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <cmath>
#include <initializer_list>

// The YF-19 filter core. JUCE-free so it can be unit tested on its own.
//
// Modelled on the circuit family of the Blue Lantern Spacemoth v2: an MS-20 style
// 12 dB/oct OTA filter (Korg-35 topology) with a switchable low-pass / high-pass
// output. The character comes from the saturation INSIDE the resonance loop: as
// RESO rises the feedback starts to clip, so the peak grows teeth and screams
// instead of just getting louder, and near the top it self-oscillates at a
// bounded level. DRIVE raises the level into that loop (the hardware's trimmers,
// which ship "tuned to barely start saturating").
//
// Implementation: zero-delay-feedback (TPT) Korg-35 after Zavalishin / Pirkle,
// solved per sample, run at the oversampled rate by the processor.
namespace moth
{
    constexpr double pi = 3.141592653589793;

    // Pade tanh, accurate to ~0.1 % below |x| = 3, exact clamp beyond.
    inline float fastTanh (float x)
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Trapezoidal one-pole, the building block of the Korg-35.
    struct OnePole
    {
        float alpha = 0.0f, beta = 0.0f, z = 0.0f;
        float lp (float x) { const float v = (x - z) * alpha; const float y = v + z; z = y + v; return y; }
        float hp (float x) { return x - lp (x); }
        float feedback() const { return beta * z; }
        void reset() { z = 0.0f; }
    };

    // Map RESO 0..1 to the loop gain K. K = 2 is the edge of self-oscillation for
    // the linear filter; the saturation lets it go a little past that and stay bounded.
    inline float resoToK (float reso)
    {
        reso = reso < 0.0f ? 0.0f : (reso > 1.0f ? 1.0f : reso);
        // gentle at the bottom (pads want a soft lift, not a whistle), steep near the top
        const float shaped = 0.55f * reso + 0.45f * reso * reso * reso;
        return 0.01f + shaped * 2.07f;
    }

    // One channel of the filter: both outputs are computed every sample so the
    // LP/HP switch can crossfade without a click and without a cold start.
    class Korg35
    {
    public:
        void setSampleRate (double fs) { sampleRate = fs; reset(); }
        void reset()
        {
            for (auto* p : { &lp1, &lp2, &hp3, &hp1b, &hp2b, &lp3b }) p->reset();
        }

        // cutoffHz, K (from resoToK), drive 0..1. Cheap enough to call every sample.
        void set (float cutoffHz, float k, float drive, bool linear = false)
        {
            const float maxFc = static_cast<float> (sampleRate) * 0.42f;
            const float fc = cutoffHz < 10.0f ? 10.0f : (cutoffHz > maxFc ? maxFc : cutoffHz);
            const float g = static_cast<float> (std::tan (pi * fc / sampleRate));
            const float G = g / (1.0f + g);
            K = k;
            isLinear = linear;

            for (auto* p : { &lp1, &lp2, &hp3, &hp1b, &hp2b, &lp3b }) p->alpha = G;
            // low-pass structure
            lp2.beta = (K - K * G) / (1.0f + g);
            hp3.beta = -1.0f / (1.0f + g);
            // high-pass structure (the dual): y = K HPF2(u), u = y1 + LPF3(y). Solving the
            // loop gives the same alpha0, with state terms -K G (1-G) z2 + (1-G) z3.
            hp2b.beta = -K * G / (1.0f + g);
            lp3b.beta = 1.0f / (1.0f + g);
            alpha0 = 1.0f / (1.0f - K * G + K * G * G);

            // Headroom of the loop's clipper: at DRIVE 0 it only starts to bite around
            // +-2 (so normal mix levels pass clean); DRIVE pushes the signal into it.
            inGain  = 1.0f + drive * drive * 7.0f;
            satHead = 2.2f - drive * 1.2f;
            outGain = 1.0f / (1.0f + drive * 1.6f);
        }

        // Process one sample; writes the low-pass and high-pass outputs.
        void process (float x, float& lowOut, float& highOut)
        {
            x *= inGain;

            // ---- low-pass: LPF1 -> (loop: LPF2, HPF3 feedback)
            {
                const float y1 = lp1.lp (x);
                const float s35 = hp3.feedback() + lp2.feedback();
                float u = alpha0 * (y1 + s35);
                if (! isLinear) u = clip (u);
                const float y = K * lp2.lp (u);
                hp3.hp (y);
                lowOut = (K > 0.0f ? y / K : y) * outGain;
            }
            // ---- high-pass: HPF1 -> (loop: HPF2 forward, LPF3 feedback)
            {
                const float y1 = hp1b.hp (x);
                const float s35 = hp2b.feedback() + lp3b.feedback();
                float u = alpha0 * (y1 + s35);
                if (! isLinear) u = clip (u);
                const float y = K * hp2b.hp (u);
                lp3b.lp (y);
                highOut = (K > 0.0f ? y / K : y) * outGain;
            }
        }

    private:
        float clip (float v) const { return satHead * fastTanh (v / satHead); }

        double sampleRate = 192000.0;
        OnePole lp1, lp2, hp3;      // low-pass structure
        OnePole hp1b, hp2b, lp3b;   // high-pass structure
        float K = 0.01f, alpha0 = 1.0f, inGain = 1.0f, outGain = 1.0f, satHead = 2.2f;
        bool isLinear = false;
    };

    // One-pole smoother, used for parameters at the oversampled rate.
    struct Smoother
    {
        float value = 0.0f, coeff = 0.0f;
        void setTime (double seconds, double fs) { coeff = static_cast<float> (std::exp (-1.0 / (seconds * fs))); }
        void snap (float v) { value = v; }
        float next (float target) { value = target + coeff * (value - target); return value; }
    };

    // Stereo-agnostic channel: filter + smoothed controls. The processor owns one per channel
    // and drives them with identical targets.
    struct Settings
    {
        float cutoffHz = 1200.0f;
        float reso = 0.3f;     // 0..1
        float drive = 0.1f;    // 0..1
        float highPass = 0.0f; // 0 = LP, 1 = HP (crossfaded)
    };

    class Channel
    {
    public:
        void prepare (double fs)
        {
            sampleRate = fs;
            filter.setSampleRate (fs);
            // cutoff in log2 space so sweeps (and stepped 7-bit MIDI CCs) glide evenly
            logCut.setTime (0.025, fs);
            k.setTime (0.03, fs);
            drv.setTime (0.03, fs);
            mode.setTime (0.012, fs);
            primed = false;
        }

        void reset() { filter.reset(); primed = false; }

        float process (float x, const Settings& s)
        {
            const float targetLog = std::log2 (s.cutoffHz < 10.0f ? 10.0f : s.cutoffHz);
            const float targetK = resoToK (s.reso);
            if (! primed)
            {
                logCut.snap (targetLog); k.snap (targetK); drv.snap (s.drive); mode.snap (s.highPass);
                primed = true;
            }
            const float lc = logCut.next (targetLog);
            const float kk = k.next (targetK);
            const float dd = drv.next (s.drive);
            const float mm = mode.next (s.highPass);

            // refresh coefficients every 4th sample (inaudible at 4x, saves the tan())
            if ((counter++ & 3) == 0)
                filter.set (std::exp2 (lc), kk, dd);

            float lo, hi;
            filter.process (x, lo, hi);
            // equal-power crossfade so the switch doesn't dip in level
            const float a = std::sqrt (1.0f - mm), b = std::sqrt (mm);
            return lo * a + hi * b;
        }

    private:
        double sampleRate = 192000.0;
        Korg35 filter;
        Smoother logCut, k, drv, mode;
        unsigned counter = 0;
        bool primed = false;
    };
}
