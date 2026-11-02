#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }

//==============================================================================
AnalysisThread::AnalysisThread() : juce::Thread ("KeyShift analysis"), fifoBuf ((size_t) (1 << 17), 0.f) {}
AnalysisThread::~AnalysisThread() { stopThread (3000); }

void AnalysisThread::prepare (double sampleRate)
{
    stopThread (3000);
    analyzer.prepare (sampleRate);
    fifo.reset();
    {
        const juce::SpinLock::ScopedLockType l (snapLock);
        snap = Snapshot();
    }
    startThread (juce::Thread::Priority::low);
}

void AnalysisThread::push (const float* mono, int num)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (num, s1, n1, s2, n2);
    if (n1 > 0) std::memcpy (fifoBuf.data() + s1, mono, (size_t) n1 * sizeof (float));
    if (n2 > 0) std::memcpy (fifoBuf.data() + s2, mono + n1, (size_t) n2 * sizeof (float));
    fifo.finishedWrite (n1 + n2);
}

bool AnalysisThread::tryGetSnapshot (Snapshot& out) const
{
    const juce::SpinLock::ScopedTryLockType l (snapLock);
    if (! l.isLocked()) return false;
    out = snap;
    return true;
}

AnalysisThread::Snapshot AnalysisThread::getSnapshot() const
{
    const juce::SpinLock::ScopedLockType l (snapLock);
    return snap;
}

void AnalysisThread::publish()
{
    const juce::SpinLock::ScopedLockType l (snapLock);
    snap.live = analyzer.live();
    snap.committed = analyzer.committed();
    snap.hasCommitted = analyzer.hasCommitted();
    snap.eventActive = analyzer.eventActive();
    snap.currentEventCommitted = analyzer.currentEventCommitted();
}

void AnalysisThread::run()
{
    std::vector<float> tmp (4096);
    while (! threadShouldExit())
    {
        if (resetFlag.exchange (false))
        {
            analyzer.resetAll();
            publish();
        }
        analyzer.setLocked (lockFlag.load());
        const int ready = fifo.getNumReady();
        if (ready <= 0) { wait (10); continue; }
        const int n = juce::jmin (ready, (int) tmp.size());
        int s1, n1, s2, n2;
        fifo.prepareToRead (n, s1, n1, s2, n2);
        if (n1 > 0) std::memcpy (tmp.data(), fifoBuf.data() + s1, (size_t) n1 * sizeof (float));
        if (n2 > 0) std::memcpy (tmp.data() + n1, fifoBuf.data() + s2, (size_t) n2 * sizeof (float));
        fifo.finishedRead (n1 + n2);
        analyzer.process (tmp.data(), n1 + n2);
        publish();
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout KeyShiftProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    const StringArray roots { "C / Do", "C# / Do#", "D / Re", "D# / Re#", "E / Mi", "F / Fa",
                              "F# / Fa#", "G / Sol", "G# / Sol#", "A / La", "A# / La#", "B / Si" };
    const StringArray modes { "Mayor", "Menor" };

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid::apply, 1 }, "Aplicar cambio", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::targetRoot, 1 }, "Tonalidad destino", roots, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::targetMode, 1 }, "Modo destino", modes, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid::srcAuto, 1 }, "Deteccion automatica", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::srcRoot, 1 }, "Tonalidad origen manual", roots, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::srcMode, 1 }, "Modo origen manual", modes, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::kind, 1 }, "Tipo de sonido",
                 StringArray { "Auto", "Tonal (usar tonalidad)", U8 ("Percusi\xc3\xb3n / una nota") }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::engine, 1 }, "Motor",
                 StringArray { "Auto", "Tonal (vocoder)", "Percusivo (granular)" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid::autoTune, 1 }, "Autoafinar", true));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { pid::octave, 1 }, "Octava", -2, 2, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { pid::fine, 1 }, "Ajuste fino (cents)",
                 NormalisableRange<float> (-100.f, 100.f, 0.1f), 0.f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid::lock, 1 }, "Bloquear analisis", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::exportMode, 1 }, "Exportar como",
                 StringArray { U8 ("Mantener duraci\xc3\xb3n"), "Estilo vinilo (tono y velocidad)" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::notation, 1 }, "Notacion",
                 StringArray { "Do Re Mi", "C D E" }, 0));
    return { p.begin(), p.end() };
}

