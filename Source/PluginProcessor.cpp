#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace audiolab;

AudioLabPhaseProcessor::AudioLabPhaseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Side-chain", juce::AudioChannelSet::stereo(), true)),   // 2.0.0 : référence du bouton Align
      apvts (*this, nullptr, "AudioLabPhase", params::createLayout()),
      ui ("ui")
{
    bypassParam = apvts.getParameter (params::bypass);
    ui.setProperty ("scale", 1.0, nullptr);
    ui.setProperty ("view", 0, nullptr);
    for (auto* id : { params::delL, params::polL, params::link, params::rotL, params::latMode })
        apvts.addParameterListener (id, this);
    // fenêtre de 48 kHz par défaut : la latence est connue dès le chargement (avant prepareToPlay)
    setLatencySamples (PhaseCore::latencyFor (48000.0));
}

AudioLabPhaseProcessor::~AudioLabPhaseProcessor()
{
    alignThread.stopThread (3000);
    cancelPendingUpdate();
    for (auto* id : { params::delL, params::polL, params::link, params::rotL, params::latMode })
        apvts.removeParameterListener (id, this);
}

bool AudioLabPhaseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (out != layouts.getMainInputChannelSet()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

PhaseParams AudioLabPhaseProcessor::currentParams() const
{
    PhaseParams p;
    const bool linked = getParam (params::link) > 0.5f;
    p.delayMs[0] = getParam (params::delL);
    p.invert[0] = getParam (params::polL) > 0.5f;
    p.rotDeg[0] = getParam (params::rotL);
    p.delayMs[1] = linked ? p.delayMs[0] : (double) getParam (params::delR);
    p.invert[1] = linked ? p.invert[0] : getParam (params::polR) > 0.5f;
    p.rotDeg[1] = linked ? p.rotDeg[0] : (double) getParam (params::rotR);
    p.samples = getParam (params::unit) < 0.5f;
    p.bypass = getParam (params::bypass) > 0.5f;
    p.zeroLatency = zeroLatency();
    return p;
}

// Link : D recopie G (dans les paramètres aussi), pour que rien ne saute quand on délie les canaux.
// Mode de latence : la latence annoncée change (depuis le fil de l'interface).
void AudioLabPhaseProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == params::latMode) latencyDirty = true;
    triggerAsyncUpdate();
}

void AudioLabPhaseProcessor::handleAsyncUpdate()
{
    if (latencyDirty.exchange (false)) setLatencySamples (latencySamples());
    if (planReady.exchange (false))
    {
        AlignPlan plan;
        { const juce::SpinLock::ScopedLockType l (statusLock); plan = pendingPlan; }
        applyAlign (plan);
    }
    if (getParam (params::link) < 0.5f) return;
    if (! juce::exactlyEqual (getParam (params::delR), getParam (params::delL))) setParam (params::delR, getParam (params::delL));
    if ((getParam (params::polR) > 0.5f) != (getParam (params::polL) > 0.5f)) setParam (params::polR, getParam (params::polL));
    if (! juce::exactlyEqual (getParam (params::rotR), getParam (params::rotL))) setParam (params::rotR, getParam (params::rotL));
}

void AudioLabPhaseProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate, std::max (1, getMainBusNumOutputChannels()));
    setLatencySamples (PhaseCore::latencyFor (sampleRate, zeroLatency()));
    core.setParams (currentParams());
    // Anneau du bouton Align : ≈ 1,4 s (puissance de 2), anneau 2 fois plus long
    const juce::ScopedLock sl (ringLock);
    int n = 16384;
    while (n < (int) (1.2 * sampleRate) && n < 262144) n <<= 1;
    alignLen = n;
    for (auto* r : { &ringL, &ringR, &ringS }) r->assign ((size_t) (2 * n), 0.0f);
    ringMask = 2 * n - 1;
    ringWritten.store (0);
    rateAt.store (sampleRate);
}

void AudioLabPhaseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getMainBusNumInputChannels();
    for (int ch = nIn; ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, n);
    if (nIn == 0 || n == 0) return;

    // copie pour le bouton Align (avant traitement) : piste G, D et somme mono du side-chain
    const float* sc0 = nullptr; const float* sc1 = nullptr;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0) { sc0 = sc.getReadPointer (0); sc1 = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : sc0; }
        }
    scConnected.store (sc0 != nullptr);
    if (! ringL.empty())
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = nIn > 1 ? buffer.getReadPointer (1) : l;
        const auto w = ringWritten.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((w + (juce::uint64) i) & (juce::uint64) ringMask);
            ringL[k] = l[i]; ringR[k] = r[i];
            ringS[k] = sc0 != nullptr ? 0.5f * (sc0[i] + sc1[i]) : 0.0f;
        }
        ringWritten.store (w + (juce::uint64) n, std::memory_order_release);
    }

    core.setParams (currentParams());
    float* io[2] = { buffer.getWritePointer (0), nIn > 1 ? buffer.getWritePointer (1) : nullptr };
    core.process (io, n);
}

