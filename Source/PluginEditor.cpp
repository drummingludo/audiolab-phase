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
static const char* helpLink  = "Link: L and R set together (same offset, same polarity, same rotation). Turn it off to set each channel separately.";
static const char* helpUnit  = "Offset unit. Samples: one-sample steps, the sound is copied exactly. "
                               "ms: steps of 0.01 ms (1 µs with Shift), even between two samples (sinc interpolation, flat up to 20 kHz).";
static const char* helpView  = "Time: position of L and R compared with the original sound. Mono sum: what you hear when L and R are added together.";
static const char* helpMono  = "Mono track: a single channel (L).";
// 2.0.0
static const char* helpRot   = "Phase rotation: shifts the phase of every frequency by the same angle, without delaying the sound "
                               "(Offset shifts time instead). 180\xc2\xb0 = polarity inverted. Useful when a pure delay cannot match "
                               "two mics (kick in / out, DI / amp). Shift = fine adjustment, double-click = 0\xc2\xb0.";
static const char* helpRotV  = "Click to type an angle: \"90\", \"-90\" (= 270\xc2\xb0), \"12.5\".";
static const char* helpRotZ  = "Phase rotation needs the Standard latency mode (Advanced settings).";
static const char* helpAlign = "Align on the side-chain: send the reference track (the other mic) to the plugin's side-chain input, "
                               "play a passage where both sound, then click. Lab Phase measures the offset, the polarity and the phase "
                               "rotation on the last 1.4 s and sets them on this track.";
static const char* helpLat   = "Standard: offsets from \xe2\x88\x92" "10 to +10 ms and phase rotation; the plugin reports a fixed latency that "
                               "your software compensates. Zero: no latency (recording, live), delays from 0 to +10 ms in whole samples, "
                               "no rotation.";

AudioLabPhaseEditor::AudioLabPhaseEditor (AudioLabPhaseProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (theme, p),
      pol0 (theme, p, 0), pol1 (theme, p, 1), linkBtn (theme, p), linkBtn2 (theme, p),
      sl0 (theme, p, 0), sl1 (theme, p, 1), val0 (theme, p, 0), val1 (theme, p, 1),
      rs0 (theme, p, 0), rs1 (theme, p, 1), rv0 (theme, p, 0), rv1 (theme, p, 1)
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
    linkBtn.setTooltip (U (helpLink)); linkBtn2.setTooltip (U (helpLink));
    auto refresh = [this] { refreshAll(); };
    pol0.onChange = pol1.onChange = linkBtn.onChange = linkBtn2.onChange = refresh;
    sl0.onChange = sl1.onChange = val0.onChange = val1.onChange = refresh;
    rs0.onChange = rs1.onChange = rv0.onChange = rv1.onChange = refresh;

    // 2.0.0 : Align, panneau avancé (mode de latence)
    alignBtn.setTooltip (U (helpAlign));
    alignBtn.onClick = [this] { proc.requestAlign(); timerCallback(); };
    advBtn.setTooltip (U ("Show / hide the latency mode"));
    advBtn.onClick = [this] { setAdvanced (! advOpen, true); };
    latSeg.onSurface = true;
    latSeg.attach (*proc.apvts.getParameter (params::latMode));
    latSeg.onSelect = [this, f = latSeg.onSelect] (int i) { f (i); refreshAll(); };
    latSeg.setTooltip (U (helpLat));

    for (auto* c : std::initializer_list<juce::Component*> { &display, &viewSeg, &unitSeg, &pol0, &pol1, &linkBtn, &linkBtn2, &sl0, &sl1, &val0, &val1,
                                                             &rs0, &rs1, &rv0, &rv1, &advBtn, &alignBtn, &latSeg })
        content.addAndMakeVisible (c);
    content.addChildComponent (toast);

    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));
    setAdvanced ((bool) proc.ui.getProperty ("adv", false), false);   // panneau avancé caché par défaut
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
    setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (currentH() * sc));
    lastAlignSerial = proc.alignStatus().serial;
    refreshAll();
    startTimerHz (20);
}

AudioLabPhaseEditor::~AudioLabPhaseEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

static String stateKey (AudioLabPhaseProcessor& proc)
{
    String s;
    for (auto* id : { params::delL, params::delR, params::polL, params::polR, params::link, params::unit, params::bypass,
                      params::rotL, params::rotR, params::latMode })
        s << String (proc.getParam (id), 6) << ";";
    s << proc.displayRate() << ";" << (int) proc.isMono();
    return s;
}

// Redessine tout quand un réglage change (souris, automation du logiciel, preset, Link, fréquence d'échantillonnage) ;
// affiche le résultat du bouton Align
void AudioLabPhaseEditor::timerCallback()
{
    if (stateKey (proc) != lastState) refreshAll();
    const auto st = proc.alignStatus();
    if (st.serial != lastAlignSerial)
    {
        lastAlignSerial = st.serial;
        using S = AudioLabPhaseProcessor::AlignStatus::State;
        toast.show (st.title, st.detail, st.state == S::done, st.state == S::listening ? 0 : 4500);
        alignBtn.on = st.state == S::listening; alignBtn.repaint();
    }
}