//==============================================================================
KeyShiftProcessor::KeyShiftProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KeyShift", createLayout())
{
}

KeyShiftProcessor::~KeyShiftProcessor() = default;

bool KeyShiftProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void KeyShiftProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    setLatencySamples (kLatency);

    voc.assign (2, ks::PhaseVocoder());
    gran.assign (2, ks::GranularShifter());
    for (auto& v : voc) v.prepare (4096, 4);
    for (auto& g : gran) g.prepare (1536, kLatency);
    dryDelay.assign (2, std::vector<float> (8192, 0.f));
    dryPos = 0;
    monoTmp.assign (4096, 0.f);
    engineMix = 0.f; wetMix = 0.f;
    cachedSnap = AnalysisThread::Snapshot();

    {
        const juce::SpinLock::ScopedLockType l (captureLock);
        capMax = (int) (30.0 * sr);
        capBuf.assign (2, std::vector<float> ((size_t) capMax + 16, 0.f));
        preRoll.assign (2, std::vector<float> (4096, 0.f));
        prePos = 0; capLen = 0; capSilence = 0; capActive = false;
    }
    analysis.prepare (sr);
}

ks::ShiftSettings KeyShiftProcessor::buildSettings() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ks::ShiftSettings s;
    s.targetRoot = juce::jlimit (0, 11, (int) std::lround (get (pid::targetRoot)));
    s.targetMinor = (int) std::lround (get (pid::targetMode)) == 1;
    s.autoSource = get (pid::srcAuto) > 0.5f;
    s.srcRoot = juce::jlimit (0, 11, (int) std::lround (get (pid::srcRoot)));
    s.srcMinor = (int) std::lround (get (pid::srcMode)) == 1;
    s.forceKind = juce::jlimit (0, 2, (int) std::lround (get (pid::kind)));
    s.autoTune = get (pid::autoTune) > 0.5f;
    s.octaves = (int) std::lround (get (pid::octave));
    s.fineCents = (double) get (pid::fine);
    return s;
}

void KeyShiftProcessor::captureBlock (const juce::AudioBuffer<float>& in, int numCh, float blockRms)
{
    const int n = in.getNumSamples();
    const juce::SpinLock::ScopedTryLockType lock (captureLock);
    if (lock.isLocked() && capMax > 0 && preRoll.size() == 2 && capBuf.size() == 2)
    {
        const int preSize = (int) preRoll[0].size();
        if (blockRms > 0.004f)
        {
            if (! capActive)
            {
                capActive = true; capLen = 0; capChannels = numCh;
                for (int k = 0; k < preSize; ++k)
                {
                    const int idx = (prePos + k) % preSize;
                    for (int c = 0; c < 2; ++c) capBuf[(size_t) c][(size_t) capLen] = preRoll[(size_t) c][(size_t) idx];
                    ++capLen;
                }
            }
            capSilence = 0;
        }
        else if (capActive)
        {
            capSilence += n;
            if (capSilence > (int) (0.35 * sr)) capActive = false;
        }
        if (capActive && capLen + n <= capMax)
        {
            for (int c = 0; c < 2; ++c)
                std::memcpy (capBuf[(size_t) c].data() + capLen, in.getReadPointer (juce::jmin (c, numCh - 1)), (size_t) n * sizeof (float));
            capLen += n;
        }
    }
    if (preRoll.size() == 2)
    {
        const int preSize = (int) preRoll[0].size();
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c) preRoll[(size_t) c][(size_t) prePos] = in.getReadPointer (juce::jmin (c, numCh - 1))[i];
            if (++prePos >= preSize) prePos = 0;
        }
    }
}

void KeyShiftProcessor::clearCapture()
{
    const juce::SpinLock::ScopedLockType l (captureLock);
    capLen = 0; capActive = false; capSilence = 0;
}

