#include "PluginEditor.h"
#include <thread>

namespace
{
const juce::Colour kBg (0xff14161b), kPanel (0xff1e2128), kAccent (0xff3ddc97), kAccent2 (0xfff2b84b),
                   kText (0xffe8eaed), kDim (0xff8a919e), kWarn (0xffe5645a);

juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }

juce::String keyName (int tonic, bool minor, bool latin)
{
    const char* const* names = latin ? ks::noteNamesLatin() : ks::noteNamesEnglish();
    return juce::String (names[tonic]) + (minor ? " menor" : " mayor");
}
}

//==============================================================================
void KeyDisplay::update()
{
    snap = proc.getSnapshot();
    shift = proc.getComputedShift();
    note = proc.lastUsedNotePath();
    gran = proc.lastUsedGranular();
    active = proc.isShiftActive();
    latin = (int) std::lround (proc.apvts.getRawParameterValue (pid::notation)->load()) == 0;
    repaint();
}

void KeyDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (b, 10.f);
    g.setColour (juce::Colour (0xff2a2f38));
    g.drawRoundedRectangle (b.reduced (0.5f), 10.f, 1.f);

    auto area = getLocalBounds().reduced (16, 10);
    const bool has = snap.hasCommitted && snap.committed.valid;
    const auto& r = has ? snap.committed : snap.live;
    const bool any = has || (snap.live.valid && snap.eventActive);

    g.setFont (juce::FontOptions (12.f));
    g.setColour (kDim);
    g.drawText (snap.eventActive && ! snap.currentEventCommitted ? U8 ("ANALIZANDO\xe2\x80\xa6") : "SAMPLE DETECTADO",
                area.removeFromTop (16), juce::Justification::centredLeft);

    if (! any)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (22.f));
        g.drawFittedText (U8 ("Reproduce un sample por esta pista\xe2\x80\xa6"), area, juce::Justification::centred, 2);
        return;
    }

    const auto kindText = [&]() -> juce::String
    {
        switch (r.kind)
        {
            case ks::SoundKind::Tonal:       return U8 ("Tonal (melod\xc3\xad" "a / acorde)");
            case ks::SoundKind::SinglePitch: return U8 ("Percusi\xc3\xb3n tonal / nota \xc3\xbanica");
            case ks::SoundKind::Noise:       return U8 ("Percusi\xc3\xb3n sin altura (caja, hat\xe2\x80\xa6)");
            default:                         return {};
        }
    }();

    auto left = area.removeFromLeft (area.getWidth() * 55 / 100);
    const bool noPitch = r.kind == ks::SoundKind::Noise;

    g.setColour (noPitch ? kDim : kAccent);
    g.setFont (juce::FontOptions (noPitch ? 28.f : 38.f, juce::Font::bold));
    if (noPitch)
        g.drawFittedText (U8 ("Sin tonalidad clara"), left.removeFromTop (54), juce::Justification::centredLeft, 1);
    else if (r.kind == ks::SoundKind::SinglePitch && r.hasDominant)
        g.drawFittedText (U8 ("Nota: ") + juce::String (ks::noteWithOctave (r.dominantMidi, latin)), left.removeFromTop (54), juce::Justification::centredLeft, 1);
    else
        g.drawFittedText (keyName (r.tonic, r.minor, latin), left.removeFromTop (54), juce::Justification::centredLeft, 1);

    g.setFont (juce::FontOptions (13.f));
    g.setColour (kText);
    g.drawText (kindText, left.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (kDim);
    if (r.hasDominant)
        g.drawText (U8 ("Nota m\xc3\xa1s fuerte: ") + juce::String (ks::noteWithOctave (r.dominantMidi, latin))
                    + "  (" + juce::String (r.dominantHz, 1) + " Hz)", left.removeFromTop (20), juce::Justification::centredLeft);
    if (! noPitch)
    {
        const int c = (int) std::lround (r.tuningCents);
        g.drawText (U8 ("Afinaci\xc3\xb3n: ") + (c >= 0 ? "+" : "") + juce::String (c) + " cents", left.removeFromTop (20), juce::Justification::centredLeft);
    }

    // columna derecha: confianza + cambio calculado
    auto right = area.reduced (10, 0);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.f));
    g.drawText ("CONFIANZA", right.removeFromTop (16), juce::Justification::centredLeft);
    auto bar = right.removeFromTop (10).toFloat();
    g.setColour (juce::Colour (0xff2a2f38));
    g.fillRoundedRectangle (bar, 5.f);
    const float conf = noPitch ? 0.f : juce::jlimit (0.f, 1.f, r.confidence);
    g.setColour (conf > 0.55f ? kAccent : conf > 0.3f ? kAccent2 : kWarn);
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.f, bar.getWidth() * conf)), 5.f);
    right.removeFromTop (12);

    g.setColour (kDim);
    g.drawText ("CAMBIO DE TONO", right.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (active ? kAccent2 : kText);
    g.setFont (juce::FontOptions (26.f, juce::Font::bold));
    const juce::String sh = (shift >= 0 ? "+" : "") + juce::String (shift, 2) + " st";
    g.drawText (sh, right.removeFromTop (34), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.f));
    g.setColour (kDim);
    g.drawText (juce::String (U8 (gran ? "Motor percusivo" : "Motor tonal")) + (active ? "  \xe2\x80\xa2 activo" : "  \xe2\x80\xa2 sin aplicar"),
                right.removeFromTop (18), juce::Justification::centredLeft);
}

