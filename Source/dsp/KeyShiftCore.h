// KeyShiftCore.h - núcleo de audio de KeyShift (sin dependencias de JUCE).
// Detección de tonalidad, nota dominante de percusión y cambio de tono.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ks
{
constexpr double kPi = 3.14159265358979323846;

inline int wrap12 (int x) { int r = x % 12; return r < 0 ? r + 12 : r; }

//==============================================================================
// FFT radix-2 compleja (doble precisión). La inversa incluye el factor 1/N.
class FFT
{
public:
    FFT() = default;
    explicit FFT (int sizePow2) { setSize (sizePow2); }

    void setSize (int sizePow2)
    {
        n = sizePow2;
        cosT.assign ((size_t) n / 2, 0.0);
        sinT.assign ((size_t) n / 2, 0.0);
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = std::cos (2.0 * kPi * i / n);
            sinT[(size_t) i] = std::sin (2.0 * kPi * i / n);
        }
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const { return n; }
    void forward (double* re, double* im) const { run (re, im, false); }
    void inverse (double* re, double* im) const { run (re, im, true); }

private:
    void run (double* re, double* im, bool inv) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const double wr = cosT[(size_t) (k * step)];
                    const double wi = inv ? sinT[(size_t) (k * step)] : -sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const double tr = re[b] * wr - im[b] * wi;
                    const double ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
        if (inv)
        {
            const double s = 1.0 / n;
            for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

    int n = 0;
    std::vector<double> cosT, sinT;
    std::vector<int> rev;
};

//==============================================================================
enum class SoundKind { Unknown = 0, Tonal, SinglePitch, Noise };

struct KeyResult
{
    bool valid = false;
    int tonic = 0;                 // 0 = C ... 11 = B
    bool minor = false;
    float confidence = 0.f;        // 0..1
    float correlation = 0.f;       // correlación del mejor candidato
    float tuningCents = 0.f;       // desafinación respecto a A=440 (-50..+50)
    std::array<float, 12> chroma {};

    bool hasDominant = false;
    float dominantHz = 0.f;
    float dominantMidi = 0.f;      // fraccionario (A4 = 69)

    SoundKind kind = SoundKind::Unknown;
    float centroidHz = 0.f;
    float flatness = 0.f;
    float singleShare = 0.f;       // cuota de la clase de nota más fuerte
    float soundSeconds = 0.f;
};

inline const char* const* noteNamesLatin()
{
    static const char* n[12] = { "Do", "Do#/Reb", "Re", "Re#/Mib", "Mi", "Fa",
                                 "Fa#/Solb", "Sol", "Sol#/Lab", "La", "La#/Sib", "Si" };
    return n;
}
inline const char* const* noteNamesEnglish()
{
    static const char* n[12] = { "C", "C#/Db", "D", "D#/Eb", "E", "F",
                                 "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    return n;
}
inline std::string noteWithOctave (double midi, bool latin)
{
    const int m = (int) std::lround (midi);
    static const char* eng[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* lat[12] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    const int pc = wrap12 (m);
    const int oct = (int) std::floor (m / 12.0) - 1;
    return std::string (latin ? lat[pc] : eng[pc]) + std::to_string (oct);
}

//==============================================================================
// Analizador: acumula audio mono, detecta "eventos" (un sample = un evento,
// separado por silencio) y calcula tonalidad + nota dominante.
class SampleAnalyzer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        N = 8192;
        const double target = std::log2 (0.37 * sr);
        N = 1 << (int) std::lround (std::max (13.0, std::min (15.0, target)));
        hop = N / 4;
        fft.setSize (N);
        window.assign ((size_t) N, 0.0);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N);
        ring.assign ((size_t) N, 0.f);
        re.assign ((size_t) N, 0.0);
        im.assign ((size_t) N, 0.0);
        mag.assign ((size_t) N / 2 + 1, 0.0);
        avgMag.assign ((size_t) N / 2 + 1, 0.0);
        holdSamples = (int) (0.35 * sr);
        resetAll();
    }

    void setLocked (bool l) { locked = l; }

    void resetAll()
    {
        liveRes = KeyResult();
        committedRes = KeyResult();
        hasCommittedRes = false;
        beginEvent();
        inEvent = false;
        std::fill (ring.begin(), ring.end(), 0.f);
        wp = 0; sinceHop = 0; silentSamples = holdSamples; lvlSum = 0; lvlCount = 0;
    }

    void process (const float* x, int num)
    {
        for (int i = 0; i < num; ++i)
        {
            const float s = x[i];
            ring[(size_t) wp] = s;
            if (++wp >= N) wp = 0;
            lvlSum += (double) s * s;
            if (++lvlCount >= kLevelBlock) { levelBlockDone(); }
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                if (inEvent) analyzeFrame();
            }
        }
    }

    const KeyResult& live() const { return liveRes; }
    const KeyResult& committed() const { return committedRes; }
    bool hasCommitted() const { return hasCommittedRes; }
    bool eventActive() const { return inEvent; }
    bool currentEventCommitted() const { return committedThisEvent; }

private:
    static constexpr int kLevelBlock = 256;

    void levelBlockDone()
    {
        const double rms = std::sqrt (lvlSum / lvlCount);
        lvlSum = 0; lvlCount = 0;
        if (rms > 0.004) // -48 dBFS
        {
            if (! inEvent || silentSamples >= holdSamples) beginEvent();
            inEvent = true;
            silentSamples = 0;
            soundSamples += kLevelBlock;
            if (! committedThisEvent && ! locked && framesAnalyzed >= 3 && soundSamples >= (long long) (0.5 * sr))
                commitLive();
        }
        else
        {
            silentSamples += kLevelBlock;
            if (inEvent && silentSamples >= holdSamples)
            {
                inEvent = false;
                if (! committedThisEvent && ! locked && framesAnalyzed >= 1 && liveRes.valid)
                    commitLive();
            }
        }
    }

    void commitLive()
    {
        committedRes = liveRes;
        hasCommittedRes = true;
        committedThisEvent = true;
    }

    void beginEvent()
    {
        std::fill (avgMag.begin(), avgMag.end(), 0.0);
        c36.fill (0.0);
        bass36.fill (0.0);
        cosAcc = sinAcc = 0.0;
        framesAnalyzed = 0;
        soundSamples = 0;
        committedThisEvent = false;
        liveRes = KeyResult();
    }

    void analyzeFrame()
    {
        // ventana + rms
        double e = 0;
        for (int i = 0; i < N; ++i)
        {
            const float s = ring[(size_t) ((wp + i) % N)];
            e += (double) s * s;
            re[(size_t) i] = s * window[(size_t) i];
            im[(size_t) i] = 0.0;
        }
        const double rms = std::sqrt (e / N);
        if (rms < 0.0008) return; // -62 dBFS
        fft.forward (re.data(), im.data());
        const int half = N / 2;
        for (int k = 0; k <= half; ++k)
            mag[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);

        for (int k = 0; k <= half; ++k) avgMag[(size_t) k] += mag[(size_t) k];

        // picos -> croma (36 bins)
        const double binHz = sr / N;
        const int kmin = std::max (2, (int) std::ceil (32.0 / binHz));
        const int kmax = std::min (half - 2, (int) std::floor (3500.0 / binHz));
        double maxBand = 1e-12;
        for (int k = kmin; k <= kmax; ++k) maxBand = std::max (maxBand, mag[(size_t) k]);
        const double fw = std::sqrt (rms);
        bool bassDone = false;
        for (int k = kmin + 1; k < kmax; ++k)
        {
            const double v = mag[(size_t) k];
            if (v > mag[(size_t) k - 1] && v >= mag[(size_t) k + 1] && v > 0.03 * maxBand)
            {
                const double a = std::log (mag[(size_t) k - 1] + 1e-12);
                const double b = std::log (v + 1e-12);
                const double c = std::log (mag[(size_t) k + 1] + 1e-12);
                const double den = a - 2 * b + c;
                double d = den != 0.0 ? 0.5 * (a - c) / den : 0.0;
                d = std::max (-0.5, std::min (0.5, d));
                const double f = (k + d) * binHz;
                const double m = 69.0 + 12.0 * std::log2 (f / 440.0);
                const double w = fw * std::sqrt (v / maxBand);
                int bin = (int) std::lround (3.0 * m) % 36; if (bin < 0) bin += 36;
                c36[(size_t) bin] += w;
                if (! bassDone && v > 0.25 * maxBand) { bass36[(size_t) bin] += fw; bassDone = true; }
                cosAcc += w * std::cos (2.0 * kPi * m);
                sinAcc += w * std::sin (2.0 * kPi * m);
            }
        }
        ++framesAnalyzed;
        computeResult (liveRes);
    }

    void computeResult (KeyResult& out) const
    {
        out = KeyResult();
        // rejilla de afinación: o = -1, 0, +1 (tercios de semitono)
        int bestO = 0; double bestE = -1;
        for (int o = -1; o <= 1; ++o)
        {
            double s = 0;
            for (int pc = 0; pc < 12; ++pc) s += c36[(size_t) (((3 * pc + o) % 36 + 36) % 36)];
            if (s > bestE + 1e-12) { bestE = s; bestO = o; }
        }
        std::array<double, 12> ch {};
        double tot = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            double s = 0;
            for (int j = -1; j <= 1; ++j) s += c36[(size_t) (((3 * pc + bestO + j) % 36 + 36) % 36)];
            ch[(size_t) pc] = s; tot += s;
        }
        if (tot <= 1e-12) return;
        for (int pc = 0; pc < 12; ++pc) out.chroma[(size_t) pc] = (float) (ch[(size_t) pc] / tot);
        out.tuningCents = (float) (std::atan2 (sinAcc, cosAcc) / (2.0 * kPi) * 100.0);
        out.singleShare = *std::max_element (out.chroma.begin(), out.chroma.end());

        // perfiles de tonalidad (Temperley)
        static const double maj[12] = { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 };
        static const double mnr[12] = { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 };
        double scores[24];
        double mc = 0; for (int i = 0; i < 12; ++i) mc += ch[(size_t) i] / tot; mc /= 12.0;
        for (int t = 0; t < 12; ++t)
            for (int mode = 0; mode < 2; ++mode)
            {
                const double* p = mode ? mnr : maj;
                double mp = 0; for (int i = 0; i < 12; ++i) mp += p[i]; mp /= 12.0;
                double sxy = 0, sxx = 0, syy = 0;
                for (int pc = 0; pc < 12; ++pc)
                {
                    const double x = ch[(size_t) pc] / tot - mc;
                    const double y = p[wrap12 (pc - t)] - mp;
                    sxy += x * y; sxx += x * x; syy += y * y;
                }
                scores[t * 2 + mode] = (sxx > 0 && syy > 0) ? sxy / std::sqrt (sxx * syy) : -1.0;
            }
        int best = 0;
        for (int i = 1; i < 24; ++i) if (scores[i] > scores[best]) best = i;
        double second = -2;
        for (int i = 0; i < 24; ++i) if (i != best && scores[i] > second) second = scores[i];
        out.tonic = best / 2; out.minor = (best % 2) == 1;
        out.correlation = (float) scores[best];
        // Un par de tonalidades relativas (p.ej. Do mayor / La menor) comparte las mismas notas.
        // Se elige el modo según qué tónica tiene más presencia en el croma.
        {
            const int relMaj = out.minor ? wrap12 (out.tonic + 3) : out.tonic;
            const int relMin = wrap12 (relMaj + 9);
            std::array<double, 12> bs {};
            double bt = 0;
            for (int pc = 0; pc < 12; ++pc)
            {
                double v = 0;
                for (int j = -1; j <= 1; ++j) v += bass36[(size_t) (((3 * pc + bestO + j) % 36 + 36) % 36)];
                bs[(size_t) pc] = v; bt += v;
            }
            bool pickMinor = ch[(size_t) relMin] > kMinorBias * ch[(size_t) relMaj];
            if (bt > 0)
            {
                const double diff = (bs[(size_t) relMin] - bs[(size_t) relMaj]) / bt;
                if (diff > 0.15) pickMinor = true;
                else if (diff < -0.15) pickMinor = false;
            }
            out.tonic = pickMinor ? relMin : relMaj;
            out.minor = pickMinor;
        }
        const double c1 = std::max (0.0, std::min (1.0, (scores[best] - 0.3) / 0.6));
        const double c2 = std::max (0.0, std::min (1.0, (scores[best] - second) / 0.12));
        out.confidence = (float) (0.6 * c1 + 0.4 * c2);

        // nota dominante, centroide, planitud
        const double binHz = sr / N; const int half = N / 2;
        const int dmin = std::max (2, (int) std::ceil (30.0 / binHz));
        const int dmax = std::min (half - 2, (int) std::floor (2000.0 / binHz));
        double maxV = 0; int maxK = -1;
        for (int k = dmin + 1; k < dmax; ++k)
        {
            const double v = avgMag[(size_t) k];
            if (v > avgMag[(size_t) k - 1] && v >= avgMag[(size_t) k + 1] && v > maxV) { maxV = v; maxK = k; }
        }
        if (maxK > 0)
        {
            auto refine = [&] (int k) {
                const double a = std::log (avgMag[(size_t) k - 1] + 1e-12), b = std::log (avgMag[(size_t) k] + 1e-12),
                             c = std::log (avgMag[(size_t) k + 1] + 1e-12);
                const double den = a - 2 * b + c;
                double d = den != 0.0 ? 0.5 * (a - c) / den : 0.0;
                return (k + std::max (-0.5, std::min (0.5, d))) * binHz;
            };
            double f = refine (maxK);
            // comprobación de subarmónico (octava inferior)
            const int ks = (int) std::lround (maxK / 2.0);
            if (ks - 3 > dmin)
            {
                double sv = 0; int sk = -1;
                for (int k = ks - 3; k <= ks + 3; ++k)
                    if (avgMag[(size_t) k] > avgMag[(size_t) k - 1] && avgMag[(size_t) k] >= avgMag[(size_t) k + 1] && avgMag[(size_t) k] > sv)
                    { sv = avgMag[(size_t) k]; sk = k; }
                if (sk > 0 && sv >= 0.55 * maxV) f = refine (sk);
            }
            out.hasDominant = true;
            out.dominantHz = (float) f;
            out.dominantMidi = (float) (69.0 + 12.0 * std::log2 (f / 440.0));
        }
        double sw = 0, swf = 0;
        const int cmin = std::max (1, (int) std::ceil (20.0 / binHz));
        const int cmax = std::min (half, (int) std::floor (16000.0 / binHz));
        for (int k = cmin; k <= cmax; ++k) { sw += avgMag[(size_t) k]; swf += avgMag[(size_t) k] * k * binHz; }
        out.centroidHz = sw > 0 ? (float) (swf / sw) : 0.f;
        const int fmin2 = std::max (1, (int) std::ceil (100.0 / binHz));
        const int fmax2 = std::min (half, (int) std::floor (8000.0 / binHz));
        double lg = 0, ar = 0; int cnt = 0;
        double peakMag = 1e-12; for (int k = fmin2; k <= fmax2; ++k) peakMag = std::max (peakMag, avgMag[(size_t) k]);
        for (int k = fmin2; k <= fmax2; ++k)
        {
            const double v = avgMag[(size_t) k] + 1e-6 * peakMag;
            lg += std::log (v); ar += v; ++cnt;
        }
        out.flatness = cnt > 0 ? (float) (std::exp (lg / cnt) / (ar / cnt)) : 0.f;

        if (out.flatness > kNoiseFlatness) out.kind = SoundKind::Noise;
        else if (out.singleShare > kSingleShare || out.centroidHz < kLowCentroid) out.kind = SoundKind::SinglePitch;
        else out.kind = SoundKind::Tonal;
        out.soundSeconds = (float) (soundSamples / sr);
        out.valid = true;
    }

