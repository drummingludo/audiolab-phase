// Composants d'interface des plugins Audio Lab selon la charte V2 (fiche 02) :
// potentiomètres à arc (gros, moyen, petit), sélecteurs segmentés, bulles d'aide.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace audiolab
{
// Ombre douce façon CSS box-shadow
inline void softShadow (juce::Graphics& g, const juce::Path& p, juce::Colour c, int radius, juce::Point<int> offset)
{
    juce::DropShadow (c, radius, offset).drawForPath (g, p);
}

// Majuscules françaises (toUpperCase ne transforme pas les lettres accentuées sur toutes les plateformes)
inline juce::String upperFr (const juce::String& s)
{
    auto u = s.toUpperCase();
    const char* from[] = { "é", "è", "ê", "ë", "à", "â", "î", "ï", "ô", "ù", "û", "ç" };
    const char* to[]   = { "É", "È", "Ê", "Ë", "À", "Â", "Î", "Ï", "Ô", "Ù", "Û", "Ç" };
    for (int i = 0; i < 12; ++i) u = u.replace (juce::String::fromUTF8 (from[i]), juce::String::fromUTF8 (to[i]));
    return u;
}
// ── Sélecteur segmenté ───────────────────────────────────────────────────
// Bande « Surface » (« Rails » sur une surface), marge 3 px, segments égaux ;
// segment choisi : pastille #005F73, texte #F9F9FB en 500.
class SegButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegButtons (Theme& t, juce::StringArray opts) : theme (t), options (std::move (opts)) {}

    bool vertical = false;      // boutons empilés (Ratio, Meter)
    bool proportional = false;  // segments à la largeur de leur texte (libellés de longueurs très différentes)
    bool onSurface = false;     // posé sur une surface : bande couleur « Rails »
    float fontSize = 12, gap = 2, padX = 12;

    // Relie à un paramètre « choix »
    void attach (juce::RangedAudioParameter& prm)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (prm, [this, &prm] (float) {
            selected = juce::roundToInt (prm.convertFrom0to1 (prm.getValue())); repaint(); }, nullptr);
        onSelect = [this] (int i) { attachment->setValueAsCompleteGesture ((float) i); };
        attachment->sendInitialUpdate();
    }

    void setSelected (int i) { selected = i; repaint(); }
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect;

    float idealWidth() const
    {
        if (proportional)
        {
            float w = 6 + gap * (options.size() - 1);
            for (auto& o : options) w += textWidth (theme.ui (fontSize, 500), o) + 2 * padX;
            return std::ceil (w);
        }
        float w = 0;
        for (auto& o : options) w = std::max (w, textWidth (theme.ui (fontSize, 500), o));
        return 6 + options.size() * (w + 2 * padX) + gap * (options.size() - 1);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        g.setColour (onSurface ? p.rail : p.surface); g.fillRoundedRectangle (getLocalBounds().toFloat(), 9);
        for (int i = 0; i < options.size(); ++i)
        {
            auto r = cell (i);
            const bool sel = i == selected;
            if (sel)
            {
                juce::Path pill; pill.addRoundedRectangle (r, 7);
                softShadow (g, pill, juce::Colours::black.withAlpha (0.18f), 3, { 0, 1 });
                g.setColour (p.tealSel); g.fillPath (pill);
            }
            g.setColour (sel || (i == hover && isEnabled()) ? p.text : p.text2);
            g.setFont (theme.ui (fontSize, sel ? 500 : 400));
            g.drawText (options[i], r, juce::Justification::centred, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h >= 0) { selected = h; repaint(); if (onSelect) onSelect (h); }
    }

private:
    juce::Rectangle<float> cell (int i) const
    {
        const int n = options.size();
        if (vertical)
        {
            const float h = ((float) getHeight() - 6 - gap * (n - 1)) / n;
            return { 3.0f, 3 + i * (h + gap), (float) getWidth() - 6, h };
        }
        if (proportional)
        {
            float total = 0, x = 3;
            for (auto& o : options) total += textWidth (theme.ui (fontSize, 500), o) + 2 * padX;
            const float scale = ((float) getWidth() - 6 - gap * (n - 1)) / std::max (1.0f, total);
            for (int j = 0; j < i; ++j) x += (textWidth (theme.ui (fontSize, 500), options[j]) + 2 * padX) * scale + gap;
            return { x, 3.0f, (textWidth (theme.ui (fontSize, 500), options[i]) + 2 * padX) * scale, (float) getHeight() - 6 };
        }
        const float w = ((float) getWidth() - 6 - gap * (n - 1)) / n;
        return { 3 + i * (w + gap), 3.0f, w, (float) getHeight() - 6 };
    }
    int hit (juce::Point<float> pt) const
    {
        for (int i = 0; i < options.size(); ++i) if (cell (i).contains (pt)) return i;
        return -1;
    }
    Theme& theme;
    juce::StringArray options;
    int selected = 0, hover = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
// ── Bouton (ex. « Align ») : fond « Capuchon », actif = #005F73 ──
class PillButton : public juce::Button
{
public:
    PillButton (Theme& t, juce::String label) : juce::Button (label), theme (t) {}
    bool on = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& p = theme.pal();
        auto b = getLocalBounds().toFloat();
        const bool en = isEnabled();
        g.setColour (on ? p.tealSel : (over || down) && en ? p.railHi : p.cap); g.fillRoundedRectangle (b, 8);
        g.setFont (theme.ui (12, on ? 500 : 400));
        g.setColour (en ? p.text : p.soft);
        g.drawText (getButtonText(), b, juce::Justification::centred, false);
    }
    float idealWidth() const { return textWidth (theme.ui (12, 500), getButtonText()) + 28; }

private:
    Theme& theme;
};

