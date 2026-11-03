// Banc qualité de Lab Phase 2.0.0 (sans JUCE) : seuils chiffrés sur le moteur PhaseCore.
// clang++ -O2 -std=c++17 -I Source tests/quality_test.cpp -o qt && ./qt      (renvoie 1 en cas d'échec)
//  1. Latence exacte par mode (Standard, Zero) à 44,1 / 48 / 96 / 192 kHz.
//  2. Rotation de phase : écart à l'angle demandé ≤ 2° (cible ; ≈ 0,6° attendu) et module à ±0,2 dB de 40 Hz à 18 kHz,
//     aux 4 fréquences d'échantillonnage ; 0° et 180° au bit près.
//  3. Rotation, décalage fractionnaire et polarité ensemble ; glissement de l'angle sans clic.
//  4. Mode Zero : latence nulle, copie exacte, pas d'avance ni de rotation ; bascule de mode sans clic,
//     rotation juste dès la fin du fondu (historique recalculé).
//  5. Alignement de bout en bout : mesure DelayEstimator (comme le bouton Align) → réglages → la piste traitée
//     annule le side-chain (test de nul) ; même convention de rotation que Lab Corr.
//  6. Coût de calcul avec rotation.
#include "PhaseCore.h"
#include "DelayEstimator.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace audiolab;
using cd = std::complex<double>;
static bool OK = true;
static void check (bool c, const char* msg) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", msg); OK &= c; }
static const double PI = 3.14159265358979323846;

static void run (PhaseCore& c, std::vector<float>& L, std::vector<float>& R, int block, const PhaseParams* p = nullptr)
{
    for (size_t off = 0; off < L.size(); off += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, L.size() - off);
        if (p != nullptr) c.setParams (*p);
        float* io[2] = { L.data() + off, R.data() + off };
        c.process (io, n);
    }
}

// projection (fenêtre de Hann) sur e^{−jωn}, de i0 à i1
static cd phasor (const std::vector<float>& x, double w, size_t i0, size_t i1)
{
    cd acc = 0; const double N = (double) (i1 - i0);
    for (size_t i = i0; i < i1; ++i) acc += (double) x[i] * (0.5 - 0.5 * std::cos (2 * PI * (double) (i - i0) / N)) * std::polar (1.0, -w * (double) i);
    return acc;
}

static std::vector<float> noise (size_t n, unsigned seed)
{
    std::mt19937 g (seed); std::normal_distribution<float> d (0, 0.25f);
    std::vector<float> v (n); for (auto& s : v) s = d (g); return v;
}