//==============================================================================
void DragWavLabel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool ok = file.existsAsFile();
    g.setColour (ok ? kAccent.withAlpha (0.15f) : kPanel);
    g.fillRoundedRectangle (b, 6.f);
    g.setColour (ok ? kAccent : juce::Colour (0xff2a2f38));
    g.drawRoundedRectangle (b, 6.f, 1.f);
    g.setColour (ok ? kText : kDim);
    g.setFont (juce::FontOptions (12.f));
    g.drawFittedText (ok ? U8 ("\xe2\x86\x97 Arrastra aqu\xc3\xad el WAV hacia tu DAW:  ") + file.getFileName()
                         : U8 ("Tras exportar podr\xc3\xa1s arrastrar el WAV a tu DAW"),
                      getLocalBounds().reduced (8, 2), juce::Justification::centred, 1);
}

void DragWavLabel::mouseDrag (const juce::MouseEvent& e)
{
    if (file.existsAsFile() && e.getDistanceFromDragStart() > 6)
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
KeyShiftEditor::KeyShiftEditor (KeyShiftProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setSize (640, 560);
    addAndMakeVisible (display);

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, kDim);
        l.setFont (juce::FontOptions (12.f));
        addAndMakeVisible (l);
    };
    setupLabel (lblTarget, U8 ("CAMBIAR A LA TONALIDAD"));
    setupLabel (lblSrc, U8 ("Tonalidad de origen (manual)"));
    setupLabel (lblKind, "Tipo de sonido");
    setupLabel (lblEngine, "Motor");
    setupLabel (lblOct, "Octava");
    setupLabel (lblFine, "Ajuste fino (cents)");
    setupLabel (lblExport, U8 ("Exportar como"));
    setupLabel (lblNotation, U8 ("Notaci\xc3\xb3n"));
    setupLabel (lblStatus, "");

    // botones de nota destino (radio manual, ligado al parámetro)
    static const char* shortNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
    {
        auto& b = rootButtons[i];
        b.setButtonText (shortNames[i]);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, kPanel);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.setColour (juce::TextButton::textColourOffId, kText);
        b.onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (pid::targetRoot))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }
    auto modeClick = [this] (bool minor)
    {
        if (auto* prm = proc.apvts.getParameter (pid::targetMode))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (minor ? 1.f : 0.f));
            prm->endChangeGesture();
        }
    };
    for (auto* b : { &majorBtn, &minorBtn })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::buttonOnColourId, kAccent2);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->setColour (juce::TextButton::textColourOffId, kText);
        addAndMakeVisible (*b);
    }
    majorBtn.onClick = [modeClick] { modeClick (false); };
    minorBtn.onClick = [modeClick] { modeClick (true); };

    applyBtn.setColour (juce::ToggleButton::textColourId, kText);
    applyBtn.setColour (juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible (applyBtn);

    for (auto* t : { &srcAutoBtn, &autoTuneBtn, &lockBtn })
    {
        t->setColour (juce::ToggleButton::textColourId, kText);
        t->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*t);
    }
    for (auto* b : { &resetBtn, &exportBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::textColourOffId, kText);
        addAndMakeVisible (*b);
    }
    exportBtn.setColour (juce::TextButton::buttonColourId, kAccent.darker (0.4f));

    auto fillCombo = [this] (juce::ComboBox& c, const char* param)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (param)))
            c.addItemList (choice->choices, 1);
        addAndMakeVisible (c);
    };
    fillCombo (srcRootBox, pid::srcRoot); fillCombo (srcModeBox, pid::srcMode); fillCombo (kindBox, pid::kind);
    fillCombo (engineBox, pid::engine); fillCombo (exportBox, pid::exportMode); fillCombo (notationBox, pid::notation);

    for (auto* s : { &octaveSlider, &fineSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        s->setColour (juce::Slider::trackColourId, kAccent);
        s->setColour (juce::Slider::thumbColourId, kText);
        addAndMakeVisible (*s);
    }
    octaveSlider.setNumDecimalPlacesToDisplay (0);
    fineSlider.setDoubleClickReturnValue (true, 0.0);

    aSrcRoot = std::make_unique<CBA> (proc.apvts, pid::srcRoot, srcRootBox);
    aSrcMode = std::make_unique<CBA> (proc.apvts, pid::srcMode, srcModeBox);
    aKind = std::make_unique<CBA> (proc.apvts, pid::kind, kindBox);
    aEngine = std::make_unique<CBA> (proc.apvts, pid::engine, engineBox);
    aExport = std::make_unique<CBA> (proc.apvts, pid::exportMode, exportBox);
    aNotation = std::make_unique<CBA> (proc.apvts, pid::notation, notationBox);
    aApply = std::make_unique<BBA> (proc.apvts, pid::apply, applyBtn);
    aSrcAuto = std::make_unique<BBA> (proc.apvts, pid::srcAuto, srcAutoBtn);
    aAutoTune = std::make_unique<BBA> (proc.apvts, pid::autoTune, autoTuneBtn);
    aLock = std::make_unique<BBA> (proc.apvts, pid::lock, lockBtn);
    aOct = std::make_unique<SA> (proc.apvts, pid::octave, octaveSlider);
    aFine = std::make_unique<SA> (proc.apvts, pid::fine, fineSlider);

    resetBtn.onClick = [this] { proc.resetAnalysis(); tempFile = juce::File(); dragLabel.setFile ({}); lblStatus.setText ({}, juce::dontSendNotification); };
    exportBtn.onClick = [this] { doExport(); };
    addAndMakeVisible (dragLabel);

    startTimerHz (30);
    updateEnabledStates();
}

