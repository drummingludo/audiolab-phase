// Moteur de « Audio Lab Phase » : inversion de polarité, décalage temporel de −10 à +10 ms et (2.0.0) rotation
// de phase de 0 à 360° par canal. Sans dépendance à JUCE (testable seul : tests/core_test.cpp, tests/quality_test.cpp).
//
// Principe
// - Mode de latence « Standard » (défaut) : le plugin annonce une latence fixe
//     L = ⌈10 ms × fs⌉ + 32 (avance et lecture fractionnaire, comme en 1.x) + M (transformée de Hilbert, 2.0.0),
//   que le logiciel hôte compense. Le retard interne vaut L + d (d = décalage voulu, de −10 à +10 ms) :
//   d < 0 avance donc réellement le son. M = ⌈14,25 ms × fs⌉ (impair) : 685 éch. à 48 kHz, L = 1197 éch. (24,9 ms).
// - Mode « Zero » (2.0.0) : latence nulle ; décalages de 0 à +10 ms seulement (pas d'avance), à l'échantillon entier
//   (une lecture fractionnaire sans latence est impossible), pas de rotation. Sortie = copie exacte, décalée.
// - Unité « Échantillons » : d est arrondi à l'échantillon entier, et la sortie est une copie exacte
//   de l'entrée (au bit près), simplement décalée (et retournée si la polarité est inversée).
// - Unité « ms » : pas de 1 µs à la souris et au clavier (grille interne de 10 ns, 2.0.0) ; quand d tombe entre deux échantillons, lecture par un filtre sinc
//   fenêtré (Kaiser β = 8, 64 points, somme normalisée à 1) : réponse plate à ±0,001 dB jusqu'à 20 kHz
//   à 44,1 kHz, erreur de retard < 0,001 échantillon. Si d tombe juste sur un échantillon : lecture exacte.
// - Rotation de phase θ (2.0.0) : y = z·cos θ − H{z}·sin θ (toutes les fréquences avancées de θ, module inchangé),
//   H = transformée de Hilbert à phase linéaire (RIF antisymétrique de 2M + 1 points, un sur deux nul,
//   fenêtre de Kaiser β = 3,5) appliquée au signal déjà décalé z ; z est retardé de M pour rester aligné.
//   Mesuré : rotation exacte à ±0,6° et module à ±0,17 dB de 40 Hz à 18 kHz (44,1 à 192 kHz) ; en dessous de 40 Hz
//   la rotation s'atténue progressivement (rien au continu). À 0° (ou 180°), aucun calcul : copie exacte.
//   Convention identique à DelayEstimator (Lab Corr) : sig ≈ Rot_θ { ref retardé }.
// - Changement de décalage : fondu enchaîné de 20 ms entre l'ancienne et la nouvelle position (aucun clic,
//   aucun effet « Doppler »). Changement de polarité : gain qui passe de +1 à −1 en 20 ms.
//   Changement de rotation : l'angle glisse en 20 ms par le plus court chemin (module constant).
//   Changement de mode de latence : fondu de sortie de 10 ms, bascule, fondu d'entrée de 10 ms.
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
    double rotDeg[2] = { 0, 0 };    // rotation de phase par canal, 0 … 360° (2.0.0)
    bool samples = true;            // true : arrondi à l'échantillon entier ; false : ms au µs près
    bool bypass = false;
    bool zeroLatency = false;       // mode de latence « Zero » (2.0.0)
};

class PhaseCore
{
    // égalité exacte voulue (valeurs recopiées, jamais recalculées)
    static bool same (double a, double b) { return ! (a < b) && ! (a > b); }

public:
    static constexpr int taps = 64, half = taps / 2;    // filtre de lecture fractionnaire
    static constexpr double maxMs = 10.0;
    static constexpr double fadeSec = 0.02;              // fondus (décalage, polarité, rotation, bypass)
    static constexpr double hilbertMs = 14.25;           // demi-longueur de la transformée de Hilbert
    static constexpr double hilbertBeta = 3.5;

    // latence de la partie « décalage » (celle des versions 1.x)
    static int delayLatencyFor (double sampleRate) { return (int) std::ceil (maxMs * sampleRate / 1000.0 - 1e-9) + half; }
    // demi-longueur M de la transformée de Hilbert (impaire)
    static int hilbertHalfFor (double sampleRate)
    {
        int M = (int) std::ceil (hilbertMs * sampleRate / 1000.0 - 1e-9);
        return M % 2 == 0 ? M + 1 : M;
    }
    // latence annoncée : mode Standard (défaut), ou Zero
    static int latencyFor (double sampleRate) { return delayLatencyFor (sampleRate) + hilbertHalfFor (sampleRate); }
    static int latencyFor (double sampleRate, bool zero) { return zero ? 0 : latencyFor (sampleRate); }

