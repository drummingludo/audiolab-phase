// Captures de l'interface en PNG, hors DAW : vue Temps (échantillons, ms), Somme mono, Link, polarité, bypass,
// saisie au clavier, piste mono, 60 % et 200 %.
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
    steps.push_back ({ 500, [&] { shot ("11_mono"); juce::MessageManager::getInstance()->stopDispatchLoop(); } });

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
