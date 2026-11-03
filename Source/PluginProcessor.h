#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PhaseCore.h"
#include "DelayEstimator.h"
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
    juce::ValueTree ui;   // taille de la fenêtre, vue de l'écran, panneau avancé
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const char* id, float v) { auto* q = apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

    // Réglages tels que le moteur les applique (Link : D suit G)
    audiolab::PhaseParams currentParams() const;
    // Fréquence d'échantillonnage pour l'affichage (48 kHz tant que le logiciel n'a rien préparé)
    double displayRate() const { const double r = getSampleRate(); return r > 0 ? r : 48000.0; }
    bool isMono() const { return getMainBusNumOutputChannels() == 1; }
    bool zeroLatency() const { return getParam (audiolab::params::latMode) > 0.5f; }
    int latencySamples() const { return audiolab::PhaseCore::latencyFor (displayRate(), zeroLatency()); }
    // Applique tout de suite les mises à jour en attente (recopie Link, latence, résultat d'Align) :
    // tests ; sinon faites au prochain passage du fil de l'interface
    void flushLinkCopy() { handleUpdateNowIfNeeded(); }

    // ── Alignement sur le side-chain (2.0.0) ──
    // Le bouton Align mesure, dans un fil de fond, le décalage, la polarité et la rotation de la piste par rapport
    // au side-chain sur les ≈ 1,4 dernières secondes, puis règle Lab Phase pour caler la piste sur le side-chain.
    struct AlignStatus
    {
        enum class State { idle, listening, done, failed } state = State::idle;
        juce::String title, detail;   // ex. « Aligned on the side-chain » / « +0.417 ms · polarity inverted · confidence 94 % »
        int serial = 0;
    };
    void requestAlign();            // interface : lance la mesure (fil de fond)
    bool alignNow();                // tests : mesure et réglages tout de suite, dans le fil appelant ; true si réglé
    AlignStatus alignStatus() const;
    bool sideChainConnected() const { return scConnected.load(); }
    static constexpr double minConfidence = 0.15;

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;

    struct AlignPlan
    {
        bool ok = false;
        juce::String title, detail;
        bool linked = true;
        double ms[2] = { 0, 0 }, rot[2] = { 0, 0 };
        bool inv[2] = { false, false };
        bool setUnitMs = false;
    };
    bool computeAlign (AlignPlan& plan);   // fil de fond (ou tests) : mesure et plan des réglages
    void applyAlign (const AlignPlan& plan);   // fil de l'interface : réglages + message
    void setStatus (AlignStatus::State st, const juce::String& title, const juce::String& detail);

    audiolab::PhaseCore core;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    int currentProgram = 0;

    // Anneau de la mesure (fil audio → fil de fond) : piste G, D (avant traitement) et side-chain (somme mono)
    std::vector<float> ringL, ringR, ringS;
    int ringMask = 0, alignLen = 65536;
    std::atomic<juce::uint64> ringWritten { 0 };
    std::atomic<double> rateAt { 48000 };
    std::atomic<bool> scConnected { false };
    juce::CriticalSection ringLock;   // prepareToPlay ↔ fil de mesure (jamais pris par le fil audio)
    mutable juce::SpinLock statusLock;
    AlignStatus status;
    AlignPlan pendingPlan;
    std::atomic<bool> planReady { false }, latencyDirty { false };

    struct AlignThread : juce::Thread
    {
        explicit AlignThread (AudioLabPhaseProcessor& p) : juce::Thread ("Lab Phase align"), proc (p) {}
        void run() override;
        AudioLabPhaseProcessor& proc;
    } alignThread { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabPhaseProcessor)
};