void AudioLabPhaseEditor::refreshAll()
{
    lastState = stateKey (proc);

    const bool mono = proc.isMono(), zero = proc.zeroLatency();
    for (auto* c : std::initializer_list<juce::Component*> { &pol1, &sl1, &val1, &linkBtn })
        c->setEnabled (! mono);
    linkBtn2.setEnabled (! mono && ! zero);
    rs0.setEnabled (! zero); rv0.setEnabled (! zero);
    rs1.setEnabled (! zero && ! mono); rv1.setEnabled (! zero && ! mono);
    viewSeg.setVisible (! mono);   // piste mono : seulement la vue Temps
    if (mono) { for (auto* c : std::initializer_list<juce::SettableTooltipClient*> { &pol1, &sl1, &val1, &linkBtn, &linkBtn2, &rs1, &rv1 }) c->setTooltip (U (helpMono)); }
    else { pol1.setTooltip (U (helpPol)); sl1.setTooltip (U (helpSl)); val1.setTooltip (U (helpVal)); linkBtn.setTooltip (U (helpLink)); linkBtn2.setTooltip (U (helpLink)); }
    for (auto* r : std::initializer_list<juce::SettableTooltipClient*> { &rs0, &rs1 }) if (! mono || r == &rs0) r->setTooltip (U (zero ? helpRotZ : helpRot));
    for (auto* r : std::initializer_list<juce::SettableTooltipClient*> { &rv0, &rv1 }) if (! mono || r == &rv0) r->setTooltip (U (zero ? helpRotZ : helpRotV));
    unitSeg.setSelected (proc.getParam (params::unit) > 0.5f ? 1 : 0);
    latSeg.setSelected (zero ? 1 : 0);
    display.updateTooltip();
    content.repaint();
    for (auto* c : content.getChildren()) c->repaint();
}

// Ouvre / ferme le panneau « Advanced settings » : la fenêtre grandit ou rétrécit de 74 px (à l'échelle)
void AudioLabPhaseEditor::setAdvanced (bool open, bool resizeWindow)
{
    advOpen = open;
    proc.ui.setProperty ("adv", open, nullptr);
    advBtn.open = open; advBtn.repaint();
    latSeg.setVisible (open);
    if (resizeWindow)
    {
        const double s = (double) getWidth() / baseW;
        getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
        setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
        setSize (juce::roundToInt (baseW * s), juce::roundToInt (currentH() * s));
    }
    resized();
}

String AudioLabPhaseEditor::latencyText() const
{
    const int L = proc.latencySamples();
    if (L == 0) return "Latency: 0 (Zero mode)";
    return "Latency: " + params::num (L * 1000.0 / proc.displayRate(), 1) + " ms (" + String (L) + " smp)";
}

void AudioLabPhaseEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, currentH());
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

// Lignes des canaux : noms des commandes à y 258, G à y 276, D à y 324 (hauteur 36) ;
// 2.0.0 : rotation, nom à y 396, G à y 414, D à y 462 ; rangée du bas à y 534 ; panneau à y 578
namespace rows { constexpr int head = 258, y0 = 276, y1 = 324, h = 36, rhead = 396, r0 = 414, r1 = 462, bottom = 534, panel = 578; }