KeyShiftEditor::~KeyShiftEditor() { stopTimer(); }

void KeyShiftEditor::updateEnabledStates()
{
    const bool autoSrc = srcAutoBtn.getToggleState();
    srcRootBox.setEnabled (! autoSrc);
    srcModeBox.setEnabled (! autoSrc);
    lblSrc.setAlpha (autoSrc ? 0.4f : 1.f);
    autoTuneBtn.setEnabled (true);
}

void KeyShiftEditor::timerCallback()
{
    display.update();
    const int root = juce::jlimit (0, 11, (int) std::lround (proc.apvts.getRawParameterValue (pid::targetRoot)->load()));
    const bool minor = (int) std::lround (proc.apvts.getRawParameterValue (pid::targetMode)->load()) == 1;
    for (int i = 0; i < 12; ++i) rootButtons[i].setToggleState (i == root, juce::dontSendNotification);
    majorBtn.setToggleState (! minor, juce::dontSendNotification);
    minorBtn.setToggleState (minor, juce::dontSendNotification);
    updateEnabledStates();
}

void KeyShiftEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (20.f, juce::Font::bold));
    g.drawText ("KeyShift", 20, 10, 200, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.f));
    g.drawText (U8 ("Detecta la tonalidad del sample y c\xc3\xa1mbiala"), 120, 14, 400, 20, juce::Justification::centredLeft);
}

void KeyShiftEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);
    r.removeFromTop (44);
    display.setBounds (r.removeFromTop (150));
    r.removeFromTop (14);

    lblTarget.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);
    {
        auto row = r.removeFromTop (34);
        const int w = row.getWidth() / 12;
        for (int i = 0; i < 12; ++i) rootButtons[i].setBounds (row.removeFromLeft (w).reduced (2, 0));
    }
    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (34);
        majorBtn.setBounds (row.removeFromLeft (100).reduced (2, 0));
        minorBtn.setBounds (row.removeFromLeft (100).reduced (2, 0));
        row.removeFromLeft (16);
        applyBtn.setBounds (row.removeFromLeft (200));
        lockBtn.setBounds (row);
    }
    r.removeFromTop (14);

    auto line = [&r] (juce::Label& l, juce::Component& c)
    {
        auto row = r.removeFromTop (26);
        l.setBounds (row.removeFromLeft (190));
        c.setBounds (row);
        r.removeFromTop (6);
    };
    {
        auto row = r.removeFromTop (26);
        srcAutoBtn.setBounds (row.removeFromLeft (210));
        autoTuneBtn.setBounds (row);
        r.removeFromTop (6);
    }
    {
        auto row = r.removeFromTop (26);
        lblSrc.setBounds (row.removeFromLeft (190));
        srcRootBox.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (8);
        srcModeBox.setBounds (row.removeFromLeft (100));
        r.removeFromTop (6);
    }
    line (lblKind, kindBox);
    line (lblEngine, engineBox);
    line (lblOct, octaveSlider);
    line (lblFine, fineSlider);
    line (lblNotation, notationBox);
    r.removeFromTop (4);
    line (lblExport, exportBox);
    {
        auto row = r.removeFromTop (30);
        exportBtn.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (8);
        resetBtn.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (8);
        lblStatus.setBounds (row);
    }
    r.removeFromTop (8);
    dragLabel.setBounds (r.removeFromTop (30));
}