public:
    // umbrales de clasificación (ajustados con señales de prueba)
    static constexpr float kNoiseFlatness = 0.30f;
    static constexpr float kSingleShare = 0.50f;
    static constexpr float kLowCentroid = 350.0f;
    static constexpr double kMinorBias = 1.0;

private:
    double sr = 44100.0;
    int N = 16384, hop = 4096;
    FFT fft;
    std::vector<double> window, re, im, mag, avgMag;
    std::vector<float> ring;
    int wp = 0, sinceHop = 0;
    double lvlSum = 0; int lvlCount = 0;
    int silentSamples = 0, holdSamples = 15435;
    bool inEvent = false, committedThisEvent = false, locked = false, hasCommittedRes = false;
    long long soundSamples = 0; int framesAnalyzed = 0;
    std::array<double, 36> c36 {}, bass36 {};
    double cosAcc = 0, sinAcc = 0;
    KeyResult liveRes, committedRes;
};

//==============================================================================
// Cálculo del desplazamiento en semitonos a partir del análisis y los ajustes.
struct ShiftSettings
{
    bool autoSource = true;
    int  srcRoot = 0; bool srcMinor = false;
    int  targetRoot = 0; bool targetMinor = false;
    int  forceKind = 0;          // 0 auto, 1 tonal, 2 percusión/nota única
    bool autoTune = true;
    int  octaves = 0;
    double fineCents = 0.0;
};