// ── Alignement sur le side-chain ────────────────────────────────────────

static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

void AudioLabPhaseProcessor::setStatus (AlignStatus::State st, const juce::String& title, const juce::String& detail)
{
    const juce::SpinLock::ScopedLockType l (statusLock);
    status.state = st; status.title = title; status.detail = detail; ++status.serial;
}

AudioLabPhaseProcessor::AlignStatus AudioLabPhaseProcessor::alignStatus() const
{
    const juce::SpinLock::ScopedLockType l (statusLock);
    return status;
}

void AudioLabPhaseProcessor::requestAlign()
{
    if (alignThread.isThreadRunning()) return;
    setStatus (AlignStatus::State::listening, U ("Listening\xe2\x80\xa6"), U ("Measuring the track against the side-chain"));
    alignThread.startThread (juce::Thread::Priority::low);
}

void AudioLabPhaseProcessor::AlignThread::run()
{
    // jusqu'à 4 s d'attente si l'on n'a pas encore assez de signal (lecture lancée juste avant le clic)
    AlignPlan plan;
    for (int tries = 0; tries < 40 && ! threadShouldExit(); ++tries)
    {
        plan = AlignPlan();
        if (proc.computeAlign (plan) || plan.title.isNotEmpty()) break;
        wait (100);
    }
    if (threadShouldExit()) return;
    if (! plan.ok && plan.title.isEmpty()) { plan.title = U ("Not enough signal"); plan.detail = U ("Play a passage where the track and the side-chain sound together"); }
    { const juce::SpinLock::ScopedLockType l (proc.statusLock); proc.pendingPlan = plan; }
    proc.planReady = true;
    proc.triggerAsyncUpdate();
}

bool AudioLabPhaseProcessor::alignNow()
{
    AlignPlan plan;
    const bool ok = computeAlign (plan);
    if (! ok && plan.title.isEmpty()) { plan.title = U ("Not enough signal"); plan.detail = U ("Play a passage where the track and the side-chain sound together"); }
    applyAlign (plan);
    return ok;
}

