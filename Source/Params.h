// Paramètres du plugin « Audio Lab Phase » (polarité et décalage temporel, canaux G / D avec Link)
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PhaseCore.h"

namespace audiolab::params
{
inline constexpr const char* delL   = "delL";     // décalage G en ms (−10 … +10)
inline constexpr const char* delR   = "delR";     // décalage D en ms
inline constexpr const char* polL   = "polL";     // polarité G inversée
inline constexpr const char* polR   = "polR";     // polarité D inversée
inline constexpr const char* link   = "link";     // G et D réglés ensemble
inline constexpr const char* unit   = "unit";     // 0 = Échantillons, 1 = ms
inline constexpr const char* bypass = "bypass";

inline constexpr float maxMs = (float) PhaseCore::maxMs;

// ── Mise en forme des valeurs (virgule décimale, vrai signe moins) ──
inline juce::String num (double v, int decimals)
{
    auto s = juce::String (std::abs (v), decimals);
    return s;
}
inline juce::String sign (double v) { return v > 0 ? "+" : v < 0 ? juce::String::fromUTF8 ("−") : juce::String(); }

// « +1,250 ms »
inline juce::String fmtMs (double ms, bool withUnit = true)
{
    const double r = std::round (ms * 1000.0) / 1000.0;
    return sign (r) + num (r, 3) + (withUnit ? " ms" : "");
}
// « +48 smp » ; fractionnaire : « ≈ 55.13 smp »
inline juce::String fmtSamples (double s, bool withUnit = true)
{
    const double r = std::round (s);
    const juce::String u = withUnit ? juce::String (" smp") : juce::String();
    if (std::abs (s - r) < 1e-6) return sign (r) + num (r, 0) + u;
    return juce::String::fromUTF8 ("≈ ") + sign (s) + num (s, 2) + u;
}
// Distance parcourue par le son pendant ce temps (343 m/s, air à 20 °C)
inline juce::String fmtDist (double ms)
{
    const double cm = std::abs (ms) * 34.3;
    if (cm < 9.95) return num (cm, 1) + " cm";
    if (cm < 99.5) return num (std::round (cm), 0) + " cm";
    return num (cm / 100.0, 2) + " m";
}
// Fréquence : « 500 Hz », « 1,00 kHz », « 12,5 kHz »
inline juce::String fmtHz (double f)
{
    if (f < 99.95) return num (f, 1) + " Hz";
    if (f < 999.5) return num (std::round (f), 0) + " Hz";
    if (f < 9995) return num (f / 1000.0, 2) + " kHz";
    return num (f / 1000.0, 1) + " kHz";
}

// Lecture d'une valeur tapée : « 1,5 », « 1.5 ms », « 72 éch », « 250 µs », « 34 cm », « 1,2 m ».
// Sans unité : unité courante. Renvoie false si illisible.
inline bool parseValue (juce::String t, bool samplesUnit, double fs, double& msOut)
{
    t = t.trim().toLowerCase().replaceCharacter (',', '.').replace (juce::String::fromUTF8 ("−"), "-").removeCharacters (" ");
    if (t.isEmpty()) return false;
    int i = 0;
    if (t[0] == '+' || t[0] == '-') ++i;
    bool digits = false;
    while (i < t.length() && (juce::CharacterFunctions::isDigit (t[i]) || t[i] == '.')) { digits |= juce::CharacterFunctions::isDigit (t[i]); ++i; }
    if (! digits) return false;
    const double v = t.substring (0, i).getDoubleValue();
    const auto u = t.substring (i);
    if (u.isEmpty()) msOut = samplesUnit ? v * 1000.0 / fs : v;
    else if (u == "ms") msOut = v;
    else if (u == juce::String::fromUTF8 ("µs") || u == "us") msOut = v / 1000.0;
    else if (u == "s") msOut = v * 1000.0;
    else if (u == "cm") msOut = v / 34.3;
    else if (u == "m") msOut = v * 100.0 / 34.3;
    else if (u.startsWith (juce::String::fromUTF8 ("éch")) || u.startsWith ("ech") || u.startsWith ("smp") || u.startsWith ("sample") || u == "spl")
        msOut = v * 1000.0 / fs;
    else return false;
    msOut = juce::jlimit (-(double) maxMs, (double) maxMs, msOut);
    return true;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto delay = [&] (const char* id, const char* name)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, String::fromUTF8 (name),
            NormalisableRange<float> (-maxMs, maxMs), 0.0f,
            AudioParameterFloatAttributes().withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return fmtMs (v); })
                .withValueFromStringFunction ([] (const String& s) { double ms = 0; return parseValue (s, false, 48000, ms) ? (float) ms : 0.0f; })));
    };
    delay (delL, "Offset L");
    delay (delR, "Offset R");
    // choix à deux valeurs plutôt que « bool » : valeur toujours exactement 0 ou 1 (rappel de session exact)
    const StringArray pol { "Normal", "Inverted" };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { polL, 1 }, "Polarity L", pol, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { polR, 1 }, "Polarity R", pol, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { link, 1 }, "Link L/R", StringArray { "Off", "On" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { unit, 1 }, "Unit",
                                                   StringArray { "Samples", "ms" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));
    return l;
}

// Presets d'usage, proposés comme programmes « usine » au logiciel hôte (l'unité n'est pas modifiée)
struct Preset { const char* name; float delL, delR; bool polL, polR, link; };
inline constexpr Preset presets[] = {
    { "Default",                        0,    0, false, false, true  },
    { "Polarity inverted",              0,    0, true,  true,  true  },
    { "Mono test: R inverted",          0,    0, false, true,  false },
    { "Later by 1 ms (34 cm)",          1,    1, false, false, true  },
    { "Earlier by 1 ms (34 cm)",       -1,   -1, false, false, true  },
    { "Haas widening (R +8 ms)",        0,    8, false, false, false },
};
inline constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
} // namespace audiolab::params
