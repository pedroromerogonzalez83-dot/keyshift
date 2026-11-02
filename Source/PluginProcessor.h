#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "dsp/KeyShiftCore.h"

namespace pid
{
inline constexpr const char* apply      = "apply";
inline constexpr const char* targetRoot = "targetRoot";
inline constexpr const char* targetMode = "targetMode";
inline constexpr const char* srcAuto    = "srcAuto";
inline constexpr const char* srcRoot    = "srcRoot";
inline constexpr const char* srcMode    = "srcMode";
inline constexpr const char* kind       = "kind";
inline constexpr const char* engine     = "engine";
inline constexpr const char* autoTune   = "autoTune";
inline constexpr const char* octave     = "octave";
inline constexpr const char* fine       = "fine";
inline constexpr const char* lock       = "lock";
inline constexpr const char* exportMode = "exportMode";
inline constexpr const char* notation   = "notation";
}

//==============================================================================
// Hilo de análisis: recibe audio mono desde el hilo de audio y ejecuta el
// analizador de tonalidad fuera del hilo de audio.
class AnalysisThread : public juce::Thread
{
public:
    AnalysisThread();
    ~AnalysisThread() override;

    struct Snapshot
    {
        ks::KeyResult live, committed;
        bool hasCommitted = false;
        bool eventActive = false;
        bool currentEventCommitted = false;
    };

    void prepare (double sampleRate);
    void push (const float* mono, int num);          // hilo de audio
    void requestReset() { resetFlag = true; }
    void setLocked (bool l) { lockFlag = l; }
    bool tryGetSnapshot (Snapshot& out) const;       // hilo de audio
    Snapshot getSnapshot() const;                    // interfaz

    void run() override;

private:
    void publish();

    ks::SampleAnalyzer analyzer;
    juce::AbstractFifo fifo { 1 << 17 };
    std::vector<float> fifoBuf;
    std::atomic<bool> resetFlag { false }, lockFlag { false };
    mutable juce::SpinLock snapLock;
    Snapshot snap;
};

//==============================================================================
class KeyShiftProcessor : public juce::AudioProcessor
{
public:
    KeyShiftProcessor();
    ~KeyShiftProcessor() override;

    static constexpr int kLatency = 3072;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KeyShift"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    AnalysisThread::Snapshot getSnapshot() const { return analysis.getSnapshot(); }
    void resetAnalysis() { analysis.requestReset(); clearCapture(); }

    double getComputedShift() const { return computedShift.load(); }
    bool lastUsedNotePath() const { return usedNote.load(); }
    bool lastUsedGranular() const { return usedGranular.load(); }
    bool isShiftActive() const { return shiftActive.load(); }

    // Audio capturado del último sample (para exportar).
    bool copyCaptured (std::vector<std::vector<float>>& out, double& sampleRate) const;
    float getCapturedSeconds() const;
    ks::ShiftSettings buildSettings() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void clearCapture();
    void captureBlock (const juce::AudioBuffer<float>& in, int numCh, float blockRms);

    AnalysisThread analysis;
    AnalysisThread::Snapshot cachedSnap;

    double sr = 44100.0;
    std::vector<ks::PhaseVocoder> voc;
    std::vector<ks::GranularShifter> gran;
    std::vector<std::vector<float>> dryDelay;
    int dryPos = 0;
    std::vector<float> monoTmp;
    float engineMix = 0.f, wetMix = 0.f;

    std::atomic<double> computedShift { 0.0 };
    std::atomic<bool> usedNote { false }, usedGranular { false }, shiftActive { false };

    // captura
    mutable juce::SpinLock captureLock;
    std::vector<std::vector<float>> capBuf;
    std::vector<std::vector<float>> preRoll;
    int prePos = 0, capLen = 0, capSilence = 0, capChannels = 2, capMax = 0;
    bool capActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyShiftProcessor)
};
