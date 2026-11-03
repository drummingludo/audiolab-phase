// Banc de test du moteur PhaseCore (sans JUCE) : décalage exact, avance compensée, polarité,
// lecture fractionnaire (mode ms), fondus sans clic, bypass aligné, indépendance de la taille des blocs.
// 2.0.0 : latence plus longue (transformée de Hilbert de la rotation), lue avec getLatency() ; grille interne de 10 ns.
// Rotation, mode Zero et alignement : tests/quality_test.cpp.
// g++ -O2 -std=c++17 -ffp-contract=off -I Source tests/core_test.cpp -o tests/core_test && tests/core_test
#include "PhaseCore.h"
#include <cstdio>
#include <random>
#include <complex>

using namespace audiolab;
static bool OK = true;
static void check (bool c, const char* msg) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", msg); OK &= c; }
static const double PI = 3.14159265358979323846;

// Fait tourner le moteur sur deux canaux, en blocs de taille fixe ou aléatoire
static void run (PhaseCore& core, std::vector<float>& L, std::vector<float>& R, int block, unsigned seed = 0)
{
    std::mt19937 rng (seed);
    const int n = (int) L.size();
    for (int off = 0; off < n;)
    {
        int b = block > 0 ? block : 1 + (int) (rng() % 4096);
        b = std::min (b, n - off);
        float* io[2] = { L.data() + off, R.data() + off };
        core.process (io, b);
        off += b;
    }
}

