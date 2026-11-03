// Captures de l'interface en PNG, hors DAW : vue Temps (échantillons, ms), Somme mono, Link, polarité, bypass,
// saisie au clavier, piste mono, 60 % et 200 % ; 2.0.0 : rotation, mode Zero, panneau avancé, résultat du bouton Align.
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace audiolab;
    const juce::File out (argc > 1 ? argv[1] : "/tmp");
    auto proc = std::make_unique<AudioLabPhaseProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000, 512);
    proc->prepareToPlay (48000, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed;
    std::vector<std::pair<int, std::function<void()>>> steps;
    auto shot = [&] (const juce::String& name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = out.getChildFile (name + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("%s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    auto rebuild = [&]
    {
        if (ed != nullptr) { proc->editorBeingDeleted (ed.get()); ed.reset(); }
        ed.reset (proc->createEditorAndMakeActive()); ed->setVisible (true);
    };
    auto set = [&] (const char* id, float v) { proc->setParam (id, v); };
    auto ui = [&] (const char* k, juce::var v) { proc->ui.setProperty (k, v, nullptr); };
    using E = AudioLabPhaseEditor;

    steps.push_back ({ 0,   [&] { ui ("view", 0); rebuild(); } });
    steps.push_back ({ 300, [&] { shot ("01_defaut"); set (params::link, 0); set (params::delL, -1.0f); set (params::delR, 2.5f); set (params::polR, 1); } });
    steps.push_back ({ 300, [&] { shot ("02_temps_echantillons"); ui ("view", 1); rebuild(); } });
    steps.push_back ({ 300, [&] { shot ("03_somme_mono"); set (params::polR, 0); set (params::delR, 0.5f); set (params::delL, 0); } });
    steps.push_back ({ 300, [&] { shot ("04_somme_mono_0_5ms"); set (params::unit, 1); set (params::delR, 0.0123f); set (params::delL, -7.5f); ui ("view", 0); rebuild(); } });
    steps.push_back ({ 300, [&] { shot ("05_temps_ms"); set (params::link, 1); set (params::delL, 3.0f); set (params::polL, 1); } });
    steps.push_back ({ 300, [&] { shot ("06_link_inverse"); set (params::bypass, 1); } });
    steps.push_back ({ 300, [&] { shot ("07_bypass"); set (params::bypass, 0); set (params::unit, 0);
                                  if (auto* c = ed->getChildComponent (0)) for (auto* k : c->getChildren())
                                      if (auto* v = dynamic_cast<ValueBox*> (k)) { v->startEditing(); v->repaint(); break; } } });
    steps.push_back ({ 300, [&] { shot ("08_saisie"); ed->setSize (E::baseW * 6 / 10, E::baseH * 6 / 10); } });
    steps.push_back ({ 300, [&] { shot ("09_60pc"); ed->setSize (E::baseW * 2, E::baseH * 2); } });
    steps.push_back ({ 300, [&] { shot ("10_200pc"); proc->releaseResources(); proc->setPlayConfigDetails (1, 1, 44100, 512); proc->prepareToPlay (44100, 512);
                                  set (params::link, 0); set (params::delL, 1.0f); set (params::polL, 0); rebuild(); } });
    // 2.0.0 : rotation, mode Zero, panneau avancé, bouton Align
    steps.push_back ({ 500, [&] { shot ("11_mono"); proc->releaseResources(); proc->setPlayConfigDetails (2, 2, 48000, 512); proc->prepareToPlay (48000, 512);
                                  set (params::link, 0); set (params::unit, 1); set (params::delL, 0); set (params::delR, 0.5f);
                                  set (params::rotL, 0); set (params::rotR, 90); ui ("view", 0); rebuild(); } });
    steps.push_back ({ 300, [&] { shot ("12_rotation_temps"); ui ("view", 1); rebuild(); } });
    steps.push_back ({ 300, [&] { shot ("13_rotation_somme"); set (params::delR, 0); set (params::rotR, 90); } });
    steps.push_back ({ 300, [&] { shot ("14_rotation_90_sans_retard"); set (params::link, 1); set (params::rotL, 0); set (params::delL, -1.0f);
                                  set (params::latMode, 1); ui ("adv", true); ui ("view", 0); rebuild(); } });
    steps.push_back ({ 300, [&] { shot ("15_avance_zero"); set (params::latMode, 0); set (params::delL, 0);
                                  // side-chain = 2e micro : piste inversée et retardée de 20 éch. ; mesure par le bouton Align
                                  AudioLabPhaseProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo());
                                  l.outputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (juce::AudioChannelSet::stereo());
                                  proc->releaseResources(); proc->setBusesLayout (l); proc->prepareToPlay (48000, 512);
                                  juce::Random rnd (5); std::vector<float> h (64, 0.0f); int hw = 0; juce::MidiBuffer m;
                                  for (int k = 0; k < 150; ++k)
                                  {
                                      juce::AudioBuffer<float> b (4, 512);
                                      for (int i = 0; i < 512; ++i) { const float v = rnd.nextFloat() - 0.5f; h[(size_t) hw] = v; const float d = h[(size_t) ((hw - 20 + 64) % 64)];
                                                                      hw = (hw + 1) % 64; b.setSample (0, i, v); b.setSample (1, i, v); b.setSample (2, i, -d); b.setSample (3, i, -d); }
                                      proc->processBlock (b, m);
                                  }
                                  ui ("adv", false); rebuild(); proc->alignNow(); } });
    steps.push_back ({ 400, [&] { shot ("16_align_resultat"); ed->setSize (E::baseW * 6 / 10, E::baseH * 6 / 10); } });
    steps.push_back ({ 300, [&] { shot ("17_60pc"); ui ("adv", true); rebuild(); ed->setSize (E::baseW * 2, E::baseHOpen * 2); } });
    steps.push_back ({ 300, [&] { shot ("18_avance_200pc"); ui ("adv", false); juce::MessageManager::getInstance()->stopDispatchLoop(); } });

    std::function<void (size_t)> next = [&] (size_t i)
    {
        if (i >= steps.size()) return;
        juce::Timer::callAfterDelay (std::max (1, steps[i].first), [&, i] { steps[i].second(); next (i + 1); });
    };
    next (0);
    juce::MessageManager::getInstance()->runDispatchLoop();

    proc->editorBeingDeleted (ed.get()); ed.reset();
    proc->releaseResources();
    return 0;
}
