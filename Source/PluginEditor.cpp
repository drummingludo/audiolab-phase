#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

static const char* helpPol   = "Polarity (often called \"phase invert\"): flips the signal, peaks become troughs. "
                               "Snare top / bottom, kick in / out: choose the setting where the low end is fullest.";
static const char* helpSl    = "Offset: to the right the sound arrives later, to the left earlier (up to 10 ms, ≈ 3.4 m). "
                               "1 ms ≈ 34 cm: realign two mics placed at different distances. "
                               "Shift = fine adjustment, arrows = step by step, double-click = 0.";
static const char* helpVal   = "Click to type a value: \"48\" (current unit), \"1.5 ms\", \"72 smp\", \"250 µs\" or a distance \"34 cm\".";
static const char* helpLink  = "Link: L and R set together (same offset, same polarity). Turn it off to set each channel separately.";
static const char* helpUnit  = "Offset unit. Samples: one-sample steps, the sound is copied exactly. "
                               "ms: steps of 0.01 ms (1 µs with Shift), even between two samples (sinc interpolation, flat up to 20 kHz).";
static const char* helpView  = "Time: position of L and R compared with the original sound. Mono sum: what you hear when L and R are added together.";
static const char* helpMono  = "Mono track: a single channel (L).";

AudioLabPhaseEditor::AudioLabPhaseEditor (AudioLabPhaseProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (theme, p),
      pol0 (theme, p, 0), pol1 (theme, p, 1), linkBtn (theme, p),
      sl0 (theme, p, 0), sl1 (theme, p, 1), val0 (theme, p, 0), val1 (theme, p, 1)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    viewSeg.fontSize = 11; unitSeg.fontSize = 11;
    viewSeg.setTooltip (U (helpView));
    unitSeg.setTooltip (U (helpUnit));
    unitSeg.attach (*proc.apvts.getParameter (params::unit));
    unitSeg.onSelect = [this, f = unitSeg.onSelect] (int i) { f (i); refreshAll(); };
    const int v = juce::jlimit (0, 1, (int) proc.ui.getProperty ("view", 0));
    viewSeg.setSelected (v); display.setView (v);
    viewSeg.onSelect = [this] (int i) { display.setView (i); proc.ui.setProperty ("view", i, nullptr); };

    for (auto* b : { &pol0, &pol1 }) b->setTooltip (U (helpPol));
    for (auto* s : { &sl0, &sl1 }) s->setTooltip (U (helpSl));
    for (auto* b : { &val0, &val1 }) b->setTooltip (U (helpVal));
    linkBtn.setTooltip (U (helpLink));
    auto refresh = [this] { refreshAll(); };
    pol0.onChange = pol1.onChange = linkBtn.onChange = refresh;
    sl0.onChange = sl1.onChange = val0.onChange = val1.onChange = refresh;

    for (auto* c : std::initializer_list<juce::Component*> { &display, &viewSeg, &unitSeg, &pol0, &pol1, &linkBtn, &sl0, &sl1, &val0, &val1 })
        content.addAndMakeVisible (c);

    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (baseH * sc));
    refreshAll();
    startTimerHz (20);
}

AudioLabPhaseEditor::~AudioLabPhaseEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

// Redessine tout quand un réglage change (souris, automation du logiciel, preset, Link, fréquence d'échantillonnage)
void AudioLabPhaseEditor::timerCallback()
{
    String s;
    for (auto* id : { params::delL, params::delR, params::polL, params::polR, params::link, params::unit, params::bypass })
        s << String (proc.getParam (id), 6) << ";";
    s << proc.displayRate() << ";" << (int) proc.isMono();
    if (s != lastState) refreshAll();
}

