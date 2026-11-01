#include "Display.h"
#include "Widgets.h"

namespace audiolab
{
using juce::String;
static String U (const char* s) { return String::fromUTF8 (s); }

PhaseDisplay::State PhaseDisplay::state() const
{
    State st;
    const auto p = proc.currentParams();
    st.fs = proc.displayRate();
    st.mono = proc.isMono();
    st.bypass = p.bypass;
    st.samplesUnit = p.samples;
    for (int c = 0; c < 2; ++c)
    {
        st.s[c] = PhaseCore::effectiveSamples (p.delayMs[c], p.samples, st.fs);
        st.inv[c] = p.invert[c];
    }
    if (st.mono) { st.s[1] = st.s[0]; st.inv[1] = st.inv[0]; }
    return st;
}

String PhaseDisplay::combText (const State& st)
{
    if (st.bypass) return U ("Bypass : son d'origine, aucun creux");
    const double dt = (st.s[1] - st.s[0]) / st.fs;      // écart D − G en secondes
    const bool opposite = st.inv[0] != st.inv[1];
    if (std::abs (dt) < 1e-12)
        return opposite ? U ("Annulation totale : G et D en opposition") : U ("Aucun creux : G et D en phase");
    const double spacing = 1.0 / std::abs (dt);
    if (opposite) return U ("Graves annulés, puis un creux tous les ") + params::fmtHz (spacing);
    return U ("Premier creux : ") + params::fmtHz (spacing / 2) + U (", puis tous les ") + params::fmtHz (spacing);
}

void PhaseDisplay::updateTooltip()
{
    const double lat = proc.latencySamples() * 1000.0 / proc.displayRate();
    if (view == 0)
        setTooltip (U ("Temps : où sortent G et D par rapport au son d'origine (trait pointillé). Vers le haut : polarité normale ; "
                       "vers le bas : inversée. Pour pouvoir avancer un son, le plugin annonce une latence fixe de ")
                    + params::num (lat, 2) + U (" ms, que votre logiciel compense automatiquement."));
    else
        setTooltip (U ("Somme mono : ce que donne G + D (une enceinte, un téléphone, une console en mono). Un écart de temps entre G et D "
                       "creuse des fréquences régulièrement espacées (filtre en peigne) ; une polarité inversée annule les graves."));
}

void PhaseDisplay::paint (juce::Graphics& g)
{
    const auto& p = theme.pal();
    auto b = getLocalBounds().toFloat();
    juce::Path box; box.addRoundedRectangle (b, 10);
    g.setColour (p.surface); g.fillPath (box);
    const auto st = state();
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (box);
        if (view == 1 && ! st.mono) paintSum (g, st); else paintTime (g, st);
    }
    g.setColour (p.rail); g.strokePath (box, juce::PathStrokeType (1));
}

