#include "../Source/dsp/KeyShiftCore.h"
#include <cstdio>
#include <random>
using namespace ks;

static const double SR = 44100.0;
static const char* nn[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

static void addNote (std::vector<float>& b, double midi, double start, double dur, double amp, double decay = 0.0, int harm = 6)
{
    const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    const size_t s0 = (size_t) (start * SR), n = (size_t) (dur * SR);
    if (b.size() < s0 + n) b.resize (s0 + n, 0.f);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / SR;
        double env = decay > 0 ? std::exp (-t * decay) : 1.0;
        env *= std::min (1.0, t / 0.01) * std::min (1.0, (dur - t) / 0.02);
        double v = 0;
        for (int h = 1; h <= harm; ++h) if (f * h < SR / 2.2) v += std::sin (2 * kPi * f * h * t) / h;
        b[s0 + i] += (float) (amp * env * v);
    }
}

static void runAnalyzer (SampleAnalyzer& a, const std::vector<float>& sig, double tailSilence = 1.0)
{
    std::vector<float> s = sig;
    s.resize (s.size() + (size_t) (tailSilence * SR), 0.f);
    for (size_t i = 0; i < s.size(); i += 512)
        a.process (s.data() + i, (int) std::min<size_t> (512, s.size() - i));
}

static void show (const char* name, const SampleAnalyzer& a)
{
    const KeyResult& r = a.committed();
    std::printf ("%-26s valid=%d key=%s %s conf=%.2f corr=%.2f tune=%+.0fc | dom=%s (%.1f Hz) kind=%d share=%.2f flat=%.3f cent=%.0f\n",
                 name, r.valid, nn[r.tonic], r.minor ? "min" : "maj", r.confidence, r.correlation, r.tuningCents,
                 r.hasDominant ? noteWithOctave (r.dominantMidi, false).c_str() : "-", r.dominantHz, (int) r.kind, r.singleShare, r.flatness, r.centroidHz);
}

static double peakFreq (const std::vector<float>& x, size_t from, size_t n)
{
    FFT f ((int) n); std::vector<double> re (n), im (n, 0.0);
    for (size_t i = 0; i < n; ++i) re[i] = x[from + i] * (0.5 - 0.5 * std::cos (2 * kPi * i / n));
    f.forward (re.data(), im.data());
    size_t best = 1; double bv = 0;
    for (size_t k = 1; k < n / 2 - 1; ++k) { double m = std::hypot (re[k], im[k]); if (m > bv) { bv = m; best = k; } }
    const double a = std::log (std::hypot (re[best-1], im[best-1])), b = std::log (bv), c = std::log (std::hypot (re[best+1], im[best+1]));
    const double d = 0.5 * (a - c) / (a - 2 * b + c);
    return (best + d) * SR / n;
}
static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{ double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (b - a)); }

