// Tests du plugin hors DAW : sauvegarde / rappel, presets, latence annoncée, décalage exact (G / D, Link),
// recopie de G dans D avec Link, bypass aligné, mono, plugin = moteur en blocs irréguliers, unité ms, saisie de valeurs.
#include "PluginProcessor.h"
#include <cstdio>
#include <random>

static bool OK = true;
static void check (bool c, const char* msg) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", msg); OK &= c; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace audiolab;
    using P = AudioLabPhaseProcessor;
    // recopie Link : faite d'habitude par le fil de l'interface, juste après le changement
    #define pump() p.flushLinkCopy()
    std::mt19937 rng (5); std::uniform_real_distribution<float> U (-0.9f, 0.9f);
    auto noise = [&] (juce::AudioBuffer<float>& b) { for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, U (rng)); };

    std::puts ("\nPlugin Audio Lab Phase");
    {
        P a; a.setParam (params::link, 0); a.setParam (params::delL, -3.25f); a.setParam (params::delR, 7.125f);
        a.setParam (params::polR, 1); a.setParam (params::unit, 1);
        a.ui.setProperty ("view", 1, nullptr); a.ui.setProperty ("scale", 1.5, nullptr);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        P b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.getParam (params::link) < 0.5f && std::abs (b.getParam (params::delL) + 3.25f) < 1e-5f && std::abs (b.getParam (params::delR) - 7.125f) < 1e-5f
               && b.getParam (params::polR) > 0.5f && b.getParam (params::polL) < 0.5f && b.getParam (params::unit) > 0.5f
               && (int) b.ui.getProperty ("view") == 1 && std::abs ((double) b.ui.getProperty ("scale") - 1.5) < 1e-9,
               "sauvegarde / rappel de la session (décalages, polarités, Link, unité, vue, taille de la fenêtre)");
    }
    {
        P p;
        const bool names = p.getNumPrograms() == 6 && p.getProgramName (0) == juce::String ("Default")
                           && p.getProgramName (5) == juce::String ("Haas widening (R +8 ms)");
        p.setCurrentProgram (5);
        const bool haas = p.getParam (params::link) < 0.5f && std::abs (p.getParam (params::delR) - 8) < 1e-5f && std::abs (p.getParam (params::delL)) < 1e-6f;
        p.setCurrentProgram (2);
        const bool test = p.getParam (params::polR) > 0.5f && p.getParam (params::polL) < 0.5f && p.getParam (params::link) < 0.5f;
        p.setCurrentProgram (4);
        pump();
        const bool adv = std::abs (p.getParam (params::delL) + 1) < 1e-5f && std::abs (p.getParam (params::delR) + 1) < 1e-5f && p.getParam (params::link) > 0.5f;
        check (names && haas && test && adv, "presets d'usage (6 programmes : Par défaut … Élargissement Haas)");
    }
    {
        bool ok = true; char det[200] = "";
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            P p; p.setPlayConfigDetails (2, 2, fs, 512); p.prepareToPlay (fs, 512);
            const int L0 = p.getLatencySamples();
            p.setParam (params::delL, -9.9f); p.setParam (params::unit, 1);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; noise (b); p.processBlock (b, m);
            ok &= L0 == PhaseCore::latencyFor (fs) && p.getLatencySamples() == L0;
            std::snprintf (det + std::strlen (det), sizeof det - std::strlen (det), " %.1f kHz : %d éch. (%.2f ms) ;", fs / 1000, L0, L0 * 1000 / fs);
        }
        char msg[300]; std::snprintf (msg, sizeof msg, "latence annoncée fixe, indépendante des réglages :%s", det);
        check (ok, msg);
    }
    {
        // G −2 ms, D +3 ms (Link coupé), D inversée, unité Échantillons : copie exacte décalée
        P p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        p.setParam (params::link, 0); p.setParam (params::delL, -2.0f); p.setParam (params::delR, 3.0f); p.setParam (params::polR, 1);
        pump();
        p.prepareToPlay (48000, 512);   // départ sans fondu
        const int L = p.getLatencySamples();
        std::vector<float> inL, inR; juce::MidiBuffer m; std::vector<float> outL, outR;
        for (int k = 0; k < 40; ++k)
        {
            juce::AudioBuffer<float> b (2, 512); noise (b);
            for (int i = 0; i < 512; ++i) { inL.push_back (b.getSample (0, i)); inR.push_back (b.getSample (1, i)); }
            p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { outL.push_back (b.getSample (0, i)); outR.push_back (b.getSample (1, i)); }
        }
        bool exact = true;
        for (size_t i = 2000; i < outL.size(); ++i)
            exact &= outL[i] == inL[i - (size_t) (L - 96)] && outR[i] == -inR[i - (size_t) (L + 144)];
        check (exact, "G −2 ms et D +3 ms inversée (Link coupé) : copies exactes décalées de −96 et +144 éch., au bit près");
    }
    {
        // Link : D suit G dans le moteur, et le paramètre D est recopié ; en coupant Link, rien ne bouge
        P p; p.setPlayConfigDetails (2, 2, 48000, 256); p.prepareToPlay (48000, 256);
        p.setParam (params::link, 1); p.setParam (params::delR, 5.0f); p.setParam (params::delL, 1.5f); p.setParam (params::polL, 1);
        const auto e = p.currentParams();
        const bool engine = juce::exactlyEqual (e.delayMs[1], e.delayMs[0]) && e.invert[1];
        pump();
        const bool copied = std::abs (p.getParam (params::delR) - 1.5f) < 1e-6f && p.getParam (params::polR) > 0.5f;
        p.setParam (params::link, 0); pump();
        const auto e2 = p.currentParams();
        const bool still = std::abs (e2.delayMs[1] - 1.5) < 1e-6 && e2.invert[1];
        check (engine && copied && still, "Link : D suit G (moteur et paramètres) ; en coupant Link, D garde la même valeur (aucun saut)");
    }
    {
        P p; p.setPlayConfigDetails (2, 2, 44100, 1024); p.prepareToPlay (44100, 1024);
        p.setParam (params::delL, 4.0f); p.setParam (params::polL, 1); pump(); p.prepareToPlay (44100, 1024);
        const int L = p.getLatencySamples();
        std::vector<float> in, out; juce::MidiBuffer m;
        for (int k = 0; k < 40; ++k)
        {
            if (k == 5) p.setParam (params::bypass, 1);
            juce::AudioBuffer<float> b (2, 1024); noise (b);
            for (int i = 0; i < 1024; ++i) in.push_back (b.getSample (0, i));
            p.processBlock (b, m);
            for (int i = 0; i < 1024; ++i) out.push_back (b.getSample (0, i));
        }
        bool exact = true; for (size_t i = 8000; i < out.size(); ++i) exact &= out[i] == in[i - (size_t) L];
        check (exact, "bypass du logiciel : après le fondu de 20 ms, sortie = entrée retardée de la latence (alignée), au bit près");
    }
    {
        P p; p.setPlayConfigDetails (1, 1, 48000, 300); p.prepareToPlay (48000, 300);
        p.setParam (params::delL, 0.5f); p.setParam (params::polL, 1); pump(); p.prepareToPlay (48000, 300);
        const int L = p.getLatencySamples();
        std::vector<float> in, out; juce::MidiBuffer m;
        for (int k = 0; k < 20; ++k)
        {
            juce::AudioBuffer<float> b (1, 300); noise (b);
            for (int i = 0; i < 300; ++i) in.push_back (b.getSample (0, i));
            p.processBlock (b, m);
            for (int i = 0; i < 300; ++i) out.push_back (b.getSample (0, i));
        }
        bool exact = p.isMono(); for (size_t i = 1500; i < out.size(); ++i) exact &= out[i] == -in[i - (size_t) (L + 24)];
        check (exact, "mono (1 voie, blocs de 300) : +0,5 ms et polarité inversée, au bit près");
    }
    {
        // plugin = moteur, blocs irréguliers, unité ms (filtre fractionnaire), réglages changés en cours de lecture
        P p; p.setPlayConfigDetails (2, 2, 48000, 4096); p.prepareToPlay (48000, 4096);
        p.setParam (params::link, 0); p.setParam (params::unit, 1); p.setParam (params::delL, -1.234f); p.setParam (params::delR, 0.017f);
        pump(); p.prepareToPlay (48000, 4096);
        PhaseCore c; c.prepare (48000, 2); c.setParams (p.currentParams());
        juce::Random r (3); juce::MidiBuffer m; float err = 0;
        for (int k = 0; k < 150; ++k)
        {
            if (k == 60) { p.setParam (params::delR, -6.66f); p.setParam (params::polL, 1); }
            const int n = 1 + r.nextInt (1500);
            juce::AudioBuffer<float> b (2, n); noise (b);
            juce::AudioBuffer<float> ref (b);
            p.processBlock (b, m);
            c.setParams (p.currentParams());
            float* io[2] = { ref.getWritePointer (0), ref.getWritePointer (1) };
            c.process (io, n);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) err = std::max (err, std::abs (b.getSample (ch, i) - ref.getSample (ch, i)));
        }
        char msg[160]; std::snprintf (msg, sizeof msg, "plugin = moteur, unité ms, blocs de 1 à 1500 éch., réglages changés en lecture : écart max %.1e", (double) err);
        check (err == 0.0f, msg);
    }
    {
        double v = 0; bool ok = true;
        ok &= params::parseValue (juce::String::fromUTF8 ("1,5"), false, 48000, v) && std::abs (v - 1.5) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("72"), true, 48000, v) && std::abs (v - 1.5) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("72 smp"), false, 48000, v) && std::abs (v - 1.5) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("72 éch."), false, 48000, v) && std::abs (v - 1.5) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("−0.5 ms"), true, 48000, v) && std::abs (v + 0.5) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("250 µs"), true, 48000, v) && std::abs (v - 0.25) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("34,3 cm"), true, 48000, v) && std::abs (v - 1.0) < 1e-12;
        ok &= params::parseValue (juce::String::fromUTF8 ("25 ms"), true, 48000, v) && std::abs (v - 10.0) < 1e-12;
        ok &= ! params::parseValue (juce::String::fromUTF8 ("abc"), true, 48000, v);
        ok &= params::fmtMs (-1.25) == juce::String::fromUTF8 ("−1.250 ms") && params::fmtSamples (48) == juce::String ("+48 smp")
              && params::fmtDist (1.0) == juce::String ("34 cm") && params::fmtHz (1000) == juce::String ("1.00 kHz");
        check (ok, "saisie au clavier (« 1,5 », « 72 éch. », « −0.5 ms », « 250 µs », « 34,3 cm », limite à 10 ms) et affichage des valeurs");
    }
    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}
