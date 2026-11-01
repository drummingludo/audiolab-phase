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

    // Grille de 24 px (docs/13_GUI_Plugin_Phase.md)
    static constexpr int baseW = 580, baseH = 396;

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

    AudioLabPhaseProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    audiolab::PhaseDisplay display;
    audiolab::SegButtons viewSeg { theme, { "Temps", "Somme mono" } };
    audiolab::SegButtons unitSeg { theme, { juce::String::fromUTF8 ("Échantillons"), "ms" } };
    audiolab::PolButton pol0, pol1;
    audiolab::LinkButton linkBtn;
    audiolab::DelaySlider sl0, sl1;
    audiolab::ValueBox val0, val1;

    juce::String lastState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabPhaseEditor)
};