    void prepare (double sampleRate, int numChannels = 2)
    {
        fs = sampleRate;
        nCh = std::clamp (numChannels, 1, 2);
        maxSamples = maxMs * fs / 1000.0;
        L1std = delayLatencyFor (fs);
        M = hilbertHalfFor (fs);
        const int Lstd = L1std + M;
        const int need = 2 * Lstd + 2 * M + taps + 8;
        int sz = 1; while (sz < need) sz <<= 1;
        mask = sz - 1;
        Zh = 1; while (Zh < M + 4) Zh <<= 1;
        for (auto& c : ch)
        {
            c.buf.assign ((size_t) sz, 0.0f); c.w = 0;
            for (auto& r : c.zp) r.assign ((size_t) (2 * Zh), 0.0f);
            c.zt = 0;
        }
        // transformée de Hilbert : coefficients des points impairs k = 1, 3, … M (le RIF est antisymétrique),
        // en float pour le calcul (h dans l'ordre et à l'envers : deux produits scalaires contigus)
        J = (M + 1) / 2;
        hc.assign ((size_t) J, 0.0); hf.assign ((size_t) J, 0.0f); hr.assign ((size_t) J, 0.0f);
        const double i0b = besselI0 (hilbertBeta);
        for (int j = 0; j < J; ++j)
        {
            const int k = 2 * j + 1;
            const double v = (double) k / (M + 1);
            hc[(size_t) j] = 2.0 / (pi * k) * besselI0 (hilbertBeta * std::sqrt (std::max (0.0, 1.0 - v * v))) / i0b;
        }
        for (int j = 0; j < J; ++j) { hf[(size_t) j] = (float) hc[(size_t) j]; hr[(size_t) (J - 1 - j)] = (float) hc[(size_t) j]; }
        fadeLen = std::max (1, (int) std::lround (fadeSec * fs));
        modeFadeLen = std::max (1, fadeLen / 2);
        zero = false; applyModeLatency();
        modeStage = 0; modeG = 1;
        snap = true;
    }

    int getLatency() const { return latency; }
    double getSampleRate() const { return fs; }
    bool isZeroLatency() const { return zero; }
    int hilbertHalf() const { return M; }

    // Décalage effectivement appliqué (en échantillons, peut être fractionnaire en mode ms)
    static double effectiveSamples (double ms, bool samples, double sampleRate, bool zeroLatency = false)
    {
        const double lim = maxMs * sampleRate / 1000.0;
        if (zeroLatency)   // latence nulle : retards seulement, à l'échantillon entier
            return std::clamp (std::round (ms * sampleRate / 1000.0), 0.0, std::floor (lim + 1e-9));
        if (samples)
        {
            double d = std::round (ms * sampleRate / 1000.0);
            const double li = std::floor (lim + 1e-9);
            return std::clamp (d, -li, li);
        }
        // grille de 10 ns (1 µs en 1.x) : les valeurs réglées à la souris ou au clavier (pas de 1 µs) tombent juste ;
        // celles du bouton Align (précision ≈ 0,001 éch.) ne sont plus arrondies au µs
        const double tens = std::round (ms * 1.0e5);
        return std::clamp (tens * sampleRate / 1.0e8, -lim, lim);
    }
    // Rotation effectivement appliquée (degrés, 0 … 360), 0 en mode Zero
    static double effectiveRotation (double deg, bool zeroLatency) { return zeroLatency ? 0.0 : wrap360 (deg); }