int main()
{
    std::puts ("\nMoteur Audio Lab Phase (PhaseCore)");
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> U (-0.9f, 0.9f);

    // 1. Unité « Échantillons » : copie exacte de l'entrée, décalée de L + d, à 44,1 / 48 / 96 kHz
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        PhaseCore c; c.prepare (fs);
        const int L = c.getLatency(), lim = (int) std::floor (10 * fs / 1000 + 1e-9);
        bool exact = true; int tested = 0;
        for (int d : { -lim, -lim + 1, -37, -1, 0, 1, 2, 48, 441, lim - 1, lim })
        {
            for (bool inv : { false, true })
            {
                PhaseParams p; p.samples = true; p.delayMs[0] = d * 1000.0 / fs; p.delayMs[1] = -d * 1000.0 / fs;
                p.invert[0] = inv; p.invert[1] = ! inv;
                c.reset(); c.setParams (p);
                std::vector<float> in (8000), l (8000), r (8000);
                for (auto& v : in) v = U (rng);
                l = in; r = in;
                run (c, l, r, 0, (unsigned) (d + 5000));
                for (int i = 0; i < 8000; ++i)
                {
                    const int jl = i - L - d, jr = i - L + d;
                    const float el = jl >= 0 ? (inv ? -in[(size_t) jl] : in[(size_t) jl]) : 0.0f;
                    const float er = jr >= 0 ? (inv ? in[(size_t) jr] : -in[(size_t) jr]) : 0.0f;
                    exact &= l[(size_t) i] == el && r[(size_t) i] == er;
                }
                ++tested;
            }
        }
        char msg[200];
        std::snprintf (msg, sizeof msg, "%6.0f Hz, unité Échantillons : sortie = ±entrée décalée de L + d au bit près (L = %d éch., d de −%d à +%d, %d cas, blocs de 1 à 4096)",
                       fs, L, lim, lim, tested);
        check (exact, msg);
    }

    // 2. Avance : une fois la latence compensée par l'hôte (sortie lue L échantillons plus tôt), d < 0 arrive AVANT l'original
    {
        PhaseCore c; c.prepare (48000);
        PhaseParams p; p.delayMs[0] = -1.0; p.delayMs[1] = 1.0; c.setParams (p);
        std::vector<float> l (4000, 0.0f), r (4000, 0.0f); l[1000] = 1; r[1000] = 1;
        run (c, l, r, 512);
        int pl = -1, pr = -1;
        for (int i = 0; i < 4000; ++i) { if (l[(size_t) i] != 0) pl = i; if (r[(size_t) i] != 0) pr = i; }
        const int L = c.getLatency();
        char msg[200]; std::snprintf (msg, sizeof msg, "impulsion à 1000 : après compensation, G (−1 ms) sort à %d, D (+1 ms) à %d (attendu 952 et 1048)", pl - L, pr - L);
        check (pl - L == 952 && pr - L == 1048, msg);
    }

    // 3. Unité « ms » : retard fractionnaire. Sinus de 20 Hz à 20 kHz : gain et retard mesurés vs théorie
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        double worstDb = 0, worstUs = 0;
        for (double ms : { 0.01, 0.0123, 0.25, -0.333, 1.007, -4.321, 9.999 })
        {
            PhaseCore c; c.prepare (fs);
            PhaseParams p; p.samples = false; p.delayMs[0] = ms; p.delayMs[1] = ms; c.setParams (p);
            const double D = c.getLatency() + std::round (ms * 1e5) * fs / 1e8;   // grille interne de 10 ns (2.0.0 ; 1 µs en 1.x)
            for (double f : { 20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
            {
                const int n = (int) (fs * 0.5);
                std::vector<float> l ((size_t) n), r ((size_t) n);
                for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) (0.5 * std::sin (2 * PI * f * i / fs));
                run (c, l, r, 777);
                // projection sur sin / cos sur la 2ᵉ moitié (régime établi), nombre entier de périodes approché
                std::complex<double> acc = 0;
                const int i0 = n / 2, cnt = n - i0;
                for (int i = i0; i < n; ++i) acc += (double) l[(size_t) i] * std::exp (std::complex<double> (0, -2 * PI * f * i / fs));
                const std::complex<double> ideal = 0.5 * std::exp (std::complex<double> (0, -2 * PI * f * D / fs)) * std::complex<double> (0, -0.5);
                const auto meas = acc / (double) cnt;
                const double ratio = std::abs (meas) / std::abs (ideal);
                double dph = std::arg (meas / ideal);                 // écart de phase
                worstDb = std::max (worstDb, std::abs (20 * std::log10 (ratio)));
                worstUs = std::max (worstUs, std::abs (dph / (2 * PI * f)) * 1e6);
            }
        }
        char msg[200]; std::snprintf (msg, sizeof msg, "%6.0f Hz, unité ms (grille de 10 ns) : gain 20 Hz–20 kHz à ±%.4f dB, erreur de retard ≤ %.3f µs", fs, worstDb, worstUs);
        check (worstDb < 0.02 && worstUs < 0.05, msg);
    }

    // 4. Unité ms sur un échantillon entier (1,000 ms à 48 kHz = 48 éch.) : lecture exacte, pas de filtre
    {
        PhaseCore c; c.prepare (48000);
        PhaseParams p; p.samples = false; p.delayMs[0] = 1.0; p.delayMs[1] = -0.5; c.setParams (p);
        std::vector<float> in (6000), l, r; for (auto& v : in) v = U (rng);
        l = in; r = in; run (c, l, r, 300);
        const int L = c.getLatency(); bool exact = true;
        for (int i = 2000; i < 6000; ++i) exact &= l[(size_t) i] == in[(size_t) (i - L - 48)] && r[(size_t) i] == in[(size_t) (i - L + 24)];
        check (exact, "unité ms tombant sur un échantillon entier (1,000 ms ; −0,500 ms à 48 kHz) : copie exacte");
    }

    // 5. Fondu de 20 ms : changement de décalage pendant un sinus, aucun saut (dérivée bornée), arrivée exacte
    {
        PhaseCore c; c.prepare (48000);
        PhaseParams p; c.setParams (p);
        const int n = 48000; std::vector<float> in ((size_t) n), l, r;
        for (int i = 0; i < n; ++i) in[(size_t) i] = (float) (0.8 * std::sin (2 * PI * 440 * i / 48000));
        l = in; r = in;
        float maxJump = 0, natural = 0;
        for (int off = 0, k = 0; off < n; off += 480, ++k)
        {
            if (k == 20) { p.delayMs[0] = 7.3; p.delayMs[1] = -3.1; p.invert[1] = true; c.setParams (p); }
            if (k == 60) { p.samples = false; p.delayMs[0] = 2.345; c.setParams (p); }
            float* io[2] = { l.data() + off, r.data() + off };
            c.process (io, 480);
        }
        for (int i = 1; i < n; ++i)
        {
            natural = std::max (natural, std::abs (in[(size_t) i] - in[(size_t) i - 1]));
            maxJump = std::max (maxJump, std::max (std::abs (l[(size_t) i] - l[(size_t) i - 1]), std::abs (r[(size_t) i] - r[(size_t) i - 1])));
        }
        const int L = c.getLatency(); bool arrived = true;
        for (int i = 20000; i < 28000; ++i) arrived &= r[(size_t) i] == -in[(size_t) (i - L + 149)];   // −3,1 ms = −148,8 → −149 éch.
        char msg[220]; std::snprintf (msg, sizeof msg, "fondus (20 ms) : plus grand pas entre deux échantillons %.4f (sinus seul : %.4f), puis copie exacte à la nouvelle position", maxJump, natural);
        check (maxJump <= natural * 1.05f && arrived, msg);
    }

    // 6. Bypass : fondu de 20 ms, puis entrée exacte retardée de la latence (alignée sur les autres pistes)
    {
        PhaseCore c; c.prepare (44100);
        PhaseParams p; p.delayMs[0] = 3; p.invert[0] = true; c.setParams (p);
        std::vector<float> in (20000), l, r; for (auto& v : in) v = U (rng);
        l = in; r = in;
        for (int off = 0, k = 0; off < 20000; off += 1000, ++k)
        {
            if (k == 4) { p.bypass = true; c.setParams (p); }
            float* io[2] = { l.data() + off, r.data() + off }; c.process (io, 1000);
        }
        const int L = c.getLatency(); bool exact = true;
        for (int i = 6000; i < 20000; ++i) exact &= l[(size_t) i] == in[(size_t) (i - L)] && r[(size_t) i] == in[(size_t) (i - L)];
        check (exact, "bypass : après le fondu, sortie = entrée retardée de la latence, au bit près");
    }

    // 7. Taille des blocs : 1 éch., 64, 4096, aléatoire → résultats identiques (unité ms, filtre actif)
    {
        std::vector<float> in (30000); for (auto& v : in) v = U (rng);
        std::vector<std::vector<float>> outs;
        for (int blk : { 1, 64, 4096, 0 })
        {
            PhaseCore c; c.prepare (48000);
            PhaseParams p; p.samples = false; p.delayMs[0] = -2.718; p.delayMs[1] = 0.031; p.invert[0] = true; c.setParams (p);
            auto l = in, r = in; run (c, l, r, blk, 9);
            l.insert (l.end(), r.begin(), r.end()); outs.push_back (l);
        }
        bool same = true; for (size_t k = 1; k < outs.size(); ++k) same &= outs[k] == outs[0];
        check (same, "taille des blocs (1, 64, 4096, aléatoire) : sorties identiques au bit près");
    }

    // 8. Mono : un seul canal, réglages de G
    {
        PhaseCore c; c.prepare (48000, 1);
        PhaseParams p; p.delayMs[0] = 0.5; p.invert[0] = true; c.setParams (p);
        std::vector<float> in (3000), l; for (auto& v : in) v = U (rng); l = in;
        float* io[1] = { l.data() }; c.process (io, 3000);
        const int L = c.getLatency(); bool exact = true;
        for (int i = 0; i < 3000; ++i) exact &= l[(size_t) i] == (i - L - 24 >= 0 ? -in[(size_t) (i - L - 24)] : 0.0f);
        check (exact, "mono : décalage et polarité de G, au bit près");
    }

    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}