// filtrage exact dans le domaine fréquentiel (signal périodisé) : bande [fLo, fHi], retard d, rotation θ, gain
static std::vector<float> spectral (const std::vector<float>& x, double fs, double fLo, double fHi, double d, double deg, double gain)
{
    size_t N = 1; while (N < x.size()) N <<= 1;
    std::vector<cd> X (N, cd (0));
    for (size_t i = 0; i < x.size(); ++i) X[i] = x[i];
    align::DelayEstimator::fft (X, false);
    for (size_t k = 0; k <= N / 2; ++k)
    {
        const double f = (double) k * fs / (double) N;
        cd v = (f >= fLo && f <= fHi) ? X[k] * std::polar (gain, -2 * PI * (double) k / (double) N * d + deg * PI / 180) : cd (0);
        if (k == 0 || k == N / 2) v = cd (0);
        X[k] = v; if (k > 0 && k < N / 2) X[N - k] = std::conj (v);
    }
    align::DelayEstimator::fft (X, true);
    std::vector<float> y (x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = (float) X[i].real();
    return y;
}

int main()
{
    std::puts ("\nLab Phase 2.0.0 : banc qualité");
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    // ── 1. Latence ──
    {
        bool ok = true; char det[300] = "";
        for (double fs : rates)
        {
            PhaseCore c; c.prepare (fs);
            PhaseParams p; c.setParams (p);
            const int Ls = c.getLatency();
            PhaseCore z; z.prepare (fs); PhaseParams pz; pz.zeroLatency = true; z.setParams (pz);
            ok &= Ls == PhaseCore::latencyFor (fs) && Ls == (int) std::ceil (fs / 100 - 1e-9) + 32 + PhaseCore::hilbertHalfFor (fs)
                  && z.getLatency() == 0 && PhaseCore::latencyFor (fs, true) == 0;
            std::snprintf (det + std::strlen (det), sizeof det - std::strlen (det), " %.1f kHz %d éch. (%.2f ms) ;", fs / 1000, Ls, Ls * 1000 / fs);
        }
        char m[400]; std::snprintf (m, sizeof m, "latence Standard = ⌈10 ms⌉ + 32 + M (Hilbert) :%s Zero : 0", det);
        check (ok, m);
    }

    // ── 2. Rotation : angle et module, 40 Hz → 18 kHz ──
    {
        bool ok = true; double worstDeg = 0, worstDb = 0, low20 = 0, low30 = 0, low30p = 0;
        for (double fs : rates)
        {
            for (double deg : { 30.0, 45.0, 90.0, 135.0, 225.0, 270.0, 333.3 })
            {
                PhaseCore c; c.prepare (fs);
                PhaseParams p; p.rotDeg[0] = deg; p.rotDeg[1] = deg; c.setParams (p);
                const int L = c.getLatency();
                for (double f : { 20.0, 30.0, 40.0, 50.0, 63.0, 100.0, 250.0, 1000.0, 4000.0, 10000.0, 15000.0, 18000.0 })
                {
                    const size_t n = (size_t) (fs * 0.6) + (size_t) L;
                    std::vector<float> l (n), r (n);
                    for (size_t i = 0; i < n; ++i) l[i] = r[i] = (float) (0.5 * std::sin (2 * PI * f * (double) i / fs));
                    const auto in = l;
                    c.reset(); c.setParams (p);
                    run (c, l, r, 512);
                    const double w = 2 * PI * f / fs;
                    const size_t i0 = (size_t) L + (size_t) (fs * 0.1), i1 = n;
                    const cd pin = phasor (in, w, i0 - (size_t) L, i1 - (size_t) L) * std::polar (1.0, -w * L);   // entrée retardée de L
                    const cd pout = phasor (l, w, i0, i1);
                    const cd ratio = pout / (pin * std::polar (1.0, deg * PI / 180));
                    const double eDeg = std::arg (ratio) * 180 / PI, eDb = 20 * std::log10 (std::abs (ratio));
                    if (f >= 40) { worstDeg = std::max (worstDeg, std::abs (eDeg)); worstDb = std::max (worstDb, std::abs (eDb)); }
                    if (std::abs (f - 20) < 1 && std::abs (deg - 90) < 1e-9 && std::abs (fs - 48000) < 1) low20 = eDb;
                    if (std::abs (f - 30) < 1 && std::abs (deg - 90) < 1e-9 && std::abs (fs - 48000) < 1) low30 = eDb;
                    if (std::abs (f - 30) < 1 && std::abs (deg - 45) < 1e-9 && std::abs (fs - 48000) < 1) low30p = eDeg;
                }
            }
        }
        ok = worstDeg <= 2.0 && worstDb <= 0.2;
        char m[300]; std::snprintf (m, sizeof m, "rotation 30 à 333°, 40 Hz → 18 kHz, 44,1 / 48 / 96 / 192 kHz : écart d'angle max %.2f° (cible ±2°), module ±%.3f dB",
                                    worstDeg, worstDb);
        check (ok, m);
        std::snprintf (m, sizeof m, "sous 40 Hz (48 kHz, pour la fiche) : à 90°, module %.2f dB à 30 Hz, %.2f dB à 20 Hz ; à 45°, angle %.1f° à 30 Hz ; Hilbert : gain %.3f à 40 Hz, %.3f à 20 Hz",
                       low30, low20, low30p, [] { PhaseCore c; c.prepare (48000); return c.hilbertGain (40); }(), [] { PhaseCore c; c.prepare (48000); return c.hilbertGain (20); }());
        check (true, m);
    }
    {
        // 0° et 180° (et 360° = 0°) : copie exacte / opposée, sans calcul de rotation
        bool ok = true;
        for (double deg : { 0.0, 180.0, 360.0 })
        {
            PhaseCore c; c.prepare (48000);
            PhaseParams p; p.rotDeg[0] = deg; p.rotDeg[1] = deg; p.delayMs[0] = 1.0; p.delayMs[1] = -2.0; c.setParams (p);
            auto in = noise (20000, 3); auto l = in, r = in;
            run (c, l, r, 333);
            const int L = c.getLatency();
            const float sg = deg == 180.0 ? -1.0f : 1.0f;
            for (size_t i = 3000; i < in.size(); ++i)
                ok &= l[i] == sg * in[i - (size_t) (L + 48)] && r[i] == sg * in[i - (size_t) (L - 96)];
        }
        check (ok, "rotation 0°, 180° et 360° : copie exacte (ou opposée) décalée, au bit près");
    }

    // ── 3. Rotation + décalage fractionnaire + polarité ; glissement sans clic ──
    {
        const double fs = 48000, f = 1000, deg = 72, dms = 0.0123;
        PhaseCore c; c.prepare (fs);
        PhaseParams p; p.samples = false; p.delayMs[0] = dms; p.delayMs[1] = dms; p.invert[0] = true; p.rotDeg[0] = deg; p.rotDeg[1] = deg; c.setParams (p);
        const int L = c.getLatency();
        const size_t n = (size_t) fs;
        std::vector<float> l (n), r (n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = (float) (0.5 * std::sin (2 * PI * f * (double) i / fs));
        const auto in = l;
        run (c, l, r, 480);
        const double w = 2 * PI * f / fs, D = L + std::round (dms * 1e5) * fs / 1e8;   // grille interne de 10 ns
        // sortie G (inversée) et D comparées à l'entrée décalée de D et tournée de 72°, sur la même fenêtre
        const cd pl = phasor (l, w, 20000, 44000), pr = phasor (r, w, 20000, 44000);
        const cd pi0 = phasor (in, w, 20000, 44000) * std::polar (1.0, -w * D + deg * PI / 180);
        const double eL = std::arg (pl / (-pi0)) * 180 / PI, eR = std::arg (pr / pi0) * 180 / PI;
        char m[200]; std::snprintf (m, sizeof m, "1 kHz, +0.0123 ms (fractionnaire) + rotation 72° (+ polarité inversée sur G) : écart %.3f° (G), %.3f° (D)", eL, eR);
        check (std::abs (eL) < 0.5 && std::abs (eR) < 0.5, m);
    }
    {
        // glissement de l'angle pendant un sinus : 0 → 90° → 300° → 0°, aucun saut
        const double fs = 48000;
        PhaseCore c; c.prepare (fs);
        PhaseParams p; c.setParams (p);
        const int n = 48000; std::vector<float> in ((size_t) n), l, r;
        for (int i = 0; i < n; ++i) in[(size_t) i] = (float) (0.8 * std::sin (2 * PI * 440 * i / fs));
        l = in; r = in;
        for (int off = 0, k = 0; off < n; off += 480, ++k)
        {
            if (k == 20) { p.rotDeg[0] = 90; p.rotDeg[1] = 300; }
            if (k == 50) { p.rotDeg[0] = 300; p.rotDeg[1] = 0; }
            c.setParams (p);
            float* io[2] = { l.data() + off, r.data() + off };
            c.process (io, 480);
        }
        float natural = 0, jump = 0;
        for (int i = 1; i < n; ++i)
        {
            natural = std::max (natural, std::abs (in[(size_t) i] - in[(size_t) i - 1]));
            if (i > c.getLatency() + 10) jump = std::max (jump, std::max (std::abs (l[(size_t) i] - l[(size_t) i - 1]), std::abs (r[(size_t) i] - r[(size_t) i - 1])));
        }
        char m[200]; std::snprintf (m, sizeof m, "rotation changée en lecture (0 → 90 → 300°, 0 → 300 → 0°) : plus grand pas %.4f (sinus seul %.4f)", jump, natural);
        check (jump <= natural * 1.03f, m);
    }

    // ── 4. Mode Zero ──
    {
        bool ok = true;
        for (double fs : rates)
        {
            PhaseCore c; c.prepare (fs);
            PhaseParams p; p.zeroLatency = true; p.samples = false; p.delayMs[0] = 1.2345; p.delayMs[1] = -3; p.rotDeg[0] = 90; p.invert[1] = true;
            c.setParams (p);
            auto in = noise (30000, 7); auto l = in, r = in;
            run (c, l, r, 256);
            const int d = (int) std::lround (1.2345 * fs / 1000);
            for (size_t i = 0; i < in.size(); ++i)
                ok &= l[i] == (i >= (size_t) d ? in[i - (size_t) d] : 0.0f) && r[i] == -in[i];
            ok &= c.getLatency() == 0;
        }
        check (ok, "mode Zero : latence 0 ; +1.2345 ms → échantillon entier le plus proche, copie exacte ; −3 ms → 0 (pas d'avance) ; rotation ignorée");
    }
    {
        // bascule Standard → Zero → Standard pendant un sinus : fondu (aucun saut), puis sortie juste
        const double fs = 48000;
        PhaseCore c; c.prepare (fs);
        PhaseParams p; p.rotDeg[0] = 90; p.rotDeg[1] = 90; p.delayMs[0] = 0.5; p.delayMs[1] = 0.5; c.setParams (p);
        const int n = 96000; std::vector<float> in ((size_t) n), l, r;
        for (int i = 0; i < n; ++i) in[(size_t) i] = (float) (0.8 * std::sin (2 * PI * 220 * i / fs));
        l = in; r = in;
        for (int off = 0, k = 0; off < n; off += 480, ++k)
        {
            if (k == 40) p.zeroLatency = true;
            if (k == 100) p.zeroLatency = false;
            c.setParams (p);
            float* io[2] = { l.data() + off, r.data() + off };
            c.process (io, 480);
        }
        float natural = 0, jump = 0;
        for (int i = 1; i < n; ++i) natural = std::max (natural, std::abs (in[(size_t) i] - in[(size_t) i - 1]));
        for (int i = 3000; i < n; ++i) jump = std::max (jump, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
        // référence : moteur resté en Standard depuis le début (mêmes réglages)
        PhaseCore ref; ref.prepare (fs); PhaseParams q = p; q.zeroLatency = false; ref.setParams (q);
        auto l2 = in, r2 = in; run (ref, l2, r2, 480);
        float diff = 0; for (int i = 100 * 480 + 2000; i < n; ++i) diff = std::max (diff, std::abs (l[(size_t) i] - l2[(size_t) i]));
        // pendant le mode Zero : copie exacte retardée de 24 éch.
        bool zeroOk = true; for (int i = 52 * 480; i < 98 * 480; ++i) zeroOk &= l[(size_t) i] == in[(size_t) (i - 24)];
        char m[260]; std::snprintf (m, sizeof m, "bascule Standard → Zero → Standard (rotation 90°) : plus grand pas %.4f (sinus seul %.4f) ; en Zero copie exacte (%d) ; "
                                    "retour en Standard identique à un moteur resté en Standard à %.1g près", jump, natural, (int) zeroOk, diff);
        check (jump <= natural * 1.05f && zeroOk && diff < 1e-6f, m);
    }

    // ── 5. Alignement de bout en bout (bouton Align) ──
    {
        const double fs = 48000; const int n = 65536;   // fenêtre d'analyse du bouton Align (≈ 1,4 s)
        auto src = noise ((size_t) n * 2, 11);
        const auto track = spectral (src, fs, 60, 16000, 0, 0, 1.0);
        struct Case { const char* name; double d, deg, gain; bool inv; };
        const Case cases[] = { { "side-chain = piste +20.37 éch., inversé", 20.37, 0, 0.8, true },
                               { "piste en retard de 7.5 éch. (avance nécessaire), rotation 60°", -7.5, 60, 1.0, false },
                               { "piste +3.2 ms, rotation −35°, inversée", 153.6, -35, 0.6, true } };
        for (const auto& cs : cases)
        {
            const auto sc = spectral (track, fs, 0, 24000, cs.d, cs.deg + (cs.inv ? 180 : 0), cs.gain);
            // mesure sur la 2e moitié (régime établi), comme le bouton Align sur les dernières 1,4 s
            const auto res = align::DelayEstimator::estimate (track.data() + n, sc.data() + n, n, fs, 10.0);
            PhaseParams p; p.samples = false;
            p.delayMs[0] = p.delayMs[1] = res.lagMs;
            p.invert[0] = p.invert[1] = res.invert;
            p.rotDeg[0] = p.rotDeg[1] = std::abs (res.residualDeg) >= 3 ? res.residualDeg : 0;
            PhaseCore c; c.prepare (fs); c.setParams (p);
            auto l = track, r = track; run (c, l, r, 512);
            const int L = c.getLatency();
            // nul après réglage du gain (Lab Phase ne change pas le niveau) : énergie de (g·piste traitée − side-chain)
            double ll = 0, ls = 0, ss = 0;
            for (size_t i = (size_t) n; i < track.size() - 1000; ++i) { const double a = l[i], b = sc[i - (size_t) L]; ll += a * a; ls += a * b; ss += b * b; }
            const double g = ls / ll, nullDb = 10 * std::log10 (std::max (1e-30, ss - 2 * g * ls + g * g * ll) / ss);
            char m[300]; std::snprintf (m, sizeof m, "Align, %s : mesuré %+.3f éch., %s, rotation %.1f° → nul %.1f dB (cible ≤ −40 dB)",
                                        cs.name, res.lagSamples, res.invert ? "inversé" : "normal", p.rotDeg[0], nullDb);
            check (res.valid && nullDb <= -40, m);
        }
    }

    // ── 6. Coût ──
    {
        char m[260]; double cost[3] = { 0, 0, 0 }; int k = 0;
        for (double fs : { 48000.0, 96000.0, 192000.0 })
        {
            PhaseCore c; c.prepare (fs);
            PhaseParams p; p.rotDeg[0] = 33; p.rotDeg[1] = 147; p.samples = false; p.delayMs[0] = 0.0123; p.delayMs[1] = -1.1; c.setParams (p);
            auto l = noise ((size_t) fs * 3, 1), r = noise ((size_t) fs * 3, 2);
            const auto t0 = std::chrono::steady_clock::now();
            run (c, l, r, 512);
            cost[k++] = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 3 * 100;
        }
        std::snprintf (m, sizeof m, "coût en stéréo, rotation + décalage fractionnaire : %.1f %% d'un cœur à 48 kHz, %.1f %% à 96 kHz, %.1f %% à 192 kHz", cost[0], cost[1], cost[2]);
        check (cost[0] < 5 && cost[2] < 30, m);
    }

    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}