// Petite flèche horizontale (la police n'a pas de flèches)
static void arrow (juce::Graphics& g, float x, float y, bool right, juce::Colour c)
{
    juce::Path a;
    const float d = right ? 1.0f : -1.0f;
    a.startNewSubPath (x - 4 * d, y); a.lineTo (x + 4 * d, y);
    a.startNewSubPath (x + 1 * d, y - 3); a.lineTo (x + 4 * d, y); a.lineTo (x + 1 * d, y + 3);
    g.setColour (c);
    g.strokePath (a, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

static double niceStep (double v)
{
    for (double s : { 25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 2500.0, 5000.0 }) if (s >= v * 0.8) return s;
    return 10000.0;
}

void PhaseDisplay::paintTime (juce::Graphics& g, const State& st)
{
    const auto& p = theme.pal();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float L = 22, R = w - 22, T = 32, B = h - 26, mid = (T + B) / 2, xc = (L + R) / 2;
    const double lim = params::maxMs * st.fs / 1000.0;          // 10 ms en échantillons
    auto xAt = [&] (double samples) { return xc + (float) (samples / lim) * (R - L) / 2; };

    // grille verticale et graduations
    g.setFont (theme.ui (8));
    auto tick = [&] (double samples, const String& label)
    {
        const float x = xAt (samples);
        g.setColour (p.rail); g.fillRect (x - 0.5f, T, 1.0f, B - T);
        if (label.isNotEmpty())
        {
            g.setColour (p.soft);
            g.drawText (label, juce::Rectangle<float> (x - 30, B + 5, 60, 12), juce::Justification::centred, false);
        }
    };
    if (st.samplesUnit)
    {
        // graduations rondes ; pas de chiffre trop près du bord (chiffre de la limite à la place)
        const double stp = niceStep (lim / 2.5);
        for (double s = stp; s < lim - 1e-9; s += stp)
        {
            const bool room = s < lim - stp * 0.6;
            tick (s, room ? "+" + params::num (s, 0) : String());
            tick (-s, room ? params::sign (-1) + params::num (s, 0) : String());
        }
        tick (lim, "+" + params::num (std::floor (lim + 1e-9), 0) + U (" éch."));
        tick (-lim, params::sign (-1) + params::num (std::floor (lim + 1e-9), 0) + U (" éch."));
    }
    else
    {
        for (int k = 1; k <= 10; ++k)
        {
            const double s = k * lim / 10;
            const String lb = k == 5 ? "5" : k == 10 ? "10 ms" : String();
            tick (s, lb.isEmpty() ? lb : "+" + lb);
            tick (-s, lb.isEmpty() ? lb : params::sign (-1) + lb);
        }
    }
    // ligne de base et son d'origine (0, pointillés)
    g.setColour (p.rail); g.fillRect (L, mid - 0.5f, R - L, 1.0f);
    {
        juce::Path z; z.startNewSubPath (xc, T - 4); z.lineTo (xc, B);
        juce::Path dashed; const float dl[] = { 3, 3 };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, z, dl, 2);
        g.setColour (p.mark2); g.fillPath (dashed);
        g.setColour (p.soft);
        g.drawText ("0", juce::Rectangle<float> (xc - 20, B + 5, 40, 12), juce::Justification::centred, false);
    }

    // en-tête : avance à gauche, retard à droite, écart au centre
    g.setFont (theme.ui (10));
    g.setColour (p.soft);
    arrow (g, L + 5, 17, false, p.soft);
    g.drawText ("Avance", juce::Rectangle<float> (L + 14, 10, 80, 14), juce::Justification::centredLeft, false);
    g.drawText ("Retard", juce::Rectangle<float> (R - 94, 10, 80, 14), juce::Justification::centredRight, false);
    arrow (g, R - 5, 17, true, p.soft);
    {
        String info;
        if (st.bypass) info = U ("Bypass : son d'origine (aligné)");
        else if (st.mono) info = U ("Mono");
        else
        {
            const double dms = (st.s[1] - st.s[0]) * 1000.0 / st.fs;
            info = std::abs (dms) < 1e-9 ? U ("G et D au même endroit")
                                         : U ("Écart D − G : ") + params::fmtMs (dms) + U (" · ") + params::fmtDist (dms);
        }
        g.setColour (p.text2);
        drawTabular (g, theme, info, 10, 400, juce::Rectangle<float> (xc - 160, 10, 320, 14), juce::Justification::centred);
    }

    // impulsions G et D (en bypass : le son d'origine, en gris)
    const int n = st.mono ? 1 : 2;
    const bool same = n == 2 && std::abs (st.s[0] - st.s[1]) < 1e-9 && st.inv[0] == st.inv[1];
    for (int c = n - 1; c >= 0; --c)
    {
        const double s = st.bypass ? 0.0 : st.s[c];
        const bool inv = st.bypass ? false : st.inv[c];
        if (c == 1 && (same || st.bypass)) continue;
        const float x = xAt (s);
        const float tip = inv ? B - 6 : T + 6;
        const juce::Colour col = st.bypass ? p.soft : p.orange;
        if (! st.bypass)
        {
            g.setColour (col.withAlpha (0.18f)); g.fillRect (x - 2.5f, std::min (mid, tip), 5.0f, std::abs (tip - mid));
        }
        g.setColour (col); g.fillRect (x - 1.25f, std::min (mid, tip), 2.5f, std::abs (tip - mid));
        // pastille : orange, bord #1D1E1C, halo
        g.setColour (col.withAlpha (0.25f)); g.fillEllipse (x - 7, tip - 7, 14, 14);
        g.setColour (p.bg); g.fillEllipse (x - 5.5f, tip - 5.5f, 11, 11);
        g.setColour (col); g.fillEllipse (x - 4, tip - 4, 8, 8);
        // nom du canal à côté de la pastille (avec « Ø » si la polarité est inversée)
        String name = st.bypass ? U ("Original") : st.mono ? String() : (same ? String ("G D") : String (c == 0 ? "G" : "D"));
        if (inv) name = (name.isEmpty() ? String() : name + " ") + U ("Ø");
        if (name.isNotEmpty())
        {
            g.setFont (theme.ui (10, 600));
            const float tw = textWidth (g.getCurrentFont(), name);
            const bool left = x + 10 + tw > R;
            g.setColour (st.bypass ? p.soft : p.text);
            g.drawText (name, juce::Rectangle<float> (left ? x - 10 - tw : x + 10, tip - 7, tw + 2, 14),
                        left ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
        }
    }
}

void PhaseDisplay::paintSum (juce::Graphics& g, const State& st)
{
    const auto& p = theme.pal();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float L = 42, R = w - 18, T = 32, B = h - 26;
    const double fMin = 20, fMax = 20000, dbMin = -30;
    auto xAt = [&] (double f) { return L + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * (R - L); };
    auto yAt = [&] (double db) { return T + (float) (juce::jlimit (dbMin, 0.0, db) / dbMin) * (B - T); };

    // grille
    g.setFont (theme.ui (8));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (p.rail); g.fillRect (xAt (f) - 0.5f, T, 1.0f, B - T);
    }
    for (auto [f, lb] : { std::pair<double, const char*> { 20, "20" }, { 100, "100" }, { 1000, "1k" }, { 10000, "10k" }, { 20000, "20k" } })
    {
        g.setColour (p.soft);
        const float x = xAt (f);
        const auto j = f <= 20 ? juce::Justification::centredLeft : f >= 20000 ? juce::Justification::centredRight : juce::Justification::centred;
        g.drawText (lb, juce::Rectangle<float> (f <= 20 ? x : f >= 20000 ? x - 40 : x - 20, B + 5, f <= 20 || f >= 20000 ? 40.0f : 40.0f, 12), j, false);
    }
    for (int db = 0; db >= -24; db -= 6)
    {
        g.setColour (p.rail); g.fillRect (L, yAt (db) - 0.5f, R - L, 1.0f);
        if (db % 12 == 0)
        {
            g.setColour (p.soft);
            g.drawText (db == 0 ? String ("0 dB") : params::sign (-1) + String (std::abs (db)), juce::Rectangle<float> (L - 38, yAt (db) - 6, 32, 12),
                        juce::Justification::centredRight, false);
        }
    }
    g.setColour (p.rail); g.fillRect (L, B - 0.5f, R - L, 1.0f);

    // courbe : |gG + gD·e^(−j2πfΔ)| / 2  (0 dB = G et D identiques).
    // Dessin exact colonne par colonne (2 par pixel) : minimum et maximum réels de la colonne, creux et sommets
    // calculés (pas d'échantillonnage) ; quand le peigne devient plus serré que l'écran, il apparaît comme une bande.
    const double dt = st.bypass ? 0.0 : (st.s[1] - st.s[0]) / st.fs, adt = std::abs (dt);
    const double gl = st.bypass || ! st.inv[0] ? 1.0 : -1.0, gr = st.bypass || ! st.inv[1] ? 1.0 : -1.0;
    const bool opposite = gl * gr < 0;
    auto magDb = [&] (double f)
    {
        const double ph = 2 * juce::MathConstants<double>::pi * f * dt;
        const double re = gl + gr * std::cos (ph), im = -gr * std::sin (ph);
        return 20 * std::log10 (std::max (1e-9, std::sqrt (re * re + im * im) / 2));
    };
    // y a-t-il un point k/|Δ| + o (o = 0 ou 0,5) dans [f0, f1] ?
    auto hits = [&] (double f0, double f1, double o) { return adt > 0 && std::floor (f1 * adt - o) >= std::ceil (f0 * adt - o); };
    const int M = juce::roundToInt ((R - L) * 2);
    juce::Path top, fill, band;
    std::vector<float> yTop ((size_t) M + 1), yBot ((size_t) M + 1);
    for (int i = 0; i <= M; ++i)
    {
        const double f0 = fMin * std::pow (fMax / fMin, std::max (0.0, (i - 0.5) / M));
        const double f1 = fMin * std::pow (fMax / fMin, std::min (1.0, (i + 0.5) / M));
        const double fc = fMin * std::pow (fMax / fMin, (double) i / M);
        double lo = std::min ({ magDb (f0), magDb (f1), magDb (fc) }), hi = std::max ({ magDb (f0), magDb (f1), magDb (fc) });
        if (hits (f0, f1, opposite ? 0.0 : 0.5)) lo = -200;   // un creux tombe dans la colonne
        if (hits (f0, f1, opposite ? 0.5 : 0.0)) hi = 0;      // un sommet tombe dans la colonne
        yTop[(size_t) i] = yAt (hi); yBot[(size_t) i] = yAt (lo);
        const float x = L + (R - L) * (float) i / M;
        if (i == 0) { top.startNewSubPath (x, yTop[0]); fill.startNewSubPath (x, B); fill.lineTo (x, yTop[0]); }
        else { top.lineTo (x, yTop[(size_t) i]); fill.lineTo (x, yTop[(size_t) i]); }
    }
    fill.lineTo (R, B); fill.closeSubPath();
    band.startNewSubPath (L, yTop[0]);
    for (int i = 1; i <= M; ++i) band.lineTo (L + (R - L) * (float) i / M, yTop[(size_t) i]);
    for (int i = M; i >= 0; --i) band.lineTo (L + (R - L) * (float) i / M, yBot[(size_t) i]);
    band.closeSubPath();
    g.setGradientFill (juce::ColourGradient (p.teal.withAlpha (0.25f), 0, T, p.teal.withAlpha (0.02f), 0, B, false));
    g.fillPath (fill);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<int> ((int) L, (int) T - 2, (int) (R - L) + 1, (int) (B - T) + 3));
        g.setColour (p.teal);
        g.fillPath (band);
        g.strokePath (band, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // légendes
    g.setFont (theme.ui (10));
    g.setColour (p.soft);
    g.drawText (U ("Somme mono G + D"), juce::Rectangle<float> (L, 10, 140, 14), juce::Justification::centredLeft, false);
    g.setColour (p.text2);
    drawTabular (g, theme, combText (st), 10, 400, juce::Rectangle<float> (L + 120, 10, R - L - 120, 14), juce::Justification::right);
}
} // namespace audiolab
