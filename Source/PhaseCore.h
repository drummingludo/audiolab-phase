// Moteur de « Audio Lab Phase » : inversion de polarité et décalage temporel de −10 à +10 ms par canal.
// Sans dépendance à JUCE (testable seul : tests/core_run.cpp).
//
// Principe
// - Le plugin annonce une latence fixe L = ⌈10 ms × fs⌉ + 32 échantillons, que le logiciel hôte compense.
//   Le retard interne vaut L + d (d = décalage voulu, de −10 à +10 ms) : d < 0 avance donc réellement le son.
// - Unité « Échantillons » : d est arrondi à l'échantillon entier, et la sortie est une copie exacte
//   de l'entrée (au bit près), simplement décalée (et retournée si la polarité est inversée).
// - Unité « ms » : pas de 1 µs ; quand d tombe entre deux échantillons, lecture par un filtre sinc
//   fenêtré (Kaiser β = 8, 64 points, somme normalisée à 1) : réponse plate à ±0,001 dB jusqu'à 20 kHz
//   à 44,1 kHz, erreur de retard < 0,001 échantillon. Si d tombe juste sur un échantillon : lecture exacte.
// - Changement de décalage : fondu enchaîné de 20 ms entre l'ancienne et la nouvelle position (aucun clic,
//   aucun effet « Doppler »). Changement de polarité : gain qui passe de +1 à −1 en 20 ms.
// - Bypass : fondu de 20 ms vers l'entrée simplement retardée de L (reste aligné avec les autres pistes).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace audiolab
{
struct PhaseParams
{
    double delayMs[2] = { 0, 0 };   // décalage voulu par canal (G, D), −10 … +10 ms
    bool invert[2] = { false, false };
    bool samples = true;            // true : arrondi à l'échantillon entier ; false : ms au µs près
    bool bypass = false;
};

class PhaseCore
{
    // égalité exacte voulue (valeurs recopiées, jamais recalculées)
    static bool same (double a, double b) { return ! (a < b) && ! (a > b); }

public:
    static constexpr int taps = 64, half = taps / 2;    // filtre de lecture fractionnaire
    static constexpr double maxMs = 10.0;
    static constexpr double fadeSec = 0.02;              // fondus (décalage, polarité, bypass)

    static int latencyFor (double sampleRate) { return (int) std::ceil (maxMs * sampleRate / 1000.0 - 1e-9) + half; }

    void prepare (double sampleRate, int numChannels = 2)
    {
        fs = sampleRate;
        nCh = std::clamp (numChannels, 1, 2);
        maxSamples = maxMs * fs / 1000.0;
        latency = latencyFor (fs);
        const int need = 2 * latency + taps + 4;
        int sz = 1; while (sz < need) sz <<= 1;
        mask = sz - 1;
        for (auto& c : ch) { c.buf.assign ((size_t) sz, 0.0f); c.w = 0; }
        fadeLen = std::max (1, (int) std::lround (fadeSec * fs));
        snap = true;
    }

    int getLatency() const { return latency; }
    double getSampleRate() const { return fs; }

    // Décalage effectivement appliqué (en échantillons, peut être fractionnaire en mode ms)
    static double effectiveSamples (double ms, bool samples, double sampleRate)
    {
        const double lim = maxMs * sampleRate / 1000.0;
        if (samples)
        {
            double d = std::round (ms * sampleRate / 1000.0);
            const double li = std::floor (lim + 1e-9);
            return std::clamp (d, -li, li);
        }
        const double us = std::round (ms * 1000.0);           // grille de 1 µs
        return std::clamp (us * sampleRate / 1.0e6, -lim, lim);
    }

    void setParams (const PhaseParams& p)
    {
        for (int c = 0; c < 2; ++c)
        {
            target[c] = (double) latency + effectiveSamples (p.delayMs[c], p.samples, fs);
            gTarget[c] = p.invert[c] ? -1.0 : 1.0;
        }
        bTarget = p.bypass ? 1.0 : 0.0;
        if (snap)
        {
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                setTap (s.a, target[c]);
                s.fading = false; s.fadePos = 0;
                s.g = gTarget[c];
            }
            b = bTarget;
            snap = false;
        }
    }

    // Traitement sur place : io[0] = G, io[1] = D (si 2 canaux)
    void process (float* const* io, int n)
    {
        for (int c = 0; c < nCh; ++c)
        {
            auto& s = ch[c];
            float* x = io[c];
            const double gT = gTarget[c], step = 1.0 / fadeLen;
            double bb = b;
            for (int i = 0; i < n; ++i)
            {
                s.buf[(size_t) s.w] = x[i];
                if (! s.fading && ! same (s.a.D, target[c])) startFade (s, target[c]);
                double y = read (s, s.a);
                if (s.fading)
                {
                    const double yb = read (s, s.bt);
                    const double t = (double) ++s.fadePos / fadeLen;
                    y = y + (yb - y) * t;
                    if (s.fadePos >= fadeLen) { s.a = s.bt; s.fading = false; y = yb; }
                }
                // polarité : rampe linéaire de 20 ms vers ±1
                if (! same (s.g, gT))
                {
                    s.g += gT > s.g ? 2.0 * step : -2.0 * step;
                    if ((gT > 0 && s.g >= gT) || (gT < 0 && s.g <= gT)) s.g = gT;
                }
                y *= s.g;
                // bypass : entrée retardée de la latence (alignée), au bit près une fois le fondu fini
                if (! same (bb, bTarget))
                {
                    bb += bTarget > bb ? step : -step;
                    if ((bTarget > 0.5 && bb >= 1.0) || (bTarget < 0.5 && bb <= 0.0)) bb = bTarget;
                }
                float out;
                if (same (bb, 0.0)) out = (float) y;
                else
                {
                    const float dry = s.buf[(size_t) ((s.w - latency) & mask)];
                    out = same (bb, 1.0) ? dry : (float) (y * (1.0 - bb) + dry * bb);
                }
                x[i] = out;
                s.w = (s.w + 1) & mask;
            }
            if (c == nCh - 1) b = bb;
        }
    }

    void reset()
    {
        for (auto& c : ch) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.w = 0; }
        snap = true;
    }

    // Pour l'interface et les tests
    double currentDelaySamples (int c) const { return ch[c].a.D - latency; }
    bool isFading (int c) const { return ch[c].fading; }

