// Écran d'Audio Lab Phase (532 × 176, « Surface ») : deux vues
//  - Temps : position de G et D par rapport au son d'origine (trait pointillé), de −10 à +10 ms ;
//    impulsion vers le haut = polarité normale, vers le bas = inversée.
//  - Somme mono : réponse en fréquence de G + D (filtre en peigne), 20 Hz → 20 kHz (log), 0 → −30 dB.
//  2.0.0 : rotation de phase (impulsion « étalée » dans la vue Temps, décalage des creux dans la Somme mono), mode Zero.
// Rien ne s'anime : l'écran est redessiné seulement quand un réglage change.
#pragma once
#include "PluginProcessor.h"
#include "Theme.h"

namespace audiolab
{
class PhaseDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    PhaseDisplay (Theme& t, AudioLabPhaseProcessor& p) : theme (t), proc (p) {}
    void setView (int v) { view = v; repaint(); updateTooltip(); }
    int getView() const { return view; }
    void paint (juce::Graphics& g) override;
    void updateTooltip();

    // Ce que le moteur applique (pour l'écran et les tests) : décalage effectif en échantillons et polarité, par canal
    struct State { double s[2] = { 0, 0 }; bool inv[2] = { false, false }; double rot[2] = { 0, 0 };   // rotation : 2.0.0
                   bool mono = false, bypass = false, samplesUnit = true, zero = false; double fs = 48000; };
    // Phase de D par rapport à G hors retard (rotations et polarités), en radians
    static double phaseOffset (const State&);
    State state() const;
    // Texte d'explication de la somme mono (creux du filtre en peigne)
    static juce::String combText (const State&);

private:
    void paintTime (juce::Graphics&, const State&);
    void paintSum (juce::Graphics&, const State&);
    Theme& theme;
    AudioLabPhaseProcessor& proc;
    int view = 0;
};
} // namespace audiolab