bool KeyShiftProcessor::copyCaptured (std::vector<std::vector<float>>& out, double& sampleRate) const
{
    const juce::SpinLock::ScopedLockType l (captureLock);
    if (capLen < 1024 || capBuf.size() != 2) return false;
    int last = capLen - 1;
    while (last > 0 && std::fabs (capBuf[0][(size_t) last]) < 0.0005f && std::fabs (capBuf[1][(size_t) last]) < 0.0005f) --last;
    const int len = juce::jmin (capLen, last + (int) (0.05 * sr));
    const int ch = capChannels >= 2 ? 2 : 1;
    out.assign ((size_t) ch, std::vector<float>());
    for (int c = 0; c < ch; ++c) out[(size_t) c].assign (capBuf[(size_t) c].begin(), capBuf[(size_t) c].begin() + len);
    sampleRate = sr;
    return true;
}

float KeyShiftProcessor::getCapturedSeconds() const
{
    const juce::SpinLock::ScopedLockType l (captureLock);
    return capLen > 4096 ? (float) ((capLen - 4096) / sr) : 0.f;
}

//==============================================================================
void KeyShiftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    if (n == 0 || numCh == 0 || voc.size() != 2) return;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    // 1) Análisis (hilo aparte) y captura del sample
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
        double e = 0.0;
        for (int pos = 0; pos < n;)
        {
            const int m = juce::jmin ((int) monoTmp.size(), n - pos);
            for (int i = 0; i < m; ++i)
            {
                const float v = 0.5f * (l[pos + i] + r[pos + i]);
                monoTmp[(size_t) i] = v;
                e += (double) v * v;
            }
            analysis.push (monoTmp.data(), m);
            pos += m;
        }
        captureBlock (buffer, numCh, (float) std::sqrt (e / n));
    }

    // 2) Qué cambio de tono hay que aplicar
    analysis.setLocked (apvts.getRawParameterValue (pid::lock)->load() > 0.5f);
    {
        AnalysisThread::Snapshot s;
        if (analysis.tryGetSnapshot (s)) cachedSnap = s;
    }
    const auto settings = buildSettings();
    const ks::KeyResult basis = cachedSnap.hasCommitted ? cachedSnap.committed : ks::KeyResult();
    bool note = false;
    const double st = ks::computeShiftSemitones (basis, settings, &note);
    computedShift = st;
    usedNote = note;

    const int engineChoice = (int) std::lround (apvts.getRawParameterValue (pid::engine)->load());
    bool useGran;
    if (engineChoice == 1) useGran = false;
    else if (engineChoice == 2) useGran = true;
    else useGran = basis.valid && (settings.forceKind == 2 || (settings.forceKind == 0 && basis.kind != ks::SoundKind::Tonal));
    usedGranular = useGran;

    const bool wantApply = apvts.getRawParameterValue (pid::apply)->load() > 0.5f && std::fabs (st) > 0.005;
    shiftActive = wantApply;
    const double ratio = ks::semitonesToRatio (st);
    for (int c = 0; c < 2; ++c) { voc[(size_t) c].setRatio (ratio); gran[(size_t) c].setRatio (ratio); }

    // 3) Proceso (los dos motores corren siempre para poder cambiar sin cortes)
    const float engTarget = useGran ? 1.f : 0.f;
    const float wetTarget = wantApply ? 1.f : 0.f;
    const float slew = 1.0f / (0.03f * (float) sr);
    const int mask = (int) dryDelay[0].size() - 1;
    for (int i = 0; i < n; ++i)
    {
        engineMix += juce::jlimit (-slew, slew, engTarget - engineMix);
        wetMix += juce::jlimit (-slew, slew, wetTarget - wetMix);
        for (int c = 0; c < numCh; ++c)
        {
            float* d = buffer.getWritePointer (c);
            const float x = d[i];
            auto& dl = dryDelay[(size_t) c];
            dl[(size_t) (dryPos & mask)] = x;
            const float dry = dl[(size_t) ((dryPos - kLatency) & mask)];
            const float yv = voc[(size_t) c].process (x);
            const float yg = gran[(size_t) c].process (x);
            const float sh = yv + engineMix * (yg - yv);
            d[i] = dry + wetMix * (sh - dry);
        }
        dryPos = (dryPos + 1) & mask;
    }
}

//==============================================================================
juce::AudioProcessorEditor* KeyShiftProcessor::createEditor() { return new KeyShiftEditor (*this); }

void KeyShiftProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KeyShiftProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KeyShiftProcessor(); }