    void setParams (const PhaseParams& p)
    {
        pend = p;
        if (snap)
        {
            zero = p.zeroLatency; applyModeLatency();
            computeTargets();
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                setTap (s.a, target[c]);
                s.fading = false; s.fadePos = 0;
                s.g = gTarget[c];
                s.rot.snapTo (rotTarget[c]);
            }
            b = bTarget;
            modeStage = 0; modeG = 1;
            snap = false;
            return;
        }
        if (p.zeroLatency != zero && modeStage == 0) modeStage = 1;   // changement de mode : fondu de sortie d'abord
        if (modeStage == 0) computeTargets();
    }

    // Traitement sur place : io[0] = G, io[1] = D (si 2 canaux)
    void process (float* const* io, int n)
    {
        int stage = modeStage; double mg = modeG;
        for (int c = 0; c < nCh; ++c)
        {
            auto& s = ch[c];
            float* x = io[c];
            const double gT = gTarget[c], step = 1.0 / fadeLen, mstep = 1.0 / modeFadeLen;
            double bb = b;
            stage = modeStage; mg = modeG;
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
                // rotation de phase (mode Standard) : z est retardé de M, sa transformée de Hilbert centrée sur z(n − M)
                const float z = (float) y;
                putZ (s, s.zt, z);
                if (! zero)
                {
                    s.rot.setTarget (rotTarget[c], fadeLen);
                    s.rot.tick();
                    const double z0 = (double) getZ (s, s.zt - (std::uint64_t) M);   // z(n − M)
                    if (same (s.rot.sn, 0.0)) y = same (s.rot.cs, 1.0) ? z0 : s.rot.cs * z0;
                    else y = s.rot.cs * z0 - s.rot.sn * hilbert (s, s.zt);
                }
                ++s.zt;
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
                // changement de mode de latence : fondu de sortie (1), bascule, fondu d'entrée (2)
                if (stage == 1) { mg -= mstep; if (mg <= 0) mg = 0; }
                else if (stage == 2) { mg += mstep; if (mg >= 1) { mg = 1; stage = 0; } }
                if (! same (mg, 1.0)) out = (float) (out * mg);
                x[i] = out;
                s.w = (s.w + 1) & mask;
            }
            if (c == nCh - 1) b = bb;
        }
        modeStage = stage; modeG = mg;
        if (modeStage == 1 && same (modeG, 0.0)) switchMode();
    }

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.w = 0;
            for (auto& r : c.zp) std::fill (r.begin(), r.end(), 0.0f);
            c.zt = 0;
        }
        snap = true;
    }

    // Pour l'interface et les tests
    double currentDelaySamples (int c) const { return ch[c].a.D - L1; }
    double currentRotation (int c) const { return ch[c].rot.cur; }
    bool isFading (int c) const { return ch[c].fading || ch[c].rot.ramp; }

    // Réponse de la transformée de Hilbert à la fréquence f (module ; 1 = idéal) : tests
    double hilbertGain (double f) const
    {
        const double w = 2 * pi * f / fs;
        double a = 0;
        for (int j = 0; j < (int) hc.size(); ++j) a += 2 * hc[(size_t) j] * std::sin (w * (2 * j + 1));
        return a;
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    struct Tap
    {
        double D = 0;        // retard total en échantillons (latence + décalage)
        int base = 0;        // retard entier (lecture exacte) ou premier point du filtre
        bool frac = false;
        double h[taps] {};
    };
    // Rotation : angle courant, glissement de 20 ms par le plus court chemin, cos / sin exacts aux angles droits
    struct Rot
    {
        double cur = 0, start = 0, delta = 0, end = 0, cs = 1, sn = 0;
        int pos = 0, len = 1;
        bool ramp = false;
        void exact()
        {
            if (same (cur, 0.0)) { cs = 1; sn = 0; }
            else if (same (cur, 90.0)) { cs = 0; sn = 1; }
            else if (same (cur, 180.0)) { cs = -1; sn = 0; }
            else if (same (cur, 270.0)) { cs = 0; sn = -1; }
            else { cs = std::cos (cur * pi / 180); sn = std::sin (cur * pi / 180); }
        }
        void snapTo (double t) { cur = end = t; ramp = false; exact(); }
        void setTarget (double t, int fadeLen)
        {
            if (same (t, ramp ? end : cur)) return;
            start = cur; end = t; len = fadeLen; pos = 0; ramp = true;
            delta = t - cur;
            if (delta > 180) delta -= 360; else if (delta <= -180) delta += 360;
        }
        void tick()
        {
            if (! ramp) return;
            if (++pos >= len) { snapTo (end); return; }
            cur = wrap360 (start + delta * pos / len);
            cs = std::cos (cur * pi / 180); sn = std::sin (cur * pi / 180);
        }
    };
    struct Chan
    {
        std::vector<float> buf;       // entrée
        // signal décalé z, rangé par parité de l'indice (les points utiles de la transformée de Hilbert ont tous la même
        // parité : lecture contiguë) et écrit deux fois (anneau « miroir » : fenêtre de M + 1 points toujours d'un seul tenant)
        std::vector<float> zp[2];
        std::uint64_t zt = 0;         // nombre d'échantillons de z écrits (indice absolu)
        int w = 0;
        Tap a, bt;
        bool fading = false;
        int fadePos = 0;
        double g = 1;
        Rot rot;
    };

    static double wrap360 (double d)
    {
        d = std::fmod (d, 360.0);
        if (d < 0) d += 360;
        return d >= 360 ? 0.0 : d;
    }

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

    void putZ (Chan& s, std::uint64_t t, float z) const
    {
        auto& r = s.zp[t & 1];
        const auto i = (size_t) ((t >> 1) & (std::uint64_t) (Zh - 1));
        r[i] = z; r[i + (size_t) Zh] = z;
    }
    float getZ (const Chan& s, std::uint64_t t) const { return s.zp[t & 1][(size_t) ((t >> 1) & (std::uint64_t) (Zh - 1))]; }

    // produit scalaire en 8 sommes partielles (vectorisable, même résultat quelle que soit la taille des blocs)
    static float dot (const float* a, const float* b, int n)
    {
        float s[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        int i = 0;
        for (; i + 8 <= n; i += 8)
            for (int k = 0; k < 8; ++k) s[k] += a[i + k] * b[i + k];
        float r = ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
        for (; i < n; ++i) r += a[i] * b[i];
        return r;
    }

    // H{z}(n − M) = Σ c_k [z(n − M − k) − z(n − M + k)], k impairs de 1 à M ; t = indice absolu de z(n).
    // Tous ces points ont la parité de t : z(t − M − 1 − 2j) et z(t − M + 1 + 2j), j = 0 … J − 1.
    double hilbert (const Chan& s, std::uint64_t t) const
    {
        const auto& r = s.zp[t & 1];
        const std::uint64_t start = (t - (std::uint64_t) (M + 1)) / 2 - (std::uint64_t) (J - 1);   // plus ancien point (indice de parité)
        const float* a = &r[(size_t) (start & (std::uint64_t) (Zh - 1))];                         // 2J points d'un seul tenant
        return (double) dot (hr.data(), a, J) - (double) dot (hf.data(), a + J, J);
    }

    void applyModeLatency()
    {
        L1 = zero ? 0 : L1std;
        latency = zero ? 0 : L1std + M;
    }

    void computeTargets()
    {
        for (int c = 0; c < 2; ++c)
        {
            target[c] = (double) L1 + effectiveSamples (pend.delayMs[c], pend.samples, fs, zero);
            gTarget[c] = pend.invert[c] ? -1.0 : 1.0;
            rotTarget[c] = effectiveRotation (pend.rotDeg[c], zero);
        }
        bTarget = pend.bypass ? 1.0 : 0.0;
    }

    // Bascule de mode (sortie à 0) : nouvelles positions sans fondu ; en Standard, l'historique de z est recalculé
    // depuis l'entrée pour que la rotation reparte sans trou
    void switchMode()
    {
        zero = pend.zeroLatency; applyModeLatency();
        computeTargets();
        for (int c = 0; c < nCh; ++c)
        {
            auto& s = ch[c];
            setTap (s.a, target[c]);
            s.fading = false; s.fadePos = 0;
            s.rot.snapTo (rotTarget[c]);
            if (zero) continue;
            // z(n − 1 − j) pour j = 0 … 2M : lecture de l'entrée au retard D (+ j)
            const int wNow = s.w;
            for (int j = 0; j <= 2 * M && (std::uint64_t) j < s.zt; ++j)
            {
                const int wp = (wNow - 1 - j) & mask;
                putZ (s, s.zt - 1 - (std::uint64_t) j, (float) (readAt (s, s.a, wp) * s.g));
            }
        }
        modeStage = 2; modeG = 0;
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
        constexpr double beta = 8.0;
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

    // lecture à la position d'écriture wp (wp = s.w pour l'échantillon en cours)
    double readAt (const Chan& s, const Tap& t, int wp) const
    {
        if (! t.frac) return (double) s.buf[(size_t) ((wp - t.base) & mask)];
        double acc = 0;
        int idx = wp - t.base;
        for (int k = 0; k < taps; ++k)
            acc += t.h[k] * (double) s.buf[(size_t) ((idx - k) & mask)];
        return acc;
    }
    double read (const Chan& s, const Tap& t) const { return readAt (s, t, s.w); }

    double fs = 48000, maxSamples = 480;
    int nCh = 2, latency = 512, L1 = 512, L1std = 512, M = 685, J = 343, Zh = 1024, mask = 1023, fadeLen = 960, modeFadeLen = 480;
    Chan ch[2];
    std::vector<double> hc;
    std::vector<float> hf, hr;
    double target[2] = { 0, 0 }, gTarget[2] = { 1, 1 }, rotTarget[2] = { 0, 0 }, bTarget = 0, b = 0;
    PhaseParams pend;
    bool snap = true, zero = false;
    int modeStage = 0;   // 0 : stable ; 1 : fondu de sortie avant bascule ; 2 : fondu d'entrée
    double modeG = 1;
};
} // namespace audiolab