inline double computeShiftSemitones (const KeyResult& r, const ShiftSettings& s, bool* usedNotePath = nullptr)
{
    if (usedNotePath) *usedNotePath = false;
    if (! r.valid) return 0.0;

    bool useNote;
    if (s.forceKind == 1) useNote = false;
    else if (s.forceKind == 2) useNote = true;
    else
    {
        if (r.kind == SoundKind::Noise) return std::max (-24.0, std::min (24.0, 12.0 * s.octaves + s.fineCents / 100.0));
        useNote = (r.kind == SoundKind::SinglePitch);
    }

    double st = 0.0;
    if (useNote && r.hasDominant)
    {
        const long long dom = std::lround (r.dominantMidi);
        const int srcPc = s.autoSource ? wrap12 ((int) dom) : s.srcRoot;
        int d = wrap12 (s.targetRoot - srcPc);
        if (d > 5) d -= 12;
        st = d;
        if (s.autoTune && s.autoSource) st += (double) dom - (double) r.dominantMidi;
        if (usedNotePath) *usedNotePath = true;
    }
    else
    {
        const int srcT = s.autoSource ? r.tonic : s.srcRoot;
        const bool srcM = s.autoSource ? r.minor : s.srcMinor;
        const int srcRel = srcM ? wrap12 (srcT + 3) : srcT;
        const int tgtRel = s.targetMinor ? wrap12 (s.targetRoot + 3) : s.targetRoot;
        int d = wrap12 (tgtRel - srcRel);
        if (d > 5) d -= 12;
        st = d;
        if (s.autoTune) st -= r.tuningCents / 100.0;
    }
    st += 12.0 * s.octaves + s.fineCents / 100.0;
    return std::max (-24.0, std::min (24.0, st));
}

