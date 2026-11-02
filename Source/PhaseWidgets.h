// Commandes propres à Audio Lab Phase (charte UI V2) : bouton de polarité Ø, Link G/D,
// curseur de décalage bipolaire (départ au centre) et case de valeur cliquable (saisie au clavier).
#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"

namespace audiolab
{
// Accès aux réglages d'un canal : avec Link, D lit et écrit les réglages de G (le processeur recopie G dans D)
struct ChannelAccess
{
    AudioLabPhaseProcessor& proc;
    int chan;

    bool linked() const { return proc.getParam (params::link) > 0.5f; }
    juce::RangedAudioParameter& delayParam() const { return *proc.apvts.getParameter (chan == 1 && ! linked() ? params::delR : params::delL); }
    juce::RangedAudioParameter& polParam() const { return *proc.apvts.getParameter (chan == 1 && ! linked() ? params::polR : params::polL); }
    double ms() const { auto& p = delayParam(); return p.convertFrom0to1 (p.getValue()); }
    bool inverted() const { return polParam().getValue() > 0.5f; }
    bool samplesUnit() const { return proc.getParam (params::unit) < 0.5f; }
    double fs() const { return proc.displayRate(); }
    // décalage réellement appliqué par le moteur
    double effSamples() const { return PhaseCore::effectiveSamples (ms(), samplesUnit(), fs()); }
    double effMs() const { return effSamples() * 1000.0 / fs(); }

    // Arrondi à la grille de l'unité : échantillon entier, 0,01 ms ou 1 µs (fin)
    double snap (double v, bool fine) const
    {
        v = juce::jlimit (-(double) params::maxMs, (double) params::maxMs, v);
        if (samplesUnit())
        {
            const double lim = std::floor (params::maxMs * fs() / 1000.0 + 1e-9);
            return juce::jlimit (-lim, lim, std::round (v * fs() / 1000.0)) * 1000.0 / fs();
        }
        const double g = fine ? 1000.0 : 100.0;
        return std::round (v * g) / g;
    }
    // pas des flèches et de la molette
    double step (bool fine) const { return samplesUnit() ? 1000.0 / fs() : fine ? 0.001 : 0.01; }

    void setMs (double v) { auto& p = delayParam(); p.setValueNotifyingHost (p.convertTo0to1 ((float) v)); }
    void setMsGesture (double v) { auto& p = delayParam(); p.beginChangeGesture(); setMs (v); p.endChangeGesture(); }
};

// ── Bouton de polarité Ø (36 × 36) : « Capuchon », inversé = #005F73 ──
class PolButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    PolButton (Theme& t, AudioLabPhaseProcessor& p, int c) : theme (t), acc { p, c }
    {
        setTitle (c == 0 ? "Polarity L" : "Polarity R");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const bool on = acc.inverted(), en = isEnabled();
        auto b = getLocalBounds().toFloat();
        juce::Path box; box.addRoundedRectangle (b, 8);
        if (en)
        {
            softShadow (g, box, juce::Colours::black.withAlpha (0.45f), 6, { 0, 2 });
            softShadow (g, box, juce::Colours::black.withAlpha (0.45f), 2, { 0, 1 });
        }
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (box);
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillPath (box);
            g.setColour (on && en ? p.tealSel : hover && en ? p.railHi : p.cap); g.fillRect (b.withTrimmedTop (1));
        }
        if (focused) { g.setColour (p.teal2.withAlpha (0.6f)); g.drawRoundedRectangle (b.expanded (2.5f), 10, 1.5f); }
        g.setColour (! en ? p.soft : on ? p.text : p.text2);
        g.setFont (theme.ui (18, on ? 600 : 500));
        g.drawText (juce::String::fromUTF8 ("Ø"), b.translated (0, 0.5f), juce::Justification::centred, false);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { toggle(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.isKeyCode (juce::KeyPress::spaceKey) || k.isKeyCode (juce::KeyPress::returnKey)) { toggle(); return true; }
        return false;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }
    std::function<void()> onChange;

private:
    void toggle()
    {
        if (! isEnabled()) return;
        auto& prm = acc.polParam();
        prm.beginChangeGesture(); prm.setValueNotifyingHost (acc.inverted() ? 0.0f : 1.0f); prm.endChangeGesture();
        repaint(); if (onChange) onChange();
    }
    Theme& theme;
    ChannelAccess acc;
    bool hover = false, focused = false;
};