void AudioLabPhaseEditor::Content::paint (juce::Graphics& g)
{
    const auto& p = ed.theme.pal();
    g.fillAll (p.bg);
    // noms des canaux
    g.setFont (ed.theme.ui (12, 600));
    for (int y : { rows::y0, rows::r0 })
    {
        g.setColour (y == rows::r0 && ed.proc.zeroLatency() ? p.soft : p.text);
        g.drawText ("L", juce::Rectangle<float> (24, (float) y, 14, (float) rows::h), juce::Justification::centred, false);
    }
    for (int y : { rows::y1, rows::r1 })
    {
        g.setColour (ed.proc.isMono() || (y == rows::r1 && ed.proc.zeroLatency()) ? p.soft : p.text);
        g.drawText ("R", juce::Rectangle<float> (24, (float) y, 14, (float) rows::h), juce::Justification::centred, false);
    }
    // noms des commandes, au-dessus des lignes
    g.setColour (p.text); g.setFont (ed.theme.ui (9, 600, 0.04f));
    g.drawText (U ("POLARITY"), juce::Rectangle<float> (30, (float) rows::head, 64, 12), juce::Justification::centred, false);
    g.drawText ("LINK", juce::Rectangle<float> (82, (float) rows::head, 40, 12), juce::Justification::centred, false);
    g.drawText (U ("OFFSET"), juce::Rectangle<float> (133, (float) rows::head, 120, 12), juce::Justification::centredLeft, false);
    g.setColour (ed.proc.zeroLatency() ? p.soft : p.text);
    g.drawText (U ("ROTATION"), juce::Rectangle<float> (133, (float) rows::rhead, 120, 12), juce::Justification::centredLeft, false);
    g.setColour (p.text);
    g.drawText (U ("UNIT"), juce::Rectangle<float> ((float) ed.unitSeg.getX() - 50, 216, 42, 28), juce::Justification::centredRight, false);
    // repères des curseurs : −10 / 0 / +10 ms sous la piste de D ; 0 / 90 / 180 / 270 / 360° sous la rotation de D
    g.setColour (p.soft); g.setFont (ed.theme.ui (8));
    {
        const float a = (float) ed.sl1.getX() + 9, b = (float) ed.sl1.getRight() - 9, y = (float) rows::y1 + rows::h + 1;
        g.setColour (ed.proc.zeroLatency() ? p.soft.withAlpha (0.5f) : p.soft);   // mode Zero : pas d'avance
        g.drawText (params::sign (-1) + "10 ms", juce::Rectangle<float> (a - 20, y, 40, 11), juce::Justification::centred, false);
        g.setColour (p.soft);
        g.drawText ("0", juce::Rectangle<float> ((a + b) / 2 - 20, y, 40, 11), juce::Justification::centred, false);
        g.drawText ("+10 ms", juce::Rectangle<float> (b - 20, y, 40, 11), juce::Justification::centred, false);
    }
    {
        const float a = (float) ed.rs1.getX() + 9, b = (float) ed.rs1.getRight() - 9, y = (float) rows::r1 + rows::h + 1;
        for (int k = 0; k <= 4; ++k)
            g.drawText (String (k * 90) + U ("\xc2\xb0"), juce::Rectangle<float> (a + k * (b - a) / 4 - 20, y, 40, 11), juce::Justification::centred, false);
    }
    // rangée du bas : latence annoncée, à droite de « Advanced settings »
    g.setFont (ed.theme.ui (11)); g.setColour (p.soft);
    const float lx = (float) ed.advBtn.getRight() + 12;
    g.drawText (ed.latencyText(), juce::Rectangle<float> (lx, (float) rows::bottom, (float) ed.alignBtn.getX() - 12 - lx, 28), juce::Justification::centredLeft, true);
    if (! ed.advOpen) return;
    // panneau avancé (« Surface », coins 12, marge intérieure 16) : Latency [Standard | Zero] + explication
    g.setColour (p.surface); g.fillRoundedRectangle (24, (float) rows::panel, 532, 58, 12);
    g.setFont (ed.theme.ui (12)); g.setColour (p.text2);
    g.drawText ("Latency", juce::Rectangle<float> (40, (float) rows::panel + 16, 68, 26), juce::Justification::centredLeft, false);
    g.setFont (ed.theme.ui (11)); g.setColour (p.soft);
    const float ix = (float) ed.latSeg.getRight() + 14;
    g.drawText (ed.proc.zeroLatency() ? U ("Delays only, whole samples, no rotation")
                                      : U ("Advance and phase rotation"),
                juce::Rectangle<float> (ix, (float) rows::panel + 16, 540 - ix, 26), juce::Justification::centredLeft, true);
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
    e.linkBtn2.setBounds (92, rows::r0 + 6, 20, rows::r1 + rows::h - 6 - (rows::r0 + 6));
    e.sl0.setBounds (124, rows::y0, 306, rows::h);
    e.sl1.setBounds (124, rows::y1, 306, rows::h);
    e.val0.setBounds (440, rows::y0 + 1, 116, 34);
    e.val1.setBounds (440, rows::y1 + 1, 116, 34);
    // 2.0.0 : rotation G / D, mêmes colonnes que le décalage
    e.rs0.setBounds (124, rows::r0, 306, rows::h);
    e.rs1.setBounds (124, rows::r1, 306, rows::h);
    e.rv0.setBounds (440, rows::r0 + 1, 116, 34);
    e.rv1.setBounds (440, rows::r1 + 1, 116, 34);
    // rangée du bas : « Advanced settings » à gauche (x 16, comme le Compresseur), Align à droite (bord en x 556)
    e.advBtn.setBounds (16, rows::bottom, juce::roundToInt (e.advBtn.idealWidth()), 28);
    const int aw = std::max (88, juce::roundToInt (e.alignBtn.idealWidth()));
    e.alignBtn.setBounds (556 - aw, rows::bottom, aw, 28);
    e.latSeg.setBounds (40 + 78, rows::panel + 16, juce::roundToInt (e.latSeg.idealWidth()), 26);
    e.toast.setBounds (getLocalBounds().withHeight (rows::bottom - 8));
}