//==============================================================================
// Motor 1: vocoder de fase con bloqueo de fase por picos (Laroche-Dolson).
// Mantiene el volumen estable y el timbre mejor que el remapeo bin a bin.
class PhaseVocoder
{
public:
    void prepare (int frameSize = 4096, int overlap = 4)
    {
        N = frameSize; osamp = overlap; step = N / osamp; latency = N - step;
        fft.setSize (N);
        win.assign ((size_t) N, 0.0);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N);
        inFIFO.assign ((size_t) N, 0.f); outFIFO.assign ((size_t) N, 0.f); accum.assign ((size_t) N, 0.0);
        const size_t h = (size_t) N / 2 + 1;
        lastPhase.assign (h, 0.0);
        anaRe.assign (h, 0.0); anaIm.assign (h, 0.0); anaMag.assign (h, 0.0); anaPh.assign (h, 0.0); anaFreq.assign (h, 0.0);
        synRe.assign (h, 0.0); synIm.assign (h, 0.0);
        peaks.assign (h, 0); prevBin.assign (h, 0); prevPh.assign (h, 0.0); newBin.assign (h, 0); newPh.assign (h, 0.0);
        re.assign ((size_t) N, 0.0); im.assign ((size_t) N, 0.0);
        reset();
    }
    void reset()
    {
        std::fill (inFIFO.begin(), inFIFO.end(), 0.f); std::fill (outFIFO.begin(), outFIFO.end(), 0.f);
        std::fill (accum.begin(), accum.end(), 0.0);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0);
        prevN = 0;
        rover = latency;
    }
    void setRatio (double r) { ratio = std::max (0.125, std::min (8.0, r)); }
    int getLatency() const { return latency; }

    float process (float x)
    {
        inFIFO[(size_t) rover] = x;
        const float out = outFIFO[(size_t) (rover - latency)];
        if (++rover >= N) { rover = latency; doFrame(); }
        return out;
    }

