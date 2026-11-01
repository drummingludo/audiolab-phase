#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PhaseCore.h"
#include "Params.h"

class AudioLabPhaseProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    AudioLabPhaseProcessor();
    ~AudioLabPhaseProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // après la latence compensée, le son peut sortir jusqu'à 10 ms plus tard (+ fondu)
    double getTailLengthSeconds() const override { return 0.011; }

    int getNumPrograms() override { return audiolab::params::numPresets; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    juce::ValueTree ui;   // taille de la fenêtre, vue de l'écran
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const char* id, float v) { auto* q = apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

    // Réglages tels que le moteur les applique (Link : D suit G)
    audiolab::PhaseParams currentParams() const;
    // Fréquence d'échantillonnage pour l'affichage (48 kHz tant que le logiciel n'a rien préparé)
    double displayRate() const { const double r = getSampleRate(); return r > 0 ? r : 48000.0; }
    bool isMono() const { return getTotalNumOutputChannels() == 1; }
    int latencySamples() const { return audiolab::PhaseCore::latencyFor (displayRate()); }
    // Applique tout de suite la recopie Link en attente (tests ; sinon faite au prochain passage du fil de l'interface)
    void flushLinkCopy() { handleUpdateNowIfNeeded(); }

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;

    audiolab::PhaseCore core;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabPhaseProcessor)
};