// ── Flèche d'ouverture (« Advanced settings ») : fermé = texte secondaire, ouvert = texte ──
class Disclosure : public juce::Button
{
public:
    Disclosure (Theme& t, juce::String label) : juce::Button (label), theme (t) {}
    bool open = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& p = theme.pal();
        const juce::Colour c = open || over ? p.text : p.text2;
        const float x = 8, y = getHeight() * 0.5f - 6;   // boîte 12 × 12 du chevron
        juce::Path ch;
        if (open) { ch.startNewSubPath (x + 2.5f, y + 4.5f); ch.lineTo (x + 6, y + 8); ch.lineTo (x + 9.5f, y + 4.5f); }
        else      { ch.startNewSubPath (x + 4.5f, y + 2.5f); ch.lineTo (x + 8, y + 6); ch.lineTo (x + 4.5f, y + 9.5f); }
        g.setColour (c);
        g.strokePath (ch, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (theme.ui (12));
        g.drawText (getButtonText(), juce::Rectangle<float> (26, 0, (float) getWidth() - 26, (float) getHeight()), juce::Justification::centredLeft, false);
    }
    float idealWidth() const { return 26 + textWidth (theme.ui (12), getButtonText()) + 8; }

private:
    Theme& theme;
};

// ── Message bref sur deux lignes (résultat du bouton Align) : titre 13 px 600, détail 11 px ──
class Toast : public juce::Component, private juce::Timer
{
public:
    explicit Toast (Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); setAlpha (0); }
    void show (const juce::String& t1, const juce::String& t2, bool ok, int holdMs = 4500)
    {
        title = t1; detail = t2; good = ok; hold = holdMs; shownAt = juce::Time::getMillisecondCounter();
        setAlpha (1); setVisible (true); toFront (false); repaint(); startTimerHz (30);
    }
    const juce::String& shownTitle() const { return title; }
    void paint (juce::Graphics& g) override
    {
        if (title.isEmpty()) return;
        const auto& p = theme.pal();
        const float w = std::min ((float) getWidth() - 32, std::max (textWidth (theme.ui (13, 600), title), textWidth (theme.ui (11), detail)) + 36);
        const float h = detail.isEmpty() ? 34.0f : 50.0f;
        auto r = juce::Rectangle<float> ((getWidth() - w) / 2, (float) getHeight() - h - 14, w, h);
        juce::Path box; box.addRoundedRectangle (r, 10);
        softShadow (g, box, juce::Colours::black.withAlpha (0.4f), 18, { 0, 6 });
        g.setColour (p.surface); g.fillPath (box);
        g.setColour (p.rail); g.strokePath (box, juce::PathStrokeType (1));
        g.setColour (good ? p.teal2 : p.text); g.setFont (theme.ui (13, 600));
        g.drawText (title, detail.isEmpty() ? r : r.withHeight (28).translated (0, 4), juce::Justification::centred, true);
        if (detail.isNotEmpty())
        {
            g.setColour (p.text2); g.setFont (theme.ui (11));
            g.drawText (detail, r.withTrimmedTop (26).withHeight (18).reduced (12, 0), juce::Justification::centred, true);
        }
    }

