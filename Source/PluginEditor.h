#pragma once
#include "PluginProcessor.h"
#include "PhaseWidgets.h"
#include "Display.h"

class AudioLabPhaseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioLabPhaseEditor (AudioLabPhaseProcessor&);
    ~AudioLabPhaseEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Grille de 24 px (docs/13_GUI_Plugin_Phase.md). 2.0.0 : rangées Rotation et « Advanced settings » ajoutées
    // sous les réglages validés (396 → 586), panneau avancé ouvert : 660
    static constexpr int baseW = 580, baseH = 586, baseHOpen = 660;

    void setAdvancedForTest (bool open) { setAdvanced (open, true); }

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabPhaseEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        AudioLabPhaseEditor& ed;
    };

    void timerCallback() override;
    void refreshAll();
    void setAdvanced (bool open, bool resizeWindow);
    int currentH() const { return advOpen ? baseHOpen : baseH; }
    juce::String latencyText() const;

    AudioLabPhaseProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    audiolab::PhaseDisplay display;
    audiolab::SegButtons viewSeg { theme, { "Time", "Mono sum" } };
    audiolab::SegButtons unitSeg { theme, { "Samples", "ms" } };
    audiolab::PolButton pol0, pol1;
    audiolab::LinkButton linkBtn, linkBtn2;   // 2.0.0 : un Link entre les lignes de décalage, un entre les lignes de rotation (même réglage)
    audiolab::DelaySlider sl0, sl1;
    audiolab::ValueBox val0, val1;
    // 2.0.0
    audiolab::RotSlider rs0, rs1;
    audiolab::RotValueBox rv0, rv1;
    audiolab::Disclosure advBtn { theme, "Advanced settings" };
    audiolab::PillButton alignBtn { theme, "Align" };
    audiolab::SegButtons latSeg { theme, { "Standard", "Zero" } };
    audiolab::Toast toast { theme };
    bool advOpen = false;
    int lastAlignSerial = -1;

    juce::String lastState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabPhaseEditor)
};