// Mesure et plan des réglages. Renvoie true si l'on peut régler ; sinon plan.message explique pourquoi
// (titre vide : pas encore assez de signal, on réessaiera).
bool AudioLabPhaseProcessor::computeAlign (AlignPlan& plan)
{
    std::vector<float> L, R, S;
    int n = 0; double fs = 48000;
    {
        const juce::ScopedLock sl (ringLock);
        if (ringL.empty()) return false;
        n = alignLen; fs = rateAt.load();
        const auto w = ringWritten.load (std::memory_order_acquire);
        if (w < (juce::uint64) n) return false;
        L.resize ((size_t) n); R.resize ((size_t) n); S.resize ((size_t) n);
        const auto start = w - (juce::uint64) n;
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((start + (juce::uint64) i) & (juce::uint64) ringMask);
            L[(size_t) i] = ringL[k]; R[(size_t) i] = ringR[k]; S[(size_t) i] = ringS[k];
        }
        if (ringWritten.load (std::memory_order_acquire) - w > (juce::uint64) (ringMask + 1 - n)) return false;   // rattrapé
    }
    double eS = 0, eT = 0;
    for (int i = 0; i < n; ++i) { eS += (double) S[(size_t) i] * S[(size_t) i]; eT += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; }
    if (! scConnected.load() || eS < 1e-9 * n)
    {
        plan.title = U ("No side-chain signal");
        plan.detail = U ("Send the reference track (the other mic) to the plugin's side-chain input");
        return false;
    }
    if (eT < 1e-9 * n) { plan.title = U ("No signal on the track"); plan.detail = U ("Play a passage where the track and the side-chain sound together"); return false; }

    const bool mono = getMainBusNumInputChannels() < 2;
    plan.linked = mono || getParam (params::link) > 0.5f;
    const bool zero = zeroLatency();
    align::Result res[2];
    if (plan.linked)
    {
        std::vector<float> M ((size_t) n);
        for (int i = 0; i < n; ++i) M[(size_t) i] = mono ? L[(size_t) i] : 0.5f * (L[(size_t) i] + R[(size_t) i]);
        res[0] = res[1] = align::DelayEstimator::estimate (M.data(), S.data(), n, fs, PhaseCore::maxMs);
    }
    else
    {
        res[0] = align::DelayEstimator::estimate (L.data(), S.data(), n, fs, PhaseCore::maxMs);
        res[1] = align::DelayEstimator::estimate (R.data(), S.data(), n, fs, PhaseCore::maxMs);
    }
    const int nRes = plan.linked ? 1 : 2;
    for (int c = 0; c < nRes; ++c)
    {
        const auto& r = res[c];
        if (! r.valid || r.confidence < minConfidence)
        {
            plan.title = U ("No clear match (confidence ") + juce::String (juce::roundToInt (r.confidence * 100)) + U (" %)");
            plan.detail = U ("Play a passage where the track and the side-chain hear the same sound");
            return false;
        }
        if (std::abs (r.lagMs) >= PhaseCore::maxMs - 0.02)
        {
            plan.title = U ("Offset beyond 10 ms");
            plan.detail = U ("Lab Phase cannot line these two signals up");
            return false;
        }
        if (zero && r.lagSamples < -0.5)
        {
            plan.title = U ("The track is late");
            plan.detail = U ("Switch Latency to Standard (Advanced settings) to move it earlier");
            return false;
        }
    }
    // Réglages : SC ≈ Rot_θ { piste retardée de τ } → la piste reçoit le décalage τ, la polarité et la rotation résiduelle
    const bool samplesUnit = getParam (params::unit) < 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        const auto& r = res[plan.linked ? 0 : c];
        plan.ms[c] = zero ? std::round (r.lagSamples) * 1000.0 / fs : r.lagMs;
        plan.inv[c] = r.invert;
        plan.rot[c] = ! zero && std::abs (r.residualDeg) >= 3.0 ? std::fmod (r.residualDeg + 360.0, 360.0) : 0.0;
        if (! zero && samplesUnit && std::abs (r.lagSamples - std::round (r.lagSamples)) > 0.02) plan.setUnitMs = true;
    }
    // message : « +0.417 ms · polarity inverted · rotation 12° · confidence 94 % »
    const auto dot = U (" \xc2\xb7 ");
    auto describe = [&] (int c, const juce::String& prefix)
    {
        juce::String s = prefix + params::fmtMs (plan.ms[c]);
        if (plan.inv[c]) s += dot + U ("polarity inverted");
        if (plan.rot[c] > 0) s += dot + U ("rotation ") + params::fmtDeg (plan.rot[c]);
        return s;
    };
    const double conf = std::min (res[0].confidence, res[nRes - 1].confidence);
    plan.title = U ("Aligned on the side-chain") + (plan.setUnitMs ? U (" (unit set to ms)") : juce::String());
    plan.detail = (plan.linked ? describe (0, {}) : describe (0, "L ") + U ("  |  ") + describe (1, "R "))
                + dot + U ("confidence ") + juce::String (juce::roundToInt (conf * 100)) + U (" %");
    plan.ok = true;
    return true;
}

void AudioLabPhaseProcessor::applyAlign (const AlignPlan& plan)
{
    if (! plan.ok) { setStatus (AlignStatus::State::failed, plan.title, plan.detail); return; }
    auto put = [this] (const char* id, float v)
    {
        auto* q = apvts.getParameter (id);
        q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture();
    };
    if (plan.setUnitMs) put (params::unit, 1);
    put (params::delL, (float) plan.ms[0]); put (params::polL, plan.inv[0] ? 1.0f : 0.0f); put (params::rotL, (float) plan.rot[0]);
    if (plan.linked) { put (params::delR, (float) plan.ms[0]); put (params::polR, plan.inv[0] ? 1.0f : 0.0f); put (params::rotR, (float) plan.rot[0]); }
    else { put (params::delR, (float) plan.ms[1]); put (params::polR, plan.inv[1] ? 1.0f : 0.0f); put (params::rotR, (float) plan.rot[1]); }
    setStatus (AlignStatus::State::done, plan.title, plan.detail);
}

// ── Presets, session ────────────────────────────────────────────────────

void AudioLabPhaseProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= params::numPresets) return;
    currentProgram = index;
    const auto& p = params::presets[index];
    setParam (params::link, p.link ? 1.0f : 0.0f);
    setParam (params::delL, p.delL); setParam (params::delR, p.delR);
    setParam (params::polL, p.polL ? 1.0f : 0.0f); setParam (params::polR, p.polR ? 1.0f : 0.0f);
    setParam (params::rotL, 0.0f); setParam (params::rotR, 0.0f);   // 2.0.0 : aucun preset ne tourne la phase
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
        // Session d'une version 1.x : les réglages absents (rotation, latence) prennent leur valeur par défaut
        // (sans cela, APVTS garderait la valeur du moment)
        for (auto* prm : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (! state.getChildWithProperty ("id", rp->paramID).isValid())
                {
                    juce::ValueTree c ("PARAM");
                    c.setProperty ("id", rp->paramID, nullptr);
                    c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                    state.appendChild (c, nullptr);
                }
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* AudioLabPhaseProcessor::createEditor() { return new AudioLabPhaseEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioLabPhaseProcessor(); }