// ── Link G/D : pilule verticale entre les deux lignes ; liée = #005F73 et chaîne fermée, déliée = chaîne ouverte ──
class LinkButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    LinkButton (Theme& t, AudioLabPhaseProcessor& p) : theme (t), proc (p)
    {
        setTitle ("Link L/R");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const bool on = proc.getParam (params::link) > 0.5f, en = isEnabled();
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const float r = b.getWidth() / 2;
        if (on && en)
        {
            juce::Path pill; pill.addRoundedRectangle (b, r);
            softShadow (g, pill, juce::Colours::black.withAlpha (0.18f), 3, { 0, 1 });
            g.setColour (p.tealSel); g.fillPath (pill);
        }
        else
        {
            g.setColour (hover && en ? p.railHi : p.rail);
            g.drawRoundedRectangle (b, r, 1.2f);
        }
        if (focused) { g.setColour (p.teal2.withAlpha (0.6f)); g.drawRoundedRectangle (b.expanded (2.5f), r + 2.5f, 1.5f); }
        // chaîne : deux maillons inclinés à 45° ; fermée (maillons emboîtés) quand G et D sont liés, ouverte sinon
        const float cx = b.getCentreX(), cy = b.getCentreY(), w = 6, h = 11, off = on ? 3.6f : 6.4f;
        juce::Path chain;
        chain.addRoundedRectangle (cx - w / 2, cy - off - h / 2, w, h, w / 2);
        chain.addRoundedRectangle (cx - w / 2, cy + off - h / 2, w, h, w / 2);
        if (on) { chain.startNewSubPath (cx, cy - 2.6f); chain.lineTo (cx, cy + 2.6f); }
        else
        {
            // deux petits traits de rupture de part et d'autre de la coupure
            chain.startNewSubPath (cx - 6.5f, cy - 0.5f); chain.lineTo (cx - 4.5f, cy - 0.5f);
            chain.startNewSubPath (cx + 4.5f, cy + 0.5f); chain.lineTo (cx + 6.5f, cy + 0.5f);
        }
        chain.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4, cx, cy));
        g.setColour (! en ? p.soft : on ? p.text : p.text2);
        g.strokePath (chain, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { toggle(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.isKeyCode (juce::KeyPress::spaceKey) || k.isKeyCode (juce::KeyPress::returnKey)) { toggle(); return true; }
        return false;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }
    std::function<void()> onChange;

private:
    void toggle()
    {
        if (! isEnabled()) return;
        auto* prm = proc.apvts.getParameter (params::link);
        const bool on = prm->getValue() > 0.5f;
        prm->beginChangeGesture(); prm->setValueNotifyingHost (on ? 0.0f : 1.0f); prm->endChangeGesture();
        repaint(); if (onChange) onChange();
    }
    Theme& theme;
    AudioLabPhaseProcessor& proc;
    bool hover = false, focused = false;
};