private:
    static double wrapPi (double x) { return x - 2.0 * kPi * std::floor (x / (2.0 * kPi) + 0.5); }

    void doFrame()
    {
        const int half = N / 2;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = inFIFO[(size_t) i] * win[(size_t) i]; im[(size_t) i] = 0.0; }
        fft.forward (re.data(), im.data());
        const double expct = 2.0 * kPi * step / N;
        double maxMag = 0.0;
        for (int k = 0; k <= half; ++k)
        {
            const double a = re[(size_t) k], b = im[(size_t) k];
            anaRe[(size_t) k] = a; anaIm[(size_t) k] = b;
            const double mg = std::sqrt (a * a + b * b);
            const double ph = std::atan2 (b, a);
            anaMag[(size_t) k] = mg; anaPh[(size_t) k] = ph;
            maxMag = std::max (maxMag, mg);
            const double dphi = wrapPi (ph - lastPhase[(size_t) k] - k * expct);
            lastPhase[(size_t) k] = ph;
            anaFreq[(size_t) k] = k + dphi * osamp / (2.0 * kPi);   // frecuencia instantánea en bins
        }
        std::fill (synRe.begin(), synRe.end(), 0.0);
        std::fill (synIm.begin(), synIm.end(), 0.0);

        int nPeaks = 0;
        const double thr = maxMag * 1e-4 + 1e-12;
        for (int k = 2; k <= half - 2; ++k)
        {
            const double v = anaMag[(size_t) k];
            if (v > thr && v > anaMag[(size_t) k - 1] && v >= anaMag[(size_t) k + 1]
                && v > anaMag[(size_t) k - 2] && v >= anaMag[(size_t) k + 2])
                peaks[(size_t) nPeaks++] = k;
        }

        int j = 0;
        for (int i = 0; i < nPeaks; ++i)
        {
            const int p = peaks[(size_t) i];
            const int lo = i == 0 ? 0 : (peaks[(size_t) i - 1] + p) / 2 + 1;
            const int hi = i == nPeaks - 1 ? half : (p + peaks[(size_t) i + 1]) / 2;
            const double fp = ratio * anaFreq[(size_t) p];
            const int shift = (int) std::floor (fp + 0.5) - p;

            while (j + 1 < prevN && std::abs (prevBin[(size_t) j + 1] - p) < std::abs (prevBin[(size_t) j] - p)) ++j;
            double synPh;
            if (prevN > 0 && std::abs (prevBin[(size_t) j] - p) <= 3)
                synPh = prevPh[(size_t) j] + expct * fp;
            else
                synPh = anaPh[(size_t) p];
            synPh = wrapPi (synPh);
            newBin[(size_t) i] = p; newPh[(size_t) i] = synPh;

            const double th = synPh - anaPh[(size_t) p];
            const double c = std::cos (th), s = std::sin (th);
            for (int k = lo; k <= hi; ++k)
            {
                const int d = k + shift;
                if (d < 0 || d > half) continue;
                synRe[(size_t) d] += anaRe[(size_t) k] * c - anaIm[(size_t) k] * s;
                synIm[(size_t) d] += anaRe[(size_t) k] * s + anaIm[(size_t) k] * c;
            }
        }
        prevN = nPeaks;
        std::swap (prevBin, newBin);
        std::swap (prevPh, newPh);

        for (int k = 0; k <= half; ++k) { re[(size_t) k] = synRe[(size_t) k]; im[(size_t) k] = synIm[(size_t) k]; }
        for (int k = half + 1; k < N; ++k) { re[(size_t) k] = 0.0; im[(size_t) k] = 0.0; }
        fft.inverse (re.data(), im.data());
        const double scale = kGain * 8.0 / osamp;
        for (int i = 0; i < N; ++i) accum[(size_t) i] += scale * win[(size_t) i] * re[(size_t) i];
        for (int i = 0; i < step; ++i) outFIFO[(size_t) i] = (float) accum[(size_t) i];
        for (int i = 0; i < N - step; ++i) accum[(size_t) i] = accum[(size_t) (i + step)];
        for (int i = N - step; i < N; ++i) accum[(size_t) i] = 0.0;
        for (int i = 0; i < latency; ++i) inFIFO[(size_t) i] = inFIFO[(size_t) (i + step)];
    }