private:
    struct Tap
    {
        double D = 0;        // retard total en échantillons (latence + décalage)
        int base = 0;        // retard entier (lecture exacte) ou premier point du filtre
        bool frac = false;
        double h[taps] {};
    };
    struct Chan
    {
        std::vector<float> buf;
        int w = 0;
        Tap a, bt;
        bool fading = false;
        int fadePos = 0;
        double g = 1;
    };

    static double besselI0 (double x)
    {
        double sum = 1, term = 1;
        const double q = x * x / 4;
        for (int k = 1; k < 60; ++k)
        {
            term *= q / ((double) k * k);
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }

    void setTap (Tap& t, double D) const
    {
        t.D = D;
        const double r = std::round (D);
        if (std::abs (D - r) < 1e-7)
        {
            t.frac = false;
            t.base = (int) r;
            return;
        }
        const int Di = (int) std::floor (D);
        const double f = D - Di;
        t.frac = true;
        t.base = Di - (half - 1);        // point k du filtre = x[n − base − k], centre en k = half − 1
        constexpr double beta = 8.0, pi = 3.14159265358979323846;
        const double i0b = besselI0 (beta);
        double sum = 0;
        for (int k = 0; k < taps; ++k)
        {
            const double u = k - (half - 1) - f;                 // écart au point idéal
            const double sinc = std::abs (u) < 1e-12 ? 1.0 : std::sin (pi * u) / (pi * u);
            const double v = u / half;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - v * v))) / i0b;
            t.h[k] = sinc * w;
            sum += t.h[k];
        }
        for (auto& h : t.h) h /= sum;
    }

    void startFade (Chan& s, double D)
    {
        setTap (s.bt, D);
        s.fading = true;
        s.fadePos = 0;
    }

    double read (const Chan& s, const Tap& t) const
    {
        if (! t.frac) return (double) s.buf[(size_t) ((s.w - t.base) & mask)];
        double acc = 0;
        int idx = s.w - t.base;
        for (int k = 0; k < taps; ++k)
            acc += t.h[k] * (double) s.buf[(size_t) ((idx - k) & mask)];
        return acc;
    }

    double fs = 48000, maxSamples = 480;
    int nCh = 2, latency = 512, mask = 1023, fadeLen = 960;
    Chan ch[2];
    double target[2] = { 0, 0 }, gTarget[2] = { 1, 1 }, bTarget = 0, b = 0;
    bool snap = true;
};
} // namespace audiolab
