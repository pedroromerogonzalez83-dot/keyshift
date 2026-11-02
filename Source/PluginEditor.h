#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

//==============================================================================
// Pantallita principal: tonalidad detectada, confianza, afinación, nota dominante.
class KeyDisplay : public juce::Component
{
public:
    explicit KeyDisplay (KeyShiftProcessor& p) : proc (p) {}
    void update();
    void paint (juce::Graphics&) override;

private:
    KeyShiftProcessor& proc;
    AnalysisThread::Snapshot snap;
    double shift = 0.0;
    bool note = false, gran = false, active = false;
    bool latin = true;
};

//==============================================================================
// Etiqueta que se puede arrastrar hacia el DAW con el WAV exportado.
class DragWavLabel : public juce::Component
{
public:
    void setFile (const juce::File& f) { file = f; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::File file;
};

//==============================================================================
class KeyShiftEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KeyShiftEditor (KeyShiftProcessor&);
    ~KeyShiftEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateEnabledStates();
    void doExport();

    KeyShiftProcessor& proc;
    KeyDisplay display { proc };
    DragWavLabel dragLabel;

    juce::TextButton rootButtons[12];
    juce::TextButton majorBtn { "Mayor" }, minorBtn { "Menor" };
    juce::ToggleButton applyBtn { "APLICAR CAMBIO" };
    juce::TextButton resetBtn { "Reiniciar analisis" }, exportBtn { "Exportar WAV" };
    juce::ToggleButton srcAutoBtn { "Deteccion automatica" }, autoTuneBtn { "Autoafinar" }, lockBtn { "Bloquear analisis" };
    juce::ComboBox srcRootBox, srcModeBox, kindBox, engineBox, exportBox, notationBox;
    juce::Slider octaveSlider, fineSlider;
    juce::Label lblSrc, lblKind, lblEngine, lblOct, lblFine, lblExport, lblNotation, lblTarget, lblStatus;

    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BBA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<CBA> aSrcRoot, aSrcMode, aKind, aEngine, aExport, aNotation;
    std::unique_ptr<BBA> aApply, aSrcAuto, aAutoTune, aLock;
    std::unique_ptr<SA> aOct, aFine;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File tempFile;
    std::atomic<bool> exporting { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyShiftEditor)
};