int main()
{
    std::mt19937 rng (1); std::normal_distribution<double> nd (0.0, 1.0);
    // --- tonalidad ---
    struct Chord { const char* name; std::vector<int> notes; };
    std::vector<Chord> chords = {
        { "C major triad", { 60, 64, 67 } }, { "A minor triad", { 57, 60, 64 } },
        { "F# minor triad", { 54, 57, 61 } }, { "D# major triad", { 63, 67, 70 } },
        { "Am7 (A C E G)", { 57, 60, 64, 67 } } };
    for (auto& c : chords) { std::vector<float> b; for (int m : c.notes) addNote (b, m, 0, 2.0, 0.1); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show (c.name, a); }
    {   // melodía en La menor (progresión) 
        std::vector<float> b; int seq[] = { 57, 60, 64, 67, 65, 64, 62, 60, 59, 57, 64, 60, 57 };
        double t = 0; for (int m : seq) { addNote (b, m, t, 0.35, 0.2, 5.0); t += 0.3; }
        SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("A minor melody", a);
    }
    {   // melodía en Re mayor
        std::vector<float> b; int seq[] = { 62, 66, 69, 71, 69, 66, 64, 62, 61, 62, 66, 69, 74, 62 };
        double t = 0; for (int m : seq) { addNote (b, m, t, 0.35, 0.2, 5.0); t += 0.3; }
        SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("D major melody", a);
    }
    {   // acorde desafinado +25 cents
        std::vector<float> b; for (int m : { 60, 64, 67 }) addNote (b, m + 0.25, 0, 2.0, 0.1);
        SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("C major +25c", a);
    }
    // --- percusión ---
    auto kick = [&] (double f0, double f1, double len) {
        std::vector<float> b ((size_t) (len * SR)); double ph = 0;
        for (size_t i = 0; i < b.size(); ++i) { double t = i / SR; double f = f1 + (f0 - f1) * std::exp (-t * 40); ph += 2 * kPi * f / SR; b[i] = (float) (0.9 * std::sin (ph) * std::exp (-t * 7)); }
        return b; };
    { auto b = kick (140, 52, 0.5); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("kick 52Hz (G#1 ~51.9)", a); }
    { auto b = kick (120, 41.2, 0.7); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("kick 41.2Hz (E1)", a); }
    { std::vector<float> b; addNote (b, 33, 0, 1.5, 0.5, 1.5, 3); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("808 A1 (midi33)", a); }
    { std::vector<float> b; addNote (b, 43, 0, 1.5, 0.5, 1.5, 4); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("bass G2 (midi43)", a); }
    { // snare: 190 Hz + ruido
        std::vector<float> b ((size_t) (0.3 * SR)); for (size_t i = 0; i < b.size(); ++i) { double t = i / SR; b[i] = (float) (0.4 * std::sin (2 * kPi * 190 * t) * std::exp (-t * 25) + 0.4 * nd (rng) * std::exp (-t * 18)); }
        SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("snare 190Hz+noise", a); }
    { // hat: ruido filtrado pasa-altos simple
        std::vector<float> b ((size_t) (0.12 * SR)); double prev = 0; for (size_t i = 0; i < b.size(); ++i) { double t = i / SR; double n = nd (rng); double h = n - prev; prev = n; b[i] = (float) (0.3 * h * std::exp (-t * 40)); }
        SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("hi-hat noise", a); }
    { std::vector<float> b ((size_t) (0.6 * SR)); for (auto& v : b) v = (float) (0.2 * nd (rng)); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("white noise", a); }
    { // acorde corto de 0.08s (stab)
        std::vector<float> b; for (int m : { 60, 63, 67 }) addNote (b, m, 0, 0.15, 0.15, 12.0); SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, b); show ("C minor stab 0.15s", a); }

    // --- cálculo de desplazamiento ---
    {
        KeyResult r; r.valid = true; r.tonic = 9; r.minor = true; r.kind = SoundKind::Tonal;
        ShiftSettings s; s.targetRoot = 0; s.targetMinor = false;   // La menor -> Do mayor : 0 (relativas)
        std::printf ("\nAm -> C  : %+.2f (esperado 0)\n", computeShiftSemitones (r, s));
        s.targetMinor = true;                                        // La menor -> Do menor : +3
        std::printf ("Am -> Cm : %+.2f (esperado +3)\n", computeShiftSemitones (r, s));
        r.tonic = 0; r.minor = false; s.targetRoot = 0; s.targetMinor = true;
        std::printf ("C  -> Cm : %+.2f (esperado +3)\n", computeShiftSemitones (r, s));
        r.tonic = 7; r.minor = false; s.targetRoot = 2; s.targetMinor = false;
        std::printf ("G  -> D  : %+.2f (esperado -5)\n", computeShiftSemitones (r, s));
        KeyResult k; k.valid = true; k.kind = SoundKind::SinglePitch; k.hasDominant = true; k.dominantMidi = 31.0f;
        ShiftSettings p; p.targetRoot = 0;
        bool note = false; std::printf ("kick G1 -> C : %+.2f note=%d (esperado +5)\n", computeShiftSemitones (k, p, &note), note);
        k.dominantMidi = 31.3f; std::printf ("kick G1+30c -> C autotune: %+.2f (esperado +4.70)\n", computeShiftSemitones (k, p));
    }

    // --- motores de cambio de tono ---
    for (int engine = 0; engine < 2; ++engine)
    {
        for (double st : { 0.0, 3.0, -5.0, 7.0, -12.0, 12.0 })
        {
            const double ratio = semitonesToRatio (st);
            PhaseVocoder v; GranularShifter g; v.prepare (4096, 4); g.prepare (1536, 3072); v.setRatio (ratio); g.setRatio (ratio);
            const size_t n = (size_t) (1.5 * SR); std::vector<float> in (n), out (n);
            for (size_t i = 0; i < n; ++i) in[i] = (float) (0.5 * std::sin (2 * kPi * 440.0 * i / SR));
            for (size_t i = 0; i < n; ++i) out[i] = engine == 0 ? v.process (in[i]) : g.process (in[i]);
            const double f = peakFreq (out, (size_t) (0.6 * SR), 16384);
            const double expect = 440.0 * ratio;
            std::printf ("%s st=%+5.1f  f=%8.2f expect=%8.2f err=%+.1f cents  rms=%.3f (in %.3f)\n", engine == 0 ? "vocoder " : "granular", st, f, expect,
                         1200 * std::log2 (f / expect), rmsOf (out, (size_t) (0.6 * SR), (size_t) (0.6 * SR) + 16384), rmsOf (in, 0, 16384));
        }
    }
    // --- exportación offline ---
    {
        std::vector<float> in ((size_t) (1.0 * SR)); for (size_t i = 0; i < in.size(); ++i) in[i] = (float) (0.5 * std::sin (2 * kPi * 440.0 * i / SR));
        for (double st : { 3.0, -5.0, 12.0 })
        {
            auto vin = renderVinyl (in, semitonesToRatio (st));
            auto keep = renderKeepDuration (in, semitonesToRatio (st), false);
            auto keepG = renderKeepDuration (in, semitonesToRatio (st), true);
            std::printf ("export st=%+4.1f vinilo: len=%zu (esp %.0f) f=%.2f (esp %.2f) | keep: len=%zu f=%.2f | granular: len=%zu f=%.2f\n", st, vin.size(), in.size() / semitonesToRatio (st),
                         peakFreq (vin, vin.size() / 4, std::min<size_t> (8192, vin.size() / 2)), 440 * semitonesToRatio (st), keep.size(),
                         peakFreq (keep, 8192, 16384), keepG.size(), peakFreq (keepG, 8192, 16384));
        }
    }
    // --- kick a través de los dos motores ---
    {
        auto b = kick (140, 52, 0.6);
        std::vector<float> in = b; in.resize (in.size() + (size_t) (0.5 * SR), 0.f);
        for (int engine = 0; engine < 2; ++engine)
            for (double st : { 5.0, -7.0, 3.0 })
            {
                PhaseVocoder v; GranularShifter g; v.prepare (4096, 4); g.prepare (1536, 3072);
                v.setRatio (semitonesToRatio (st)); g.setRatio (semitonesToRatio (st));
                std::vector<float> out (in.size() + 6000, 0.f);
                for (size_t i = 0; i < out.size(); ++i) { float x = i < in.size() ? in[i] : 0.f; out[i] = engine == 0 ? v.process (x) : g.process (x); }
                size_t onset = 0; float pk = 0; for (size_t i = 0; i < out.size(); ++i) { if (std::fabs (out[i]) > 0.05f && onset == 0) onset = i; pk = std::max (pk, std::fabs (out[i])); }
                SampleAnalyzer a; a.prepare (SR); runAnalyzer (a, out);
                std::printf ("kick %s st=%+4.1f: onset=%zu (latencia esp. 3072) pico=%.2f (in 0.90) dom=%.1f Hz (esperado ~%.1f)\n", engine == 0 ? "vocoder " : "granular", st, onset, pk,
                             a.committed().dominantHz, 53.0 * semitonesToRatio (st));
            }
    }
    return 0;
}