void AudioLabPhaseEditor::refreshAll()
{
    String s;
    for (auto* id : { params::delL, params::delR, params::polL, params::polR, params::link, params::unit, params::bypass })
        s << String (proc.getParam (id), 6) << ";";
    s << proc.displayRate() << ";" << (int) proc.isMono();
    lastState = s;

    const bool mono = proc.isMono();
    for (auto* c : std::initializer_list<juce::Component*> { &pol1, &sl1, &val1, &linkBtn })
        c->setEnabled (! mono);
    viewSeg.setVisible (! mono);   // piste mono : seulement la vue Temps
    if (mono) { for (auto* c : std::initializer_list<juce::SettableTooltipClient*> { &pol1, &sl1, &val1, &linkBtn }) c->setTooltip (U (helpMono)); }
    else { pol1.setTooltip (U (helpPol)); sl1.setTooltip (U (helpSl)); val1.setTooltip (U (helpVal)); linkBtn.setTooltip (U (helpLink)); }
    unitSeg.setSelected (proc.getParam (params::unit) > 0.5f ? 1 : 0);
    display.updateTooltip();
    content.repaint();
    for (auto* c : content.getChildren()) c->repaint();
}

void AudioLabPhaseEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, baseH);
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

// Lignes des canaux : noms des commandes à y 258, G à y 276, D à y 324 (hauteur 36)
namespace rows { constexpr int head = 258, y0 = 276, y1 = 324, h = 36; }

void AudioLabPhaseEditor::Content::paint (juce::Graphics& g)
{
    const auto& p = ed.theme.pal();
    g.fillAll (p.bg);
    // noms des canaux
    g.setFont (ed.theme.ui (12, 600));
    g.setColour (p.text);
    g.drawText ("L", juce::Rectangle<float> (24, (float) rows::y0, 14, (float) rows::h), juce::Justification::centred, false);
    g.setColour (ed.proc.isMono() ? p.soft : p.text);
    g.drawText ("R", juce::Rectangle<float> (24, (float) rows::y1, 14, (float) rows::h), juce::Justification::centred, false);
    // noms des commandes, au-dessus des lignes
    g.setColour (p.text); g.setFont (ed.theme.ui (9, 600, 0.04f));
    g.drawText (U ("POLARITY"), juce::Rectangle<float> (30, (float) rows::head, 64, 12), juce::Justification::centred, false);
    g.drawText ("LINK", juce::Rectangle<float> (82, (float) rows::head, 40, 12), juce::Justification::centred, false);
    g.drawText (U ("OFFSET"), juce::Rectangle<float> (133, (float) rows::head, 120, 12), juce::Justification::centredLeft, false);
    g.drawText (U ("UNIT"), juce::Rectangle<float> ((float) ed.unitSeg.getX() - 50, 216, 42, 28), juce::Justification::centredRight, false);
    // repères du curseur : −10 / 0 / +10 ms, sous la piste de D
    g.setColour (p.soft); g.setFont (ed.theme.ui (8));
    const float a = (float) ed.sl1.getX() + 9, b = (float) ed.sl1.getRight() - 9, y = (float) rows::y1 + rows::h + 1;
    g.drawText (params::sign (-1) + "10 ms", juce::Rectangle<float> (a - 20, y, 40, 11), juce::Justification::centred, false);
    g.drawText ("0", juce::Rectangle<float> ((a + b) / 2 - 20, y, 40, 11), juce::Justification::centred, false);
    g.drawText ("+10 ms", juce::Rectangle<float> (b - 20, y, 40, 11), juce::Justification::centred, false);
}

void AudioLabPhaseEditor::Content::resized()
{
    auto& e = ed;
    e.display.setBounds (24, 24, 532, 176);
    // rangée sous l'écran : vue (à gauche), unité (à droite)
    e.viewSeg.setBounds (24, 216, juce::roundToInt (e.viewSeg.idealWidth()), 28);
    const int uw = juce::roundToInt (e.unitSeg.idealWidth());
    e.unitSeg.setBounds (556 - uw, 216, uw, 28);
    // lignes G / D : polarité, Link (entre les deux), curseur, valeur
    e.pol0.setBounds (44, rows::y0, 36, 36);
    e.pol1.setBounds (44, rows::y1, 36, 36);
    e.linkBtn.setBounds (92, rows::y0 + 6, 20, rows::y1 + rows::h - 6 - (rows::y0 + 6));
    e.sl0.setBounds (124, rows::y0, 306, rows::h);
    e.sl1.setBounds (124, rows::y1, 306, rows::h);
    e.val0.setBounds (440, rows::y0 + 1, 116, 34);
    e.val1.setBounds (440, rows::y1 + 1, 116, 34);
}