// ── Curseur de décalage bipolaire : piste 4 px « Rails », départ au centre (0), partie colorée du centre
//    au bouton en dégradé orange #BB3E03 → #EAA93D (posé sur le fond), bouton 18 px « Capuchon » ──
class DelaySlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    DelaySlider (Theme& t, AudioLabPhaseProcessor& p, int c) : theme (t), acc { p, c }
    {
        setTitle (c == 0 ? "Offset L" : "Offset R");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }
    std::function<void()> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const bool en = isEnabled();
        const float cy = getHeight() * 0.5f, ta = 9, tb = (float) getWidth() - 9, xc = (ta + tb) / 2;
        const double lim = params::maxMs * acc.fs() / 1000.0;
        const float pos = (float) juce::jlimit (-1.0, 1.0, acc.effSamples() / lim);
        const float x = xc + pos * (tb - ta) / 2;
        // graduations : 0 (plus haute), ±5 ms, ±10 ms
        g.setColour (p.mark2);
        for (int k = -2; k <= 2; ++k)
        {
            const float tx = xc + k * (tb - ta) / 4;
            const float th = k == 0 ? 7.0f : 4.0f;
            g.fillRect (tx - 0.5f, cy + 5, 1.0f, th);
        }
        g.setColour (hover && en ? p.railHi : p.rail); g.fillRoundedRectangle (0, cy - 2, (float) getWidth(), 4, 2);
        if (std::abs (x - xc) > 0.3f)
        {
            const juce::Rectangle<float> f (std::min (x, xc), cy - 2, std::abs (x - xc), 4);
            if (en)
            {
                const float boost = dragging ? 1.6f : 1.0f;
                g.setColour (p.orange.withAlpha (0.12f * boost)); g.fillRoundedRectangle (f.expanded (2.5f), 4);
                g.setColour (p.orange.withAlpha (0.22f * boost)); g.fillRoundedRectangle (f.expanded (1.2f), 3);
                g.setGradientFill (juce::ColourGradient (p.orange0, xc, cy, p.orange, x, cy, false));
            }
            else g.setColour (p.soft);
            g.fillRoundedRectangle (f, 2);
        }
        // repère du centre par-dessus la piste
        g.setColour (p.mark2); g.fillRect (xc - 0.5f, cy - 5, 1.0f, 3.0f);
        juce::Path th; th.addEllipse (x - 9, cy - 9, 18, 18);
        if (en) softShadow (g, th, juce::Colours::black.withAlpha (0.5f), 6, { 0, 2 });
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (th);
            g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillPath (th);
            g.setColour (p.cap); g.fillEllipse (x - 9, cy - 8, 18, 18);
        }
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.strokePath (th, juce::PathStrokeType (0.5f));
        if (focused) { g.setColour (p.teal2.withAlpha (0.5f)); g.drawEllipse (x - 10.5f, cy - 10.5f, 21, 21, 1.5f); }
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        acc.delayParam().beginChangeGesture(); dragging = true;
        const float cy = getHeight() * 0.5f;
        const float knobX = xFor (acc.effMs());
        // clic sur la piste : le bouton saute à cet endroit ; clic sur le bouton : on le déplace sans saut
        if (std::abs (e.position.x - knobX) > 10 || std::abs (e.position.y - cy) > 12) set (msAt (e.position.x), false);
        x0 = e.position.x; ms0 = acc.ms(); repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const bool fine = e.mods.isShiftDown();
        if (fine != lastFine) { x0 = e.position.x; ms0 = acc.ms(); lastFine = fine; }   // pas de saut au passage en mode fin
        const double perPx = 2.0 * params::maxMs / ((double) getWidth() - 18) * (fine ? 0.1 : 1.0);
        set (ms0 + (e.position.x - x0) * perPx, fine);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) { acc.delayParam().endChangeGesture(); dragging = false; lastFine = false; repaint(); }
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (isEnabled()) { acc.setMsGesture (0); changed(); } }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;   // Maj + molette = défilement horizontal sur Mac
        if (std::abs (d) < 1e-6f || ! isEnabled()) return;
        const bool fine = e.mods.isShiftDown();
        acc.setMsGesture (acc.snap (acc.ms() + (d > 0 ? 1 : -1) * acc.step (fine), fine)); changed();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const bool fine = k.getModifiers().isShiftDown();
        int dir = 0;
        if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::upKey)) dir = 1;
        else if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::downKey)) dir = -1;
        else if (k.isKeyCode (juce::KeyPress::homeKey)) { acc.setMsGesture (0); changed(); return true; }
        if (dir == 0) return false;
        acc.setMsGesture (acc.snap (acc.ms() + dir * acc.step (fine), fine)); changed();
        return true;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        struct Value : juce::AccessibilityRangedNumericValueInterface
        {
            explicit Value (DelaySlider& s) : sl (s) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return sl.acc.ms(); }
            void setValue (double v) override { sl.acc.setMsGesture (sl.acc.snap (v, true)); sl.changed(); }
            AccessibleValueRange getRange() const override { return { { -params::maxMs, params::maxMs }, sl.acc.step (false) }; }
            DelaySlider& sl;
        };
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
                                                              juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
    }

