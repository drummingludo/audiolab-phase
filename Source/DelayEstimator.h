// Audio Lab · mesure du décalage entre deux signaux (brique commune, sans JUCE)
// Écrit pour Transversal Studio.
//
// Intercorrélation généralisée à pondération de phase (GCC-PHAT), sous forme analytique :
//  1. fenêtre de Hann sur les deux extraits, FFT avec zéros ajoutés (pas de repliement circulaire) ;
//  2. interspectre G = S · conj(R), ramené à un module de 1 dans la bande utile (pondération PHAT :
//     chaque fréquence pèse autant, la mesure ne dépend pas du timbre), fréquences négatives annulées ;
//  3. FFT inverse → intercorrélation complexe r(τ) : son module donne le décalage, sa phase au
//     sommet donne la rotation de phase entre les deux signaux (180° = polarité inversée).
// Convention : decalage > 0 → « sig » arrive APRÈS « ref » ; rotation θ : sig ≈ Rot_θ { ref retardé }.
// Pas pour le fil audio (allocation, FFT de grande taille) : à appeler depuis un fil de fond ou l'interface.
//
// Copie adaptée pour Lab Corr et Lab Phase 2.0.0 (la brique commune n'est pas modifiée) : étape 4 ajoutée,
//  4. affinage conjoint du décalage et de la rotation par régression pondérée de la phase de l'interspectre
//     (non pondéré PHAT : chaque fréquence pèse selon son énergie commune), autour du résultat de l'étape 3 :
//     précision ≈ 0,001 éch. et 0,05° au lieu de ≈ 0,01 éch. et 0,5° (mesuré, tests/quality_test.cpp) ;
//     appliqué seulement si la mesure est fiable (confiance ≥ 0,3) et si la correction reste sous 0,5 éch.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace audiolab::align
{
struct Result
{
    bool   valid = false;
    double lagSamples = 0;     // > 0 : sig en retard sur ref
    double lagMs = 0;
    double angleDeg = 0;       // rotation de phase mesurée, −180 à 180°
    bool   invert = false;     // |angle| > 90° : la polarité est inversée
    double residualDeg = 0;    // rotation restante après correction de polarité, −90 à 90°
    double confidence = 0;     // 0 à 1 : 1 = même signal, simplement décalé ; < 0,15 : mesure peu fiable
    double ambiguity = 0;      // rapport entre le 2e et le 1er sommet (0 = net, 1 = ambigu)
};

class DelayEstimator
{
public:
    using cd = std::complex<double>;

    static void fft (std::vector<cd>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        const double PI = 3.14159265358979323846;
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2 * PI / (double) len * (inverse ? 1 : -1);
            const cd wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cd w (1);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cd u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse) for (auto& x : a) x /= (double) n;
    }

    // ref, sig : n échantillons chacun ; maxLagMs : décalage cherché de −maxLag à +maxLag
    static Result estimate (const float* ref, const float* sig, int n, double fs, double maxLagMs,
                            double fLo = 40.0, double fHi = 16000.0)
    {
        Result res;
        if (n < 256 || fs <= 0) return res;
        size_t N = 1; while (N < (size_t) n * 2) N <<= 1;
        std::vector<cd> R (N), S (N);
        const double PI = 3.14159265358979323846;
        double eR = 0, eS = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * PI * (i + 0.5) / n);
            R[(size_t) i] = ref[i] * w; S[(size_t) i] = sig[i] * w;
            eR += (double) ref[i] * ref[i]; eS += (double) sig[i] * sig[i];
        }
        if (eR < 1e-12 * n || eS < 1e-12 * n) return res;            // un des deux signaux est muet
        fft (R, false); fft (S, false);
        // interspectre pondéré, partie analytique (fréquences positives seulement)
        const size_t kLo = std::max<size_t> (1, (size_t) std::ceil (fLo * N / fs));
        const size_t kHi = std::min (N / 2 - 1, (size_t) std::floor (std::min (fHi, 0.45 * fs) * N / fs));
        if (kHi <= kLo + 8) return res;
        std::vector<cd> G (N, cd (0));
        double gMax = 0;
        for (size_t k = kLo; k <= kHi; ++k) gMax = std::max (gMax, std::abs (S[k] * std::conj (R[k])));
        if (gMax <= 0) return res;
        size_t used = 0;
        for (size_t k = kLo; k <= kHi; ++k)
        {
            const cd g = S[k] * std::conj (R[k]);
            const double m = std::abs (g);
            if (m < 1e-9 * gMax) continue;                                // fréquence sans énergie : ignorée
            G[k] = g / m; ++used;
        }
        if (used < 8) return res;
        std::vector<cd> r (G);
        fft (r, true);
        for (auto& x : r) x *= (double) N / (double) used;                // |r| = 1 pour un décalage pur
        // sommet du module dans ±maxLag
        const int maxLag = std::max (1, std::min ((int) (N / 2) - 2, (int) std::lround (maxLagMs * 0.001 * fs)));
        auto at = [&] (int lag) { return r[(size_t) ((lag + (long) N) % (long) N)]; };
        int best = 0; double bv = -1;
        for (int lag = -maxLag; lag <= maxLag; ++lag) { const double v = std::abs (at (lag)); if (v > bv) { bv = v; best = lag; } }
        // 2e sommet (à plus de 2 ms ou 64 échantillons du premier) : ambiguïté
        const int guard = std::max (64, (int) std::lround (0.002 * fs));
        double second = 0;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
            if (std::abs (lag - best) > guard) second = std::max (second, std::abs (at (lag)));
        // affinage : parabole sur le module, puis évaluation exacte de r(τ) au décalage fractionnaire
        double frac = 0;
        if (best > -maxLag && best < maxLag)
        {
            const double a = std::abs (at (best - 1)), b = bv, c = std::abs (at (best + 1)), d = a - 2 * b + c;
            if (d < 0) frac = std::clamp (0.5 * (a - c) / d, -0.5, 0.5);
        }
        double tau = best + frac;
        cd rt (0);
        for (size_t k = kLo; k <= kHi; ++k)
            if (G[k] != cd (0)) rt += G[k] * std::polar (1.0, 2 * PI * (double) k * tau / (double) N);
        rt /= (double) used;
        // 4. affinage : ψ_k = arg (S_k R_k* e^{j2πkτ/N}) − θ ≈ a − 2πkδ/N ; moindres carrés pondérés par |S_k R_k*|
        double theta = std::arg (rt);
        if (std::abs (rt) >= 0.3)
            for (int pass = 0; pass < 2; ++pass)
            {
                double sw = 0, sk = 0, skk = 0, sp = 0, skp = 0;
                for (size_t k = kLo; k <= kHi; ++k)
                {
                    const cd g = S[k] * std::conj (R[k]);
                    const double wgt = std::abs (g);
                    if (wgt < 1e-9 * gMax) continue;
                    const double psi = std::arg (g * std::polar (1.0, 2 * PI * (double) k * tau / (double) N - theta));
                    if (std::abs (psi) > PI / 2) continue;                    // fréquence sans rapport : écartée
                    const double kk = (double) k;
                    sw += wgt; sk += wgt * kk; skk += wgt * kk * kk; sp += wgt * psi; skp += wgt * kk * psi;
                }
                const double den = sw * skk - sk * sk;
                if (sw <= 0 || std::abs (den) < 1e-30) break;
                const double slope = (sw * skp - sk * sp) / den, icpt = (sp - slope * sk) / sw;
                const double delta = -slope * (double) N / (2 * PI);
                if (std::abs (delta) > 0.5) break;                              // hors de portée : on garde l'étape 3
                tau += delta; theta += icpt;
            }
        res.valid = true;
        res.lagSamples = tau;
        res.lagMs = tau / fs * 1000;
        theta = std::remainder (theta, 2 * PI);
        res.angleDeg = theta * 180 / PI;
        res.invert = std::abs (res.angleDeg) > 90;
        double resid = res.invert ? res.angleDeg - (res.angleDeg > 0 ? 180 : -180) : res.angleDeg;
        res.residualDeg = resid;
        res.confidence = std::clamp (std::abs (rt), 0.0, 1.0);
        res.ambiguity = bv > 0 ? second / bv : 1;
        return res;
    }
};
} // namespace audiolab::align