private:
    void timerCallback() override
    {
        const auto el = (int) (juce::Time::getMillisecondCounter() - shownAt);
        if (hold <= 0) return;   // reste affiché (mesure en cours)
        if (el > hold + 400) { setAlpha (0); stopTimer(); title.clear(); return; }
        setAlpha (el > hold ? 1.0f - (float) (el - hold) / 400.0f : 1.0f);
    }
    Theme& theme;
    juce::String title, detail;
    bool good = false;
    int hold = 4500;
    juce::uint32 shownAt = 0;
};

// ── Bulles d'aide et menus ───────────────────────────────────────────────
class ALLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit ALLookAndFeel (Theme& t) : theme (t)
    {
        const auto& p = theme.pal();
        setColour (juce::PopupMenu::backgroundColourId, p.surface);
        setColour (juce::PopupMenu::textColourId, p.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.tealSel);
        setColour (juce::PopupMenu::highlightedTextColourId, p.text);
        setColour (juce::TooltipWindow::backgroundColourId, p.surface);
        setColour (juce::TooltipWindow::textColourId, p.text);
        setColour (juce::TooltipWindow::outlineColourId, p.rail);
        setColour (juce::ComboBox::textColourId, p.text);
        // saisie d'une valeur (case de valeur cliquée)
        setColour (juce::TextEditor::backgroundColourId, p.cap);
        setColour (juce::TextEditor::textColourId, p.text);
        setColour (juce::TextEditor::highlightColourId, p.tealSel);
        setColour (juce::TextEditor::highlightedTextColourId, p.text);
        setColour (juce::TextEditor::outlineColourId, p.rail);
        setColour (juce::TextEditor::focusedOutlineColourId, p.teal2);
        setColour (juce::CaretComponent::caretColourId, p.text);
    }
    // Menu déroulant (ex. Side-chain) : fond « Capuchon », coins 8, chevron en texte secondaire
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto& p = theme.pal();
        auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (box.isMouseOver (true) ? p.railHi : p.cap); g.fillRoundedRectangle (b, 8);
        if (box.hasKeyboardFocus (true)) { g.setColour (p.teal2.withAlpha (0.5f)); g.drawRoundedRectangle (b.reduced (1), 8, 1.5f); }
        juce::Path chev; const float cx = (float) w - 13, cy = h * 0.5f;
        chev.startNewSubPath (cx - 3, cy - 1.5f); chev.lineTo (cx, cy + 1.5f); chev.lineTo (cx + 3, cy - 1.5f);
        g.setColour (p.text2); g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme.ui (12); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBorderSize ({ 0, 0, 0, 0 });
        label.setBounds (10, 0, box.getWidth() - 26, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return theme.ui (13); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto& p = theme.pal();
        g.fillAll (p.surface);
        g.setColour (p.rail); g.drawRect (0, 0, w, h);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
    {
        const auto& p = theme.pal();
        g.setColour (p.surface); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
        g.setColour (p.rail); g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6, 1);
        g.setColour (p.text); g.setFont (theme.ui (12));
        g.drawFittedText (tip, juce::Rectangle<int> (0, 0, w, h).reduced (9, 5), juce::Justification::centredLeft, 5);
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::GlyphArrangement ga; ga.addFittedText (theme.ui (12), tip, 0, 0, 260, 200, juce::Justification::topLeft, 5);
        const int w = juce::jmin (280, (int) ga.getBoundingBox (0, -1, true).getWidth() + 20);
        const int h = (int) ga.getBoundingBox (0, -1, true).getHeight() + 12;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y + 14, w, h).constrainedWithin (parent);
    }

private:
    Theme& theme;
};
} // namespace audiolab