private:
    float xFor (double ms) const
    {
        const float a = 9, b = (float) getWidth() - 9;
        return (a + b) / 2 + (float) (ms / params::maxMs) * (b - a) / 2;
    }
    double msAt (float x) const
    {
        const float a = 9, b = (float) getWidth() - 9;
        return (double) (x - (a + b) / 2) / ((b - a) / 2) * params::maxMs;
    }
    void set (double v, bool fine) { acc.setMs (acc.snap (v, fine)); changed(); }
    void changed() { repaint(); if (onChange) onChange(); }

    Theme& theme;
    ChannelAccess acc;
    float x0 = 0;
    double ms0 = 0;
    bool dragging = false, focused = false, hover = false, lastFine = false;
};

// ── Case de valeur : valeur dans l'unité choisie (14 px), puis l'autre unité et la distance (11 px, gris doux) ;
//    un clic ouvre la saisie au clavier (« 1,5 ms », « 72 éch. », « 34 cm »…) ──
class ValueBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueBox (Theme& t, AudioLabPhaseProcessor& p, int c) : theme (t), acc { p, c }
    {
        setMouseCursor (juce::MouseCursor::IBeamCursor);
        addChildComponent (editor);
        editor.setJustification (juce::Justification::centredRight);
        editor.setIndents (6, 0);
        editor.setFont (theme.ui (14, 600));
        editor.setSelectAllWhenFocused (true);
        editor.onReturnKey = [this] { commit(); };
        editor.onEscapeKey = [this] { editor.setVisible (false); };
        editor.onFocusLost = [this] { if (editor.isVisible()) commit(); };
    }
    std::function<void()> onChange;

    void paint (juce::Graphics& g) override
    {
        if (editor.isVisible()) return;
        const auto& p = theme.pal();
        const bool en = isEnabled();
        const double s = acc.effSamples(), ms = acc.effMs();
        const auto main = acc.samplesUnit() ? params::fmtSamples (s) : params::fmtMs (ms);
        const auto other = acc.samplesUnit() ? params::fmtMs (ms) : params::fmtSamples (s);
        const float w = (float) getWidth();
        g.setColour (en ? (hover ? p.text : p.text) : p.soft);
        drawTabular (g, theme, main, 14, 600, { 0, 1, w, 18 }, juce::Justification::right, -0.01f);
        g.setColour (en ? p.soft : p.soft.withAlpha (0.6f));
        drawTabular (g, theme, other + juce::String::fromUTF8 (" · ") + params::fmtDist (ms), 11, 400, { 0, 20, w, 14 }, juce::Justification::right);
        if (hover && en) { g.setColour (p.rail); g.fillRect (w - tabularWidth (theme, main, 14, 600, -0.01f), 19.0f, tabularWidth (theme, main, 14, 600, -0.01f), 1.0f); }
    }
    void resized() override { editor.setBounds (getLocalBounds().withHeight (22).withTrimmedLeft (8)); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override { startEditing(); }
    void startEditing()
    {
        if (! isEnabled()) return;
        // valeur actuelle, sans unité, dans l'unité choisie
        const auto txt = acc.samplesUnit() ? juce::String (juce::roundToInt (acc.effSamples()))
                                           : juce::String (std::round (acc.effMs() * 1000) / 1000, 3);
        editor.setText (txt, false);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
        editor.selectAll();
        repaint();
    }

private:
    void commit()
    {
        double v = 0;
        if (params::parseValue (editor.getText(), acc.samplesUnit(), acc.fs(), v))
        {
            acc.setMsGesture (acc.snap (v, true));
            if (onChange) onChange();
        }
        editor.setVisible (false);
        repaint();
    }
    Theme& theme;
    ChannelAccess acc;
    juce::TextEditor editor;
    bool hover = false;
};
} // namespace audiolab
