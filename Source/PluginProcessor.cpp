#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace audiolab;

AudioLabPhaseProcessor::AudioLabPhaseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AudioLabPhase", params::createLayout()),
      ui ("ui")
{
    bypassParam = apvts.getParameter (params::bypass);
    ui.setProperty ("scale", 1.0, nullptr);
    ui.setProperty ("view", 0, nullptr);
    for (auto* id : { params::delL, params::polL, params::link })
        apvts.addParameterListener (id, this);
    // fenêtre de 48 kHz par défaut : la latence est connue dès le chargement (avant prepareToPlay)
    setLatencySamples (PhaseCore::latencyFor (48000.0));
}

AudioLabPhaseProcessor::~AudioLabPhaseProcessor()
{
    cancelPendingUpdate();
    for (auto* id : { params::delL, params::polL, params::link })
        apvts.removeParameterListener (id, this);
}

bool AudioLabPhaseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

PhaseParams AudioLabPhaseProcessor::currentParams() const
{
    PhaseParams p;
    const bool linked = getParam (params::link) > 0.5f;
    p.delayMs[0] = getParam (params::delL);
    p.invert[0] = getParam (params::polL) > 0.5f;
    p.delayMs[1] = linked ? p.delayMs[0] : (double) getParam (params::delR);
    p.invert[1] = linked ? p.invert[0] : getParam (params::polR) > 0.5f;
    p.samples = getParam (params::unit) < 0.5f;
    p.bypass = getParam (params::bypass) > 0.5f;
    return p;
}

// Link : D recopie G (dans les paramètres aussi), pour que rien ne saute quand on délie les canaux
void AudioLabPhaseProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

void AudioLabPhaseProcessor::handleAsyncUpdate()
{
    if (getParam (params::link) < 0.5f) return;
    if (! juce::exactlyEqual (getParam (params::delR), getParam (params::delL))) setParam (params::delR, getParam (params::delL));
    if ((getParam (params::polR) > 0.5f) != (getParam (params::polL) > 0.5f)) setParam (params::polR, getParam (params::polL));
}

void AudioLabPhaseProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate, std::max (1, getTotalNumOutputChannels()));
    setLatencySamples (core.getLatency());
    core.setParams (currentParams());
}

void AudioLabPhaseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels();
    for (int ch = nIn; ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, n);
    if (nIn == 0 || n == 0) return;
    core.setParams (currentParams());
    float* io[2] = { buffer.getWritePointer (0), nIn > 1 ? buffer.getWritePointer (1) : nullptr };
    core.process (io, n);
}

void AudioLabPhaseProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= params::numPresets) return;
    currentProgram = index;
    const auto& p = params::presets[index];
    setParam (params::link, p.link ? 1.0f : 0.0f);
    setParam (params::delL, p.delL); setParam (params::delR, p.delR);
    setParam (params::polL, p.polL ? 1.0f : 0.0f); setParam (params::polR, p.polR ? 1.0f : 0.0f);
}

const juce::String AudioLabPhaseProcessor::getProgramName (int index)
{
    return index >= 0 && index < params::numPresets ? juce::String::fromUTF8 (params::presets[index].name) : juce::String();
}

void AudioLabPhaseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto old = state.getChildWithName ("ui"); old.isValid()) state.removeChild (old, nullptr);
    state.appendChild (ui.createCopy(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AudioLabPhaseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        if (auto u = state.getChildWithName ("ui"); u.isValid())
        {
            ui.copyPropertiesAndChildrenFrom (u, nullptr);
            state.removeChild (u, nullptr);
        }
        currentProgram = juce::jlimit (0, params::numPresets - 1, (int) state.getProperty ("program", 0));
        state.removeProperty ("program", nullptr);
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* AudioLabPhaseProcessor::createEditor() { return new AudioLabPhaseEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioLabPhaseProcessor(); }
