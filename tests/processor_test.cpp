// Tests du plugin hors DAW : sauvegarde / rappel, presets, latence annoncée, décalage exact (G / D, Link),
// recopie de G dans D avec Link, bypass aligné, mono, plugin = moteur en blocs irréguliers, unité ms, saisie de valeurs.
// 2.0.0 : sessions 1.x, rotation (Link), mode de latence Zero, bouton Align sur le side-chain (fil de fond et direct).
#include "PluginProcessor.h"
#include <cstdio>
#include <random>
#include <thread>

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
    // ── 2.0.0 ──
    {
        // session 1.x (sans rotL, rotR, latMode) relue dans une instance réglée autrement : valeurs par défaut
        P a; a.setParam (params::delL, 1.25f); juce::MemoryBlock mb; a.getStateInformation (mb);
        auto st = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
        for (auto* id : { params::rotL, params::rotR, params::latMode }) st.removeChild (st.getChildWithProperty ("id", id), nullptr);
        juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*st.createXml(), mb2);
        P b; b.setParam (params::rotL, 90); b.setParam (params::rotR, 45); b.setParam (params::latMode, 1);
        b.setStateInformation (mb2.getData(), (int) mb2.getSize());
        P c; c.setParam (params::link, 0); c.setParam (params::rotL, 33.3f); c.setParam (params::rotR, 270); c.setParam (params::latMode, 1);
        juce::MemoryBlock mb3; c.getStateInformation (mb3);
        P d; d.setStateInformation (mb3.getData(), (int) mb3.getSize());
        check (std::abs (b.getParam (params::delL) - 1.25f) < 1e-6f && b.getParam (params::rotL) == 0 && b.getParam (params::rotR) == 0 && b.getParam (params::latMode) < 0.5f
               && std::abs (d.getParam (params::rotL) - 33.3f) < 1e-4f && std::abs (d.getParam (params::rotR) - 270) < 1e-4f && d.getParam (params::latMode) > 0.5f,
               "session 1.x : rotation 0° et latence Standard (défauts) ; rotation et mode Zero enregistrés puis relus");
    }
    {
        // mode de latence : la latence annoncée suit (0 en Zero), sortie = copie exacte retardée (pas d'avance)
        P p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        const int Ls = p.getLatencySamples();
        p.setParam (params::latMode, 1); pump();
        const int Lz = p.getLatencySamples();
        p.setParam (params::delL, -2.0f); p.setParam (params::link, 0); p.setParam (params::delR, 1.0f); p.setParam (params::rotL, 90); pump();
        std::vector<float> inL, inR, outL, outR; juce::MidiBuffer m;
        for (int k = 0; k < 60; ++k)
        {
            juce::AudioBuffer<float> b (2, 512); noise (b);
            for (int i = 0; i < 512; ++i) { inL.push_back (b.getSample (0, i)); inR.push_back (b.getSample (1, i)); }
            p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { outL.push_back (b.getSample (0, i)); outR.push_back (b.getSample (1, i)); }
        }
        bool exact = true;
        for (size_t i = 8000; i < outL.size(); ++i) exact &= outL[i] == inL[i] && outR[i] == inR[i - 48];
        p.setParam (params::latMode, 0); pump();
        char msg[220]; std::snprintf (msg, sizeof msg, "mode Zero : latence %d → %d éch. ; G −2 ms → 0 (pas d'avance), rotation ignorée, D +1 ms : copies exactes ; retour Standard : %d éch.",
                                      Ls, Lz, p.getLatencySamples());
        check (Ls == PhaseCore::latencyFor (48000) && Lz == 0 && exact && p.getLatencySamples() == Ls, msg);
    }
    {
        // rotation : Link recopie G dans D ; plugin = moteur ; presets remettent la rotation à 0
        P p; p.setPlayConfigDetails (2, 2, 48000, 256); p.prepareToPlay (48000, 256);
        p.setParam (params::rotL, 120); pump();
        const bool copied = std::abs (p.getParam (params::rotR) - 120) < 1e-4f && std::abs (p.currentParams().rotDeg[1] - 120) < 1e-4;
        p.setParam (params::link, 0); p.setParam (params::rotR, 200); pump();
        const auto e = p.currentParams();
        const bool sep = std::abs (e.rotDeg[0] - 120) < 1e-4 && std::abs (e.rotDeg[1] - 200) < 1e-4;
        p.prepareToPlay (48000, 256);   // départ sans fondu
        PhaseCore c; c.prepare (48000, 2); c.setParams (p.currentParams());
        juce::MidiBuffer m; float err = 0;
        for (int k = 0; k < 40; ++k)
        {
            juce::AudioBuffer<float> b (2, 256); noise (b); juce::AudioBuffer<float> ref (b);
            p.processBlock (b, m);
            float* io[2] = { ref.getWritePointer (0), ref.getWritePointer (1) }; c.process (io, 256);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) err = std::max (err, std::abs (b.getSample (ch, i) - ref.getSample (ch, i)));
        }
        p.setCurrentProgram (3);
        check (copied && sep && err == 0.0f && p.getParam (params::rotL) == 0 && p.getParam (params::rotR) == 0,
               "rotation : Link recopie G dans D, canaux séparés sans Link, plugin = moteur, preset → rotation 0°");
    }
    {
        // Align (direct) : piste stéréo + side-chain = −piste retardée de 37 éch. → décalage +37 éch., polarité inversée ;
        // ensuite la sortie est la copie exacte du side-chain (retardée de la latence)
        P p; P::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        const bool layoutOk = p.setBusesLayout (l);
        p.prepareToPlay (48000, 480);
        std::vector<float> hist (64, 0.0f); int hw = 0; juce::MidiBuffer m;
        std::vector<float> outs, scs;
        auto feed = [&] (int blocks, bool record)
        {
            for (int k = 0; k < blocks; ++k)
            {
                juce::AudioBuffer<float> b (4, 480);
                for (int i = 0; i < 480; ++i)
                {
                    const float s = U (rng); hist[(size_t) hw] = s;
                    const float d = hist[(size_t) ((hw - 37 + 64) % 64)]; hw = (hw + 1) % 64;
                    b.setSample (0, i, s); b.setSample (1, i, s); b.setSample (2, i, -d); b.setSample (3, i, -d);
                }
                if (record) for (int i = 0; i < 480; ++i) scs.push_back (b.getSample (2, i));
                p.processBlock (b, m);
                if (record) for (int i = 0; i < 480; ++i) outs.push_back (b.getSample (0, i));
            }
        };
        feed (160, false);   // 1,6 s
        const bool ok = p.alignNow();
        const auto st = p.alignStatus();
        feed (100, true);
        const int L = p.getLatencySamples();
        bool exact = true; for (size_t i = 4000; i < outs.size(); ++i) exact &= outs[i] == scs[i - (size_t) L];
        char msg[300]; std::snprintf (msg, sizeof msg, "Align : « %s » / « %s » → G %+.4f ms, Ø %d, rotation %.1f°, unité %s ; sortie = side-chain au bit près (%d)",
                                      st.title.toRawUTF8(), st.detail.toRawUTF8(), p.getParam (params::delL), (int) (p.getParam (params::polL) > 0.5f),
                                      p.getParam (params::rotL), p.getParam (params::unit) > 0.5f ? "ms" : "Samples", (int) exact);
        check (layoutOk && ok && std::abs (p.getParam (params::delL) - 37 / 48.0f) < 1e-4f && p.getParam (params::polL) > 0.5f && p.getParam (params::rotL) == 0
               && p.getParam (params::unit) < 0.5f && exact, msg);
    }
    {
        // Align (fil de fond, comme le bouton) : side-chain = piste en avance de 10,5 éch. et tournée de 40° (2e micro plus près)
        P p; P::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::mono());
        p.setBusesLayout (l); p.prepareToPlay (48000, 512);
        // signaux préparés d'avance dans le domaine fréquentiel (bruit 60 Hz à 16 kHz)
        const int N = 1 << 18; std::vector<std::complex<double>> X ((size_t) N), Y;
        std::mt19937 g2 (9); std::normal_distribution<double> G (0, 1);
        for (auto& v : X) v = G (g2);
        audiolab::align::DelayEstimator::fft (X, false);
        Y = X;
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = k * 48000.0 / N; const bool in = f >= 60 && f <= 16000;
            X[(size_t) k] = in ? X[(size_t) k] : 0.0;
            Y[(size_t) k] = in ? X[(size_t) k] * std::polar (0.7, 2 * juce::MathConstants<double>::pi * k * 10.5 / N + 40 * juce::MathConstants<double>::pi / 180) : 0.0;
            if (k > 0 && k < N / 2) { X[(size_t) (N - k)] = std::conj (X[(size_t) k]); Y[(size_t) (N - k)] = std::conj (Y[(size_t) k]); }
        }
        X[(size_t) N / 2] = Y[(size_t) N / 2] = 0.0;
        audiolab::align::DelayEstimator::fft (X, true); audiolab::align::DelayEstimator::fft (Y, true);
        juce::MidiBuffer m; std::vector<float> out, ref;
        int pos = 0;
        auto feed = [&] (int blocks, bool rec)
        {
            for (int k = 0; k < blocks; ++k)
            {
                juce::AudioBuffer<float> b (3, 512);
                for (int i = 0; i < 512; ++i, ++pos)
                {
                    const float t = (float) X[(size_t) pos].real() * 0.2f, sc = (float) Y[(size_t) pos].real() * 0.2f;
                    b.setSample (0, i, t); b.setSample (1, i, t); b.setSample (2, i, sc);
                    if (rec) ref.push_back (sc);
                }
                p.processBlock (b, m);
                if (rec) for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i));
            }
        };
        feed (150, false);
        p.requestAlign();
        const bool listening = p.alignStatus().state == P::AlignStatus::State::listening;
        for (int t = 0; t < 100 && p.alignStatus().state == P::AlignStatus::State::listening; ++t)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
            p.flushLinkCopy();
        }
        const auto st = p.alignStatus();
        feed (150, true);
        const int L = p.getLatencySamples();
        double ll = 0, ls = 0, ss = 0;
        for (size_t i = 20000; i < out.size(); ++i) { const double a = out[i], c = ref[i - (size_t) L]; ll += a * a; ls += a * c; ss += c * c; }
        const double gg = ls / ll, nullDb = 10 * std::log10 ((ss - 2 * gg * ls + gg * gg * ll) / ss);
        char msg[300]; std::snprintf (msg, sizeof msg, "Align en fil de fond : « %s » / « %s » → G %+.4f ms, rotation %.2f°, unité %s ; nul %.1f dB",
                                      st.title.toRawUTF8(), st.detail.toRawUTF8(), p.getParam (params::delL), p.getParam (params::rotL),
                                      p.getParam (params::unit) > 0.5f ? "ms" : "Samples", nullDb);
        check (listening && st.state == P::AlignStatus::State::done && std::abs (p.getParam (params::delL) + 10.5f / 48) < 2e-4f
               && std::abs (p.getParam (params::rotL) - 40) < 0.3f && p.getParam (params::unit) > 0.5f && nullDb < -40, msg);
    }
    {
        // Align impossible : side-chain absent ; side-chain sans rapport ; mode Zero et piste en retard
        auto make = [&] (bool sc) { auto q = std::make_unique<P>(); P::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo());
                                     l.outputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (sc ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
                                     q->setBusesLayout (l); q->prepareToPlay (48000, 512); return q; };
        juce::MidiBuffer m;
        auto a = make (false);
        for (int k = 0; k < 150; ++k) { juce::AudioBuffer<float> b (2, 512); noise (b); a->processBlock (b, m); }
        const bool r1 = ! a->alignNow(); const auto s1 = a->alignStatus();
        auto c = make (true);
        for (int k = 0; k < 150; ++k) { juce::AudioBuffer<float> b (4, 512); noise (b); c->processBlock (b, m); }
        const bool r2 = ! c->alignNow(); const auto s2 = c->alignStatus();
        auto z = make (true); z->setParam (params::latMode, 1); z->flushLinkCopy();
        std::vector<float> hist (64, 0.0f); int hw = 0;
        for (int k = 0; k < 150; ++k)
        {
            juce::AudioBuffer<float> b (4, 512);
            for (int i = 0; i < 512; ++i) { const float s = U (rng); hist[(size_t) hw] = s; const float d = hist[(size_t) ((hw - 20 + 64) % 64)]; hw = (hw + 1) % 64;
                                            b.setSample (0, i, d); b.setSample (1, i, d); b.setSample (2, i, s); b.setSample (3, i, s); }
            z->processBlock (b, m);
        }
        const bool r3 = ! z->alignNow(); const auto s3 = z->alignStatus();
        char msg[400]; std::snprintf (msg, sizeof msg, "Align refusé : « %s » ; « %s » ; « %s » (réglages inchangés : %d)",
                                      s1.title.toRawUTF8(), s2.title.toRawUTF8(), s3.title.toRawUTF8(), (int) (z->getParam (params::delL) == 0));
        check (r1 && r2 && r3 && s1.title == "No side-chain signal" && s2.title.startsWith ("No clear match") && s3.title == "The track is late"
               && z->getParam (params::delL) == 0 && s1.state == P::AlignStatus::State::failed, msg);
    }
    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}