//==============================================================================
void KeyShiftEditor::doExport()
{
    if (exporting.load()) return;
    std::vector<std::vector<float>> audio;
    double rate = 44100.0;
    if (! proc.copyCaptured (audio, rate))
    {
        lblStatus.setText (U8 ("Primero reproduce un sample por esta pista."), juce::dontSendNotification);
        return;
    }
    const auto snap = proc.getSnapshot();
    if (! snap.hasCommitted)
    {
        lblStatus.setText (U8 ("El sample a\xc3\xban no se ha analizado."), juce::dontSendNotification);
        return;
    }
    const auto settings = proc.buildSettings();
    const double st = ks::computeShiftSemitones (snap.committed, settings);
    const bool vinyl = (int) std::lround (proc.apvts.getRawParameterValue (pid::exportMode)->load()) == 1;
    const int engineChoice = (int) std::lround (proc.apvts.getRawParameterValue (pid::engine)->load());
    bool useGran;
    if (engineChoice == 1) useGran = false;
    else if (engineChoice == 2) useGran = true;
    else useGran = settings.forceKind == 2 || (settings.forceKind == 0 && snap.committed.kind != ks::SoundKind::Tonal);

    const bool minor = settings.targetMinor;
    const juce::String keyTag = juce::String (ks::noteNamesEnglish()[settings.targetRoot]).upToFirstOccurrenceOf ("/", false, false) + (minor ? "m" : "");
    const juce::String name = "KeyShift_" + keyTag + ".wav";

    chooser = std::make_unique<juce::FileChooser> (U8 ("Guardar sample transportado"),
                  juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, audio, rate, st, vinyl, useGran] (const juce::FileChooser& fc) mutable
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (! file.hasFileExtension ("wav")) file = file.withFileExtension ("wav");
            exporting = true;
            lblStatus.setText (U8 ("Procesando\xe2\x80\xa6"), juce::dontSendNotification);
            juce::Component::SafePointer<KeyShiftEditor> safe (this);
            std::thread ([safe, audio = std::move (audio), rate, st, vinyl, useGran, file]() mutable
            {
                const double ratio = ks::semitonesToRatio (st);
                std::vector<std::vector<float>> out;
                for (auto& ch : audio)
                {
                    if (std::fabs (st) < 0.005) out.push_back (ch);
                    else out.push_back (vinyl ? ks::renderVinyl (ch, ratio) : ks::renderKeepDuration (ch, ratio, useGran));
                }
                size_t len = out.empty() ? 0 : out[0].size();
                for (auto& c : out) len = std::min (len, c.size());
                float peak = 0.f;
                for (auto& c : out) for (size_t i = 0; i < len; ++i) peak = std::max (peak, std::fabs (c[i]));
                const float g = peak > 0.98f ? 0.98f / peak : 1.f;
                juce::String msg; bool ok = false;
                if (len > 0)
                {
                    juce::WavAudioFormat wav;
                    file.deleteFile();
                    std::unique_ptr<juce::FileOutputStream> fs (file.createOutputStream());
                    if (fs != nullptr)
                    {
                        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fs.get(), rate, (unsigned) out.size(), 24, {}, 0));
                        if (w != nullptr)
                        {
                            fs.release();
                            juce::AudioBuffer<float> b ((int) out.size(), (int) len);
                            for (size_t c = 0; c < out.size(); ++c)
                                for (size_t i = 0; i < len; ++i) b.setSample ((int) c, (int) i, out[c][i] * g);
                            ok = w->writeFromAudioSampleBuffer (b, 0, (int) len);
                        }
                    }
                }
                msg = ok ? juce::String::fromUTF8 ("Guardado: ") + file.getFileName() : juce::String::fromUTF8 ("No se pudo guardar el archivo.");
                juce::MessageManager::callAsync ([safe, ok, file, msg]
                {
                    if (safe == nullptr) return;
                    safe->exporting = false;
                    safe->lblStatus.setText (msg, juce::dontSendNotification);
                    if (ok) safe->dragLabel.setFile (file);
                });
            }).detach();
        });
}
