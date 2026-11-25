// Filter YF-19 - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <array>

// Single source of truth for every parameter (same scheme as Bassline YF-21). The processor
// builds its APVTS layout from this table; the editor uses it for labels; the MIDI CC map
// stores parameters by these IDs.
namespace Params
{
    enum class Unit { Percent, Hz, Db, Choice };

    struct Spec
    {
        const char* id;      // APVTS parameter ID (also the CC-map key)
        const char* name;    // full name shown to the host
        const char* knob;    // short label under the control
        float min, max;
        float centre;        // skew centre; <= 0 means linear
        float def;
        Unit unit;
        const char* choices; // Choice: "A|B" (min 0, max n-1)
    };

    struct Group { const char* prefix; const char* title; };
    constexpr std::array<Group, 2> groups {{ { "flt", "FILTER" }, { "out", "OUTPUT" } }};

    using U = Unit;
    constexpr Spec specs[] = {
        { "flt_mode",   "Filter Mode",      "MODE",   0, 1, 0, 0, U::Choice, "LOW PASS|HIGH PASS" },
        { "flt_cutoff", "Filter Cutoff",    "CUTOFF", 20.0f, 20000.0f, 900.0f, 2000.0f, U::Hz, nullptr },
        { "flt_reso",   "Filter Resonance", "RESO",   0, 1, 0, 0.25f, U::Percent, nullptr },
        { "flt_drive",  "Filter Drive",     "DRIVE",  0, 1, 0, 0.15f, U::Percent, nullptr },
        { "out_mix",    "Blend (Dry/Wet)",  "BLEND",  0, 1, 0, 1.0f, U::Percent, nullptr },
        { "out_level",  "Output Level",     "LEVEL",  -24.0f, 12.0f, 0, 0.0f, U::Db, nullptr },
    };

    constexpr int numParams = static_cast<int> (sizeof (specs) / sizeof (specs[0]));

    // Fresh instances answer the two standard "brightness / timbre" CCs right away.
    // MIDI learn changes or clears these; the map is saved with the project.
    struct DefaultCc { int cc; const char* id; };
    constexpr DefaultCc defaultCcs[] = { { 74, "flt_cutoff" }, { 71, "flt_reso" } };
}