public:
    static constexpr double kGain = 1.0 / 1.5;

private:
    int N = 4096, osamp = 4, step = 1024, latency = 3072, rover = 3072, prevN = 0;
    double ratio = 1.0;
    FFT fft;
    std::vector<double> win, accum, lastPhase, anaRe, anaIm, anaMag, anaPh, anaFreq, synRe, synIm, prevPh, newPh, re, im;
    std::vector<int> peaks, prevBin, newBin;
    std::vector<float> inFIFO, outFIFO;
};

//==============================================================================
// Motor 2: granular con alineación de forma de onda (estilo WSOLA).
// Dos "granos" con fundido cruzado; cada vez que un grano se reinicia busca
// el punto de lectura que mejor encaja con el otro, evitando el batido y la
// desafinación típicos de los pitch shifters granulares simples.
// Conserva mucho mejor los transitorios (kicks, cajas, 808) que el vocoder.
class GranularShifter
{
public:
    void prepare (int grainSamples = 1536, int latencySamples = 3072, int searchRange = 1024, int corrLen = 256)
    {
        G = grainSamples; targetLatency = latencySamples; S = searchRange; L = corrLen;
        M = 16384; mask = M - 1;
        buf.assign ((size_t) M, 0.f);
        reset();
    }
    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        wp = 0;
        phase[0] = 0.0; phase[1] = 0.5;
        const double d = nominalDelay();
        r[0] = r[1] = (double) wp - d;
    }
    void setRatio (double rr) { ratio = std::max (0.25, std::min (4.0, rr)); }
    int getLatency() const { return targetLatency; }

    float process (float x)
    {
        buf[(size_t) (wp & mask)] = x;
        float y = 0.f;
        for (int i = 0; i < 2; ++i)
        {
            const double w = std::sin (kPi * phase[i]);
            y += (float) (w * w) * readInterp (r[i]);
        }
        for (int i = 0; i < 2; ++i)
        {
            r[i] += ratio;
            phase[i] += 1.0 / G;
            if (phase[i] >= 1.0) { phase[i] -= 1.0; realign (i); }
        }
        ++wp;
        return y;
    }

private:
    double nominalDelay() const { return (double) targetLatency + 0.5 * G * (ratio - 1.0); }

    float sampleAt (long long i) const { return buf[(size_t) (i & mask)]; }

    float readInterp (double pos)
    {
        // el índice más reciente escrito es wp; garantizar lectura segura
        const double maxPos = (double) wp - 3.0;
        const double minPos = (double) wp - (double) (M - 8);
        if (pos > maxPos) pos = maxPos;
        if (pos < minPos) pos = minPos;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const long long i1 = (long long) fl;
        const float y0 = sampleAt (i1 - 1), y1 = sampleAt (i1), y2 = sampleAt (i1 + 1), y3 = sampleAt (i1 + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void realign (int i)
    {
        const int j = 1 - i;
        const double dN = nominalDelay();
        // posición nominal de inicio (entera) del grano que se reinicia (se escribirá la muestra wp+1 a continuación)
        const long long base = (long long) std::floor ((double) (wp + 1) - dN);
        long long bestD = 0;
        // referencia: lo que va a producir el otro grano
        const long long refStart = (long long) std::floor (r[j]);
        long long Leff = std::min<long long> (L, (long long) (wp + 1) - refStart - 4);
        const long long minDelay = 4 + (long long) std::ceil (G * std::max (0.0, ratio - 1.0));
        long long dLo = std::max<long long> (-S, minDelay - (long long) std::floor (dN));
        long long dHi = S;
        dHi = std::min<long long> (dHi, (long long) std::floor (dN) - Leff - 6);
        if (Leff >= 32 && dLo <= dHi)
        {
            float refE = 0.f;
            for (long long n = 0; n < Leff; ++n) { const float v = sampleAt (refStart + n); refE += v * v; }
            if (refE > 1e-10f)
            {
                auto score = [&] (long long dl) {
                    const long long c0 = base + dl;
                    float cr = 0.f, ce = 1e-9f;
                    for (long long n = 0; n < Leff; ++n)
                    {
                        const float a = sampleAt (refStart + n), b = sampleAt (c0 + n);
                        cr += a * b; ce += b * b;
                    }
                    return cr / std::sqrt (ce);
                };
                long long z = std::max (dLo, std::min (dHi, 0LL));
                float bestS = score (z); bestD = z;
                for (long long dl = dLo; dl <= dHi; ++dl)
                {
                    const float sc = score (dl);
                    if (sc > bestS * 1.0005f + 1e-9f) { bestS = sc; bestD = dl; }
                }
            }
        }
        r[i] = (double) (base + bestD);
    }

    int G = 1536, targetLatency = 3072, S = 1024, L = 256, M = 16384, mask = 16383;
    long long wp = 0;
    double phase[2] = { 0.0, 0.5 }, r[2] = { 0.0, 0.0 };
    double ratio = 1.0;
    std::vector<float> buf;
};

//==============================================================================
// Renderizado offline (exportación).
inline double semitonesToRatio (double st) { return std::pow (2.0, st / 12.0); }

// Estilo vinilo: remuestreo con sinc ventaneada. Cambia tono Y duración.
inline std::vector<float> renderVinyl (const std::vector<float>& in, double ratio, int halfTaps = 24)
{
    if (in.empty()) return {};
    const double fc = std::min (1.0, 1.0 / ratio);
    const int H = (int) std::ceil (halfTaps / fc);
    const size_t outLen = (size_t) std::floor ((double) (in.size() - 1) / ratio) + 1;
    std::vector<float> out (outLen, 0.f);
    const long long n = (long long) in.size();
    for (size_t o = 0; o < outLen; ++o)
    {
        const double p = (double) o * ratio;
        const long long i0 = (long long) std::floor (p);
        double acc = 0.0, wsum = 0.0;
        for (long long j = i0 - H + 1; j <= i0 + H; ++j)
        {
            const double x = (double) j - p;
            const double t = x / H;
            if (std::fabs (t) >= 1.0) continue;
            const double arg = kPi * fc * x;
            const double sinc = std::fabs (arg) < 1e-9 ? 1.0 : std::sin (arg) / arg;
            const double w = 0.42 + 0.5 * std::cos (kPi * t) + 0.08 * std::cos (2.0 * kPi * t);
            const double k = fc * sinc * w;
            wsum += k;
            if (j >= 0 && j < n) acc += k * in[(size_t) j];
        }
        out[o] = (float) (wsum > 1e-9 ? acc / wsum : 0.0);
    }
    return out;
}

// Conserva la duración. useGranular=false -> vocoder de fase de alta calidad.
inline std::vector<float> renderKeepDuration (const std::vector<float>& in, double ratio, bool useGranular)
{
    if (in.empty()) return {};
    std::vector<float> out (in.size(), 0.f);
    if (useGranular)
    {
        GranularShifter g; g.prepare (1536, 3072); g.setRatio (ratio);
        const int lat = g.getLatency();
        for (size_t i = 0; i < in.size() + (size_t) lat; ++i)
        {
            const float y = g.process (i < in.size() ? in[i] : 0.f);
            if ((long long) i - lat >= 0 && (size_t) ((long long) i - lat) < out.size()) out[(size_t) ((long long) i - lat)] = y;
        }
    }
    else
    {
        PhaseVocoder v; v.prepare (4096, 8); v.setRatio (ratio);
        const int lat = v.getLatency();
        for (size_t i = 0; i < in.size() + (size_t) lat + 4096; ++i)
        {
            const float y = v.process (i < in.size() ? in[i] : 0.f);
            const long long o = (long long) i - lat;
            if (o >= 0 && (size_t) o < out.size()) out[(size_t) o] = y;
        }
    }
    return out;
}
} // namespace ks
