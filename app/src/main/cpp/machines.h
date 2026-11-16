// TinyBox - sound machines
// 0-5 : classic drum engines (Machinedrum TRX / EFM style)
// 6-8 : physically modelled percussion (modal resonator banks)
// 9   : sample slicer
// 10  : polyphonic synth
#pragma once
#include "dsp.h"
#include <vector>
#include <cstring>

constexpr int kMachParams = 12;
constexpr int kTrackParams = 16; // 12 machine + level, pan, delay send, reverb send

enum MachineId {
    M_BD = 0, M_SD, M_HH, M_CP, M_FM, M_CB,
    M_PMDRUM, M_PMCYM, M_PMBAR,
    M_SLICER, M_SYNTH,
    M_GRAIN, M_FMSYN, M_STRING,
    M_COUNT
};

struct TrigParams {
    float p[kTrackParams];
    float vel = 1.f;       // 0..1
    int note = 60;         // root midi note (synth) / slice index (slicer)
    int notes[4] = {60, -1, -1, -1}; // all notes of the step (chords recorded from the keyboard)
    int nnotes = 1;
    int chord = 0;         // synth chord type
    float gateSamples = -1; // <0 = hold until noteOff
};

struct Sample {
    std::vector<float> L, R;
    float sr = 48000.f;
    int frames() const { return (int)L.size(); }
};

struct SampleSlot {
    Sample *sample = nullptr;
    int sliceStart[65] = {0};
    int sliceCount = 1;
};

class Machine {
public:
    float sr = 48000.f;
    virtual ~Machine() = default;
    virtual void trigger(const TrigParams &t) = 0;
    virtual void noteOff(int /*note*/) {}
    virtual void allOff() {}
    // ADD stereo output into l / r. live = the track's current (possibly p-locked) params
    virtual void render(float *l, float *r, int n, const float *live) = 0;
};

// ---------------------------------------------------------------- BD
class BassDrum : public Machine {
    float ph = 0, amp = 0, ampC = 0, pEnv = 0, pC = 0, f0 = 50, sweep = 0;
    float click = 0, clickC = 0, drive = 0, noise = 0;
    OnePole tone; Rng rng{111};
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        f0 = expo(p[0], 28.f, 160.f);
        ampC = decayCoef(expo(p[1], 0.05f, 2.5f), sr);
        sweep = p[2] * 7.f;
        pC = decayCoef(expo(p[3], 0.004f, 0.25f), sr);
        click = p[4] * t.vel; clickC = decayCoef(0.006f, sr);
        drive = p[5];
        tone.setLP(expo(p[6], 150.f, 18000.f), sr);
        noise = p[7] * 0.4f;
        amp = t.vel; pEnv = 1.f; ph = 0.f;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-5f && click < 1e-5f) return;
        const float g = 1.f + drive * 7.f;
        for (int i = 0; i < n; ++i) {
            float f = f0 * (1.f + sweep * pEnv * pEnv);
            ph += f / sr; if (ph >= 1.f) ph -= 1.f;
            float s = std::sin(kTwoPi * ph) * amp;
            s += rng.next() * (click * 0.6f + noise * amp);
            s = tone.lp(softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f));
            l[i] += s; r[i] += s;
            amp *= ampC; pEnv *= pC; click *= clickC;
        }
    }
};

// ---------------------------------------------------------------- SD
class SnareDrum : public Machine {
    float ph1 = 0, ph2 = 0, f = 180, ratio = 1.5f, amp = 0, ampC = 0, namp = 0, nC = 0;
    float pEnv = 0, pC = 0, sweep = 0, snap = 0.5f, drive = 0;
    SVF nf; Rng rng{222};
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        f = expo(p[0], 90.f, 450.f);
        ampC = decayCoef(expo(p[1], 0.03f, 1.2f), sr);
        snap = p[2];
        nC = decayCoef(expo(p[3], 0.03f, 1.2f), sr);
        nf.set(expo(p[4], 800.f, 12000.f), 0.7f, sr);
        ratio = 1.3f + p[5] * 0.8f;
        sweep = p[6] * 2.f; pC = decayCoef(0.03f, sr);
        drive = p[7];
        amp = t.vel; namp = t.vel; pEnv = 1.f; ph1 = ph2 = 0.f;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-5f && namp < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            float fm = 1.f + sweep * pEnv;
            ph1 += f * fm / sr; if (ph1 >= 1.f) ph1 -= 1.f;
            ph2 += f * ratio * fm / sr; if (ph2 >= 1.f) ph2 -= 1.f;
            float body = (std::sin(kTwoPi * ph1) + 0.5f * std::sin(kTwoPi * ph2)) * 0.66f * amp;
            float lp, bp, hp; nf.proc(rng.next(), lp, bp, hp);
            float nz = (hp * 0.7f + bp * 0.6f) * namp;
            float s = body * (1.f - snap * 0.6f) + nz * snap * 1.4f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
            amp *= ampC; namp *= nC; pEnv *= pC;
        }
    }
};

// ---------------------------------------------------------------- HH (808 style metal)
class HiHat : public Machine {
    float ph[6] = {0}, fr[6] = {0}, amp = 0, ampC = 0, metal = 0.5f, drive = 0;
    SVF bp, hp; Rng rng{333};
    static constexpr float kRatios[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        float tune = expo(p[0], 0.5f, 2.2f);
        for (int i = 0; i < 6; ++i) fr[i] = kRatios[i] * tune / sr;
        ampC = decayCoef(expo(p[1], 0.02f, 1.6f), sr);
        float color = expo(p[2], 2500.f, 14000.f);
        metal = p[3];
        bp.set(color, 0.6f + p[4] * 6.f, sr);
        hp.set(color * 0.6f, 0.7f, sr);
        drive = p[5];
        amp = t.vel;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            float sq = 0.f;
            for (int k = 0; k < 6; ++k) {
                ph[k] += fr[k]; if (ph[k] >= 1.f) ph[k] -= 1.f;
                sq += ph[k] < 0.5f ? 1.f : -1.f;
            }
            float src = sq * (1.f / 6.f) * metal + rng.next() * (1.f - metal);
            float s = hp.hp(bp.bp(src) * 2.f) * amp * 1.4f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
            amp *= ampC;
        }
    }
};
constexpr float HiHat::kRatios[6];

// ---------------------------------------------------------------- Clap
class Clap : public Machine {
    SVF f; Rng rng{444};
    float burst = 0, burstC = 0, tail = 0, tailC = 0, room = 0, roomC = 0, roomLvl = 0, drive = 0, vel = 0;
    int t = 0, spacing = 400, reps = 3, done = 1;
public:
    void trigger(const TrigParams &tp) override {
        const float *p = tp.p;
        f.set(expo(p[0], 500.f, 5000.f), 0.6f + p[4] * 5.f, sr);
        tailC = decayCoef(expo(p[1], 0.05f, 1.2f), sr);
        spacing = (int)(expo(p[2], 0.004f, 0.03f) * sr);
        reps = 1 + (int)(p[3] * 5.f);
        drive = p[5];
        roomLvl = p[6]; roomC = decayCoef(expo(p[1], 0.3f, 3.f), sr);
        burstC = decayCoef(0.012f, sr);
        vel = tp.vel; t = 0; done = 0; burst = vel; tail = 0; room = 0;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (done && tail < 1e-5f && room < 1e-5f && burst < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            if (!done) {
                if (t > 0 && t % spacing == 0) {
                    int k = t / spacing;
                    if (k < reps) burst = vel;
                    else { done = 1; tail = vel; room = vel * roomLvl * 0.5f; }
                }
                ++t;
            }
            float env = burst + tail + room;
            float s = f.bp(rng.next()) * env * 2.2f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
            burst *= burstC; tail *= tailC; room *= roomC;
        }
    }
};

// ---------------------------------------------------------------- FM percussion (EFM style)
class FMPerc : public Machine {
    float cph = 0, mph = 0, f = 200, ratio = 1, idx = 0, amp = 0, ampC = 0, menv = 0, mC = 0;
    float pEnv = 0, pC = 0, sweep = 0, noise = 0, drive = 0;
    Rng rng{555};
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        f = expo(p[0], 35.f, 1200.f);
        ampC = decayCoef(expo(p[1], 0.02f, 2.f), sr);
        ratio = 0.5f + p[2] * 7.5f;
        idx = p[3] * 8.f;
        mC = decayCoef(expo(p[4], 0.005f, 1.5f), sr);
        sweep = p[5] * 4.f; pC = decayCoef(0.05f, sr);
        noise = p[6] * 0.5f;
        drive = p[7];
        amp = t.vel; menv = 1.f; pEnv = 1.f; cph = mph = 0.f;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            float fc = f * (1.f + sweep * pEnv);
            mph += fc * ratio / sr; if (mph >= 1.f) mph -= 1.f;
            float m = std::sin(kTwoPi * mph) * idx * menv;
            cph += fc / sr; if (cph >= 1.f) cph -= 1.f;
            float s = (std::sin(kTwoPi * cph + m) + rng.next() * noise * menv) * amp;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
            amp *= ampC; menv *= mC; pEnv *= pC;
        }
    }
};

// ---------------------------------------------------------------- Cowbell / metal
class Cowbell : public Machine {
    float ph1 = 0, ph2 = 0, f1 = 0, f2 = 0, amp = 0, ampC = 0, tr = 0, trC = 0, drive = 0;
    SVF bp;
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        float f = expo(p[0], 200.f, 1500.f);
        f1 = f / sr; f2 = f * (1.2f + p[2] * 0.6f) / sr;
        ampC = decayCoef(expo(p[1], 0.05f, 1.5f), sr);
        bp.set(f * expo(p[3], 0.8f, 4.f), 0.8f + p[4] * 8.f, sr);
        drive = p[5];
        amp = t.vel * 0.6f; tr = t.vel; trC = decayCoef(0.02f, sr);
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-5f && tr < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            ph1 += f1; if (ph1 >= 1.f) ph1 -= 1.f;
            ph2 += f2; if (ph2 >= 1.f) ph2 -= 1.f;
            float sq = (ph1 < 0.5f ? 1.f : -1.f) + (ph2 < 0.5f ? 1.f : -1.f);
            float s = bp.bp(sq) * (amp + tr) * 1.2f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
            amp *= ampC; tr *= trC;
        }
    }
};

// ---------------------------------------------------------------- Modal resonator bank (physical modelling core)
template <int N>
struct ModalBank {
    float re[N] = {0}, im[N] = {0};
    float c[N] = {0}, s[N] = {0}, r[N] = {0}, gain[N] = {0};
    float ratio[N] = {0};
    int count = N;
    void setMode(int i, float freq, float decaySec, float g, float sr) {
        if (freq >= sr * 0.45f || freq <= 0.f) { gain[i] = 0.f; r[i] = 0.f; c[i] = 1.f; s[i] = 0.f; return; }
        float w = kTwoPi * freq / sr;
        c[i] = std::cos(w); s[i] = std::sin(w);
        r[i] = decayCoef(decaySec, sr);
        gain[i] = g;
    }
    void setFreq(int i, float freq, float sr) {
        if (freq >= sr * 0.45f) { c[i] = 1.f; s[i] = 0.f; return; }
        float w = kTwoPi * freq / sr;
        c[i] = std::cos(w); s[i] = std::sin(w);
    }
    inline float tick(float x) {
        float out = 0.f;
        for (int i = 0; i < count; ++i) {
            float nr = r[i] * (c[i] * re[i] - s[i] * im[i]) + x * gain[i];
            float ni = r[i] * (s[i] * re[i] + c[i] * im[i]);
            re[i] = nr; im[i] = ni;
            out += ni;
        }
        return out;
    }
    float energy() const { float e = 0; for (int i = 0; i < count; ++i) e += std::fabs(re[i]) + std::fabs(im[i]); return e; }
    void clear() { for (int i = 0; i < N; ++i) re[i] = im[i] = 0.f; }
};

// mallet exciter: raised-cosine pulse whose width sets brightness, + a little noise
struct Mallet {
    int len = 0, pos = 0; float amp = 0, noise = 0, norm = 1;
    Rng rng{999};
    void strike(float hardness, float vel, float sr, float noiseAmt) {
        float sec = expo(1.f - hardness, 0.00025f, 0.008f);
        len = std::max(2, (int)(sec * sr)); pos = 0; amp = vel; noise = noiseAmt;
        norm = 2.f / (float)len;
    }
    inline float tick() {
        if (pos >= len) return 0.f;
        float ph = (float)pos / (float)len;
        float v = 0.5f * (1.f - std::cos(kTwoPi * ph));
        ++pos;
        return (v + rng.next() * noise) * amp * norm;
    }
    bool active() const { return pos < len; }
};

// ---------------------------------------------------------------- PM membrane drum
class PMDrum : public Machine {
    static constexpr int N = 16;
    // circular membrane mode ratios (Bessel zeros) and their m index
    static constexpr float kMem[N] = {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f,
                                      3.600f, 3.652f, 4.060f, 4.154f, 4.601f, 4.832f, 4.903f, 5.131f};
    static constexpr int kM[N] = {0, 1, 2, 0, 3, 1, 4, 2, 0, 5, 3, 6, 1, 4, 0, 7};
    ModalBank<N> bank; Mallet mallet; Rng rng{777};
    float f0 = 100, bend = 0, bendEnv = 0, bendC = 0, wires = 0, follow = 0, drive = 0;
    float fr[N] = {0};
    int ctr = 0;
    SVF wf;
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        f0 = expo(p[0], 40.f, 800.f);
        float decay = expo(p[1], 0.05f, 4.f);
        float damp = p[2];
        bend = p[3] * 0.8f;
        float pos = p[4];
        float hard = p[5];
        wires = p[6];
        float shape = p[7];
        drive = p[8];
        for (int i = 0; i < N; ++i) {
            float harm = (float)(i + 1);
            fr[i] = lerpf(kMem[i], harm, shape);
            float dec = decay / (1.f + damp * 6.f * (fr[i] - 1.f));
            float g = (kM[i] == 0 ? (1.f - 0.75f * pos) : (0.25f + 0.9f * pos)) / (1.f + 0.35f * i);
            bank.setMode(i, f0 * fr[i], dec, g, sr);
        }
        bendEnv = 1.f; bendC = decayCoef(expo(p[3], 0.03f, 0.3f), sr);
        wf.set(3500.f, 0.8f, sr);
        mallet.strike(hard, t.vel, sr, hard * 0.2f);
        ctr = 0;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (!mallet.active() && bank.energy() < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            if ((ctr++ & 15) == 0 && bend > 0.f) {
                float m = 1.f + bend * bendEnv;
                for (int k = 0; k < N; ++k) bank.setFreq(k, f0 * fr[k] * m, sr);
            }
            bendEnv *= bendC;
            float s = bank.tick(mallet.tick()) * 0.9f;
            float a = std::fabs(s); follow += (a - follow) * (a > follow ? 0.05f : 0.0005f);
            if (wires > 0.f) s += wf.bp(rng.next()) * follow * wires * 3.f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
        }
    }
};
constexpr float PMDrum::kMem[PMDrum::N];
constexpr int PMDrum::kM[PMDrum::N];

// ---------------------------------------------------------------- PM cymbal / plate
class PMCymbal : public Machine {
    static constexpr int N = 32;
    ModalBank<N> bank; Mallet mallet; Rng rng{888};
    SVF hp; float sizzle = 0, follow = 0, drive = 0;
    float jit[N];
public:
    PMCymbal() { Rng j(4242); for (float &x : jit) x = j.uni(); }
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        float f0 = expo(p[0], 150.f, 1500.f);
        float decay = expo(p[1], 0.08f, 6.f);
        float damp = p[2];
        float spread = 0.15f + p[3] * 0.85f;
        float pos = p[4];
        float hard = p[5];
        sizzle = p[6];
        float bell = p[7];
        drive = p[8];
        for (int i = 0; i < N; ++i) {
            float ratio = 1.f + std::pow((float)i, 1.45f) * spread * 0.55f + (jit[i] - 0.5f) * spread * 0.6f;
            if (i == 0) ratio = 1.f;
            float dec = decay / (1.f + damp * 2.5f * (ratio - 1.f) * 0.3f);
            if (i < 3) dec *= 1.f + bell * 2.f;
            float tilt = std::pow(0.93f + 0.06f * pos, (float)i);
            float g = tilt * (0.35f + 0.65f * std::fabs(std::sin((i + 1) * (0.3f + pos * 2.1f))));
            if (i < 3) g *= 1.f + bell * 2.f;
            bank.setMode(i, f0 * ratio, dec, g * 0.5f, sr);
        }
        hp.set(6000.f, 0.7f, sr);
        mallet.strike(hard, t.vel, sr, 0.3f + hard * 0.5f);
    }
    void render(float *l, float *r, int n, const float *) override {
        if (!mallet.active() && bank.energy() < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            float s = bank.tick(mallet.tick());
            float a = std::fabs(s); follow += (a - follow) * (a > follow ? 0.02f : 0.0003f);
            if (sizzle > 0.f) s += hp.hp(rng.next()) * follow * sizzle * 2.5f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
        }
    }
};

// ---------------------------------------------------------------- PM bar (wood block -> marimba -> glass)
class PMBar : public Machine {
    static constexpr int N = 8;
    static constexpr float kBar[N] = {1.f, 2.756f, 5.404f, 8.933f, 13.344f, 18.64f, 24.8f, 31.9f};
    static constexpr float kMar[N] = {1.f, 3.99f, 9.14f, 10.7f, 15.8f, 19.2f, 26.4f, 32.f};
    static constexpr float kHar[N] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    ModalBank<N + 1> bank; Mallet mallet;
    float drive = 0;
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        float f0 = mtof((float)t.note - 60.f + 48.f + p[0] * 48.f);  // pitch knob spans 4 octaves, note transposes
        float decay = expo(p[1], 0.03f, 3.f);
        float damp = p[2];
        float mat = p[3] * 2.f;
        float pos = 0.05f + p[4] * 0.45f;
        float hard = p[5];
        float tube = p[6];
        float noise = p[7];
        drive = p[8];
        for (int i = 0; i < N; ++i) {
            float ratio = mat < 1.f ? lerpf(kBar[i], kMar[i], mat) : lerpf(kMar[i], kHar[i], mat - 1.f);
            float dec = decay / (1.f + damp * 3.f * (ratio - 1.f));
            float g = std::fabs(std::sin(kPi * (i + 1) * pos)) / (1.f + 0.4f * i) + 0.05f;
            bank.setMode(i, f0 * ratio, dec, g, sr);
        }
        bank.setMode(N, f0, decay * 2.5f, tube * 0.7f, sr); // tube resonator
        mallet.strike(hard, t.vel, sr, noise);
    }
    void render(float *l, float *r, int n, const float *) override {
        if (!mallet.active() && bank.energy() < 1e-5f) return;
        const float g = 1.f + drive * 5.f;
        for (int i = 0; i < n; ++i) {
            float s = bank.tick(mallet.tick()) * 0.9f;
            s = softclip(s * g) / (drive > 0.f ? softclip(g) : 1.f);
            l[i] += s; r[i] += s;
        }
    }
};
constexpr float PMBar::kBar[PMBar::N];
constexpr float PMBar::kMar[PMBar::N];
constexpr float PMBar::kHar[PMBar::N];

// ---------------------------------------------------------------- Sample slicer
class Slicer : public Machine {
    struct V {
        bool on = false; double pos = 0, inc = 1; int start = 0, end = 0; bool rev = false;
        float amp = 0, ampC = 1, fade = 1, fadeStep = 0; int age = 0, attack = 0;
    };
    static constexpr int NV = 4;
    V v[NV];
    SVF fl, fr;
    float lastCut = -1, lastRes = -1;
    int ctr = 0;
public:
    SampleSlot *slot = nullptr;
    void allOff() override { for (auto &x : v) x.on = false; }
    void trigger(const TrigParams &t) override {
        for (int k = 0; k < t.nnotes; ++k) triggerOne(t, t.notes[k], k == 0);
    }
    void triggerOne(const TrigParams &t, int note, bool first) {
        if (!slot || !slot->sample) return;
        const Sample *smp = slot->sample;
        const float *p = t.p;
        int cnt = std::max(1, slot->sliceCount);
        int si = ((note % cnt) + cnt) % cnt;
        int s0 = slot->sliceStart[si];
        int s1 = (si + 1 < cnt) ? slot->sliceStart[si + 1] : smp->frames();
        s0 = std::clamp(s0, 0, smp->frames()); s1 = std::clamp(s1, 0, smp->frames());
        if (s1 - s0 < 16) return;
        int len = s1 - s0;
        int st = s0 + (int)(p[1] * 0.95f * len);
        int en = st + std::max(16, (int)((s1 - st) * (0.05f + 0.95f * p[2])));
        en = std::min(en, s1);
        bool choke = p[8] > 0.5f && first;
        if (choke) for (auto &x : v) if (x.on && x.fadeStep == 0.f) x.fadeStep = 1.f / (0.004f * sr);
        V *slotV = nullptr;
        for (auto &x : v) if (!x.on) { slotV = &x; break; }
        if (!slotV) { slotV = &v[0]; for (auto &x : v) if (x.age > slotV->age) slotV = &x; }
        V &n = *slotV;
        n.on = true; n.start = st; n.end = en; n.rev = p[4] > 0.5f;
        n.pos = n.rev ? (double)(en - 1) : (double)st;
        float semis = (p[0] - 0.5f) * 48.f;
        n.inc = std::pow(2.0, semis / 12.0) * smp->sr / sr;
        n.amp = t.vel * expo(p[9], 0.1f, 2.f);
        n.ampC = p[3] >= 0.99f ? 1.f : decayCoef(expo(p[3], 0.02f, 4.f), sr);
        n.fade = 1.f; n.fadeStep = 0.f; n.age = 0; n.attack = 0;
    }
    void render(float *l, float *r, int n, const float *live) override {
        bool any = false;
        for (auto &x : v) any |= x.on;
        if (!any || !slot || !slot->sample) return;
        const Sample *smp = slot->sample;
        const int frames = smp->frames();
        const float cut = live[5], res = live[6];
        const bool filt = std::fabs(cut - 0.5f) > 0.02f;
        const float drive = live[7];
        const float g = 1.f + drive * 5.f;
        const int atk = (int)(0.002f * sr);
        for (int i = 0; i < n; ++i) {
            if ((ctr++ & 31) == 0 && filt && (cut != lastCut || res != lastRes)) {
                float q = 0.6f + res * 8.f;
                float f = cut < 0.5f ? expo(cut * 2.f, 60.f, 18000.f) : expo((cut - 0.5f) * 2.f, 20.f, 9000.f);
                fl.set(f, q, sr); fr.set(f, q, sr);
                lastCut = cut; lastRes = res;
            }
            float ol = 0, orr = 0;
            for (auto &x : v) {
                if (!x.on) continue;
                ++x.age;
                int ip = (int)x.pos; float fpos = (float)(x.pos - ip);
                if (ip < 0 || ip + 1 >= frames) { x.on = false; continue; }
                float sl = smp->L[ip] + (smp->L[ip + 1] - smp->L[ip]) * fpos;
                float sr2 = smp->R[ip] + (smp->R[ip + 1] - smp->R[ip]) * fpos;
                float edge = 1.f;
                if (x.attack < atk) { edge = (float)x.attack / atk; ++x.attack; }
                double remain = x.rev ? (x.pos - x.start) / x.inc : (x.end - x.pos) / x.inc;
                if (remain < atk) edge *= (float)std::max(0.0, remain / atk);
                float a = x.amp * edge * x.fade;
                ol += sl * a; orr += sr2 * a;
                x.amp *= x.ampC;
                if (x.fadeStep > 0.f) { x.fade -= x.fadeStep; if (x.fade <= 0.f) x.on = false; }
                x.pos += x.rev ? -x.inc : x.inc;
                if (x.pos >= x.end || x.pos < x.start || x.amp < 1e-5f) x.on = false;
            }
            if (filt) {
                float a, b, c;
                fl.proc(ol, a, b, c); ol = cut < 0.5f ? a : c;
                fr.proc(orr, a, b, c); orr = cut < 0.5f ? a : c;
            }
            if (drive > 0.f) { float d = softclip(g); ol = softclip(ol * g) / d; orr = softclip(orr * g) / d; }
            l[i] += ol; r[i] += orr;
        }
    }
};

// ---------------------------------------------------------------- Polyphonic synth
class PolySynth : public Machine {
    struct V {
        int note = -1; int stage = 0; // 0 off 1 atk 2 dec/sus 3 rel
        float env = 0, vel = 0, gateLeft = -1; float ph1 = 0, ph2 = 0, ph3 = 0; float freq = 0;
        SVF f; int age = 0;
    };
    static constexpr int NV = 8;
    V v[NV];
    float lfo = 0;
    int ctr = 0;
    static float osc(float &ph, float dt, float wave) {
        ph += dt; if (ph >= 1.f) ph -= 1.f;
        float saw = 2.f * ph - 1.f - polyblep(ph, dt);
        float pw = wave < 0.5f ? 0.5f : 0.5f - (wave - 0.5f) * 0.8f;
        float p2 = ph + (1.f - pw); if (p2 >= 1.f) p2 -= 1.f;
        float sq = (ph < pw ? 1.f : -1.f) + polyblep(ph, dt) - polyblep(p2, dt);
        float m = wave < 0.5f ? wave * 2.f : 1.f;
        return saw * (1.f - m) + sq * m;
    }
    static constexpr int kChords[9][4] = {
        {0, -99, -99, -99}, {0, 4, 7, -99}, {0, 3, 7, -99}, {0, 4, 7, 11}, {0, 3, 7, 10},
        {0, 5, 7, -99}, {0, 7, -99, -99}, {0, 12, -99, -99}, {0, 4, 7, 10}};
    void startNote(int note, float vel, float gate) {
        V *nv = nullptr;
        for (auto &x : v) if (x.stage == 0) { nv = &x; break; }
        if (!nv) for (auto &x : v) if (x.stage == 3 && (!nv || x.age > nv->age)) nv = &x;
        if (!nv) { nv = &v[0]; for (auto &x : v) if (x.age > nv->age) nv = &x; }
        nv->note = note; nv->vel = vel; nv->gateLeft = gate; nv->stage = 1; nv->age = 0;
        nv->freq = mtof((float)note);
        if (nv->env < 0.001f) { nv->f.reset(); nv->ph1 = 0; nv->ph2 = 0.37f; nv->ph3 = 0; }
    }
public:
    void allOff() override { for (auto &x : v) if (x.stage) x.stage = 3; }
    void trigger(const TrigParams &t) override {
        int c = std::clamp(t.chord, 0, 8);
        for (int k = 0; k < 4; ++k) {
            int iv = kChords[c][k];
            if (iv < -12) continue;
            startNote(std::clamp(t.note + iv, 0, 127), t.vel, t.gateSamples);
        }
        for (int k = 1; k < t.nnotes; ++k) if (t.notes[k] >= 0) startNote(std::clamp(t.notes[k], 0, 127), t.vel, t.gateSamples);
    }
    void noteOff(int note) override {
        for (auto &x : v) if (x.note == note && (x.stage == 1 || x.stage == 2) && x.gateLeft < 0) x.stage = 3;
    }
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false;
        for (auto &x : v) any |= x.stage != 0;
        if (!any) return;
        const float wave = p[0];
        const float det = p[1] * 30.f;
        const float m1 = std::pow(2.f, -det / 1200.f), m2 = std::pow(2.f, det / 1200.f);
        const float sub = p[2];
        const float cut = expo(p[3], 40.f, 18000.f);
        const float q = 0.5f + p[4] * 10.f;
        const float fenv = p[5] * 6.f;
        const float atkRate = 1.f / (expo(p[6], 0.001f, 3.f) * sr);
        const float decC = decayCoef(expo(p[7], 0.01f, 4.f), sr);
        const float sus = p[8];
        const float relC = decayCoef(expo(p[9], 0.01f, 5.f), sr);
        const float vib = p[10] * 0.6f;
        const float drive = p[11];
        const float g = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(g) : 1.f;
        const float lfoInc = 5.2f / sr;
        for (int i = 0; i < n; ++i) {
            lfo += lfoInc; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            const bool upd = (ctr++ & 15) == 0;
            float out = 0.f;
            for (auto &x : v) {
                if (x.stage == 0) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.stage < 3) x.stage = 3; }
                switch (x.stage) {
                    case 1: x.env += atkRate; if (x.env >= 1.f) { x.env = 1.f; x.stage = 2; } break;
                    case 2: x.env = sus + (x.env - sus) * decC; break;
                    case 3: x.env *= relC; if (x.env < 1e-4f) { x.env = 0.f; x.stage = 0; } break;
                }
                if (x.stage == 0) continue;
                if (upd) x.f.set(cut * std::pow(2.f, fenv * x.env), q, sr);
                float dt = x.freq * vm / sr;
                float s = osc(x.ph1, dt * m1, wave) + osc(x.ph2, dt * m2, wave);
                x.ph3 += dt * 0.5f; if (x.ph3 >= 1.f) x.ph3 -= 1.f;
                s += (x.ph3 < 0.5f ? 1.f : -1.f) * sub;
                s = x.f.lp(s * 0.4f) * x.env * x.vel;
                out += s;
            }
            out = softclip(out * 0.5f * g) / gn;
            l[i] += out; r[i] += out;
        }
    }
};
constexpr int PolySynth::kChords[9][4];


// ---------------------------------------------------------------- shared ADSR for the poly engines
struct ADSR {
    int stage = 0; float env = 0;   // 0 off 1 atk 2 dec/sus 3 rel
    float atkRate = 0.01f, decC = 0.999f, sus = 0.7f, relC = 0.999f;
    void set(float a, float d, float s, float r, float sr) {
        atkRate = 1.f / (std::max(a, 0.0005f) * sr); decC = decayCoef(d, sr); sus = s; relC = decayCoef(r, sr);
    }
    inline float tick() {
        switch (stage) {
            case 1: env += atkRate; if (env >= 1.f) { env = 1.f; stage = 2; } break;
            case 2: env = sus + (env - sus) * decC; break;
            case 3: env *= relC; if (env < 1e-4f) { env = 0.f; stage = 0; } break;
        }
        return env;
    }
};

// ---------------------------------------------------------------- Granular (uses the track's sample)
class Granular : public Machine {
    struct Grain { bool on = false; double pos = 0, inc = 1; int len = 1, age = 0; float gl = 0, gr = 0; };
    struct V { int note = -1; ADSR env; float vel = 0, gateLeft = -1, timer = 0; double scan = 0; int age = 0; };
    static constexpr int NG = 96, NV = 4;
    Grain g[NG]; V v[NV];
    Rng rng{4321};
    OnePole tl, tr; float lastTone = -1;
    void start(int note, float vel, float gate) {
        V *nv = nullptr;
        for (auto &x : v) if (x.env.stage == 0) { nv = &x; break; }
        if (!nv) { nv = &v[0]; for (auto &x : v) if (x.age > nv->age) nv = &x; }
        nv->note = note; nv->vel = vel; nv->gateLeft = gate; nv->env.stage = 1; nv->timer = 0; nv->scan = 0; nv->age = 0;
    }
public:
    SampleSlot *slot = nullptr;
    void allOff() override { for (auto &x : v) if (x.env.stage) x.env.stage = 3; }
    void trigger(const TrigParams &t) override {
        for (int k = 0; k < t.nnotes; ++k) if (t.notes[k] >= 0) start(t.notes[k], t.vel, t.gateSamples);
    }
    void noteOff(int note) override {
        for (auto &x : v) if (x.note == note && x.env.stage && x.env.stage < 3 && x.gateLeft < 0) x.env.stage = 3;
    }
    void render(float *l, float *r, int n, const float *p) override {
        if (!slot || !slot->sample) return;
        bool any = false;
        for (auto &x : v) any |= x.env.stage != 0;
        for (auto &x : g) any |= x.on;
        if (!any) return;
        const Sample *smp = slot->sample;
        const int frames = smp->frames();
        if (frames < 64) return;
        const float pos = p[0];
        const float size = expo(p[1], 0.01f, 0.5f);
        const float dens = expo(p[2], 2.f, 120.f);
        const float spray = p[3];
        const float pitch = (p[4] - 0.5f) * 48.f;
        const float jitter = p[5] * p[5] * 12.f;
        const float spread = p[6];
        const float revProb = p[7];
        const float scanSpeed = (p[8] - 0.5f) * 0.4f; // fraction of file per second
        if (p[11] != lastTone) { tl.setLP(expo(p[11], 300.f, 20000.f), sr); tr.a = tl.a; lastTone = p[11]; }
        const float gain = 0.6f / std::sqrt(1.f + dens * size * 0.6f);
        const int glen = std::max(16, (int)(size * sr));
        const float interval = sr / dens;
        for (auto &x : v) if (x.env.stage) x.env.set(expo(p[9], 0.001f, 3.f), 1.f, 1.f, expo(p[10], 0.01f, 6.f), sr);
        for (int i = 0; i < n; ++i) {
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) x.env.stage = 3; }
                float e = x.env.tick();
                x.scan += scanSpeed / sr;
                x.timer -= 1.f;
                if (x.timer <= 0.f && x.env.stage) {
                    x.timer += interval * (1.f + rng.next() * 0.25f * spray);
                    Grain *gr = nullptr;
                    for (auto &q : g) if (!q.on) { gr = &q; break; }
                    if (gr) {
                        double c = pos + x.scan + rng.next() * spray * 0.25;
                        c -= std::floor(c);
                        float semis = (float)(x.note - 60) + pitch + rng.next() * jitter;
                        gr->inc = std::pow(2.0, semis / 12.0) * smp->sr / sr;
                        if (rng.uni() < revProb) gr->inc = -gr->inc;
                        gr->pos = c * (frames - 2);
                        gr->len = glen; gr->age = 0; gr->on = true;
                        float pan = 0.5f + rng.next() * spread * 0.5f;
                        float a = e * x.vel * gain;
                        gr->gl = std::cos(pan * kPi * 0.5f) * 1.414f * a;
                        gr->gr = std::sin(pan * kPi * 0.5f) * 1.414f * a;
                    }
                }
            }
            float ol = 0.f, orr = 0.f;
            for (auto &q : g) {
                if (!q.on) continue;
                double ps = q.pos;
                if (ps < 0) ps += frames - 1; else if (ps >= frames - 1) ps -= frames - 1;
                q.pos = ps;
                int ip = (int)ps; float fr = (float)(ps - ip);
                float w = 0.5f - 0.5f * std::cos(kTwoPi * (float)q.age / (float)q.len);
                float sl = smp->L[ip] + (smp->L[ip + 1] - smp->L[ip]) * fr;
                float sr2 = smp->R[ip] + (smp->R[ip + 1] - smp->R[ip]) * fr;
                ol += sl * w * q.gl; orr += sr2 * w * q.gr;
                q.pos += q.inc;
                if (++q.age >= q.len) q.on = false;
            }
            l[i] += tl.lp(ol); r[i] += tr.lp(orr);
        }
    }
    // UI: current play position of the first active voice (0..1), or -1
    float playhead(float pos) const {
        for (auto &x : v) if (x.env.stage) { double c = pos + x.scan; return (float)(c - std::floor(c)); }
        return -1.f;
    }
};

// ---------------------------------------------------------------- FM poly synth (2-op + feedback)
class FMSynth : public Machine {
    struct V { int note = -1; ADSR env; float vel = 0, gateLeft = -1, mph = 0, cph = 0, menv = 0, fbz = 0, freq = 0; int age = 0; };
    static constexpr int NV = 8;
    V v[NV]; float lfo = 0;
    static constexpr float kRatios[12] = {0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f, 5.f, 6.f, 7.f, 9.f};
    void start(int note, float vel, float gate) {
        V *nv = nullptr;
        for (auto &x : v) if (x.env.stage == 0) { nv = &x; break; }
        if (!nv) for (auto &x : v) if (x.env.stage == 3 && (!nv || x.age > nv->age)) nv = &x;
        if (!nv) { nv = &v[0]; for (auto &x : v) if (x.age > nv->age) nv = &x; }
        nv->note = note; nv->vel = vel; nv->gateLeft = gate; nv->env.stage = 1; nv->menv = 1.f; nv->age = 0;
        nv->freq = mtof((float)note);
        if (nv->env.env < 0.001f) { nv->mph = 0; nv->cph = 0; nv->fbz = 0; }
    }
public:
    void allOff() override { for (auto &x : v) if (x.env.stage) x.env.stage = 3; }
    void trigger(const TrigParams &t) override {
        for (int k = 0; k < t.nnotes; ++k) if (t.notes[k] >= 0) start(std::clamp(t.notes[k], 0, 127), t.vel, t.gateSamples);
    }
    void noteOff(int note) override {
        for (auto &x : v) if (x.note == note && x.env.stage && x.env.stage < 3 && x.gateLeft < 0) x.env.stage = 3;
    }
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false;
        for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        const float ratio = kRatios[std::clamp((int)(p[0] * 11.99f), 0, 11)] * (1.f + (p[1] - 0.5f) * 0.04f);
        const float idx = p[2] * 6.f, idxEnv = p[3] * 10.f;
        const float mC = decayCoef(expo(p[4], 0.01f, 3.f), sr);
        const float fb = p[5] * 1.4f;
        const float vib = p[10] * 0.6f;
        const float drive = p[11], g = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(g) : 1.f;
        for (auto &x : v) if (x.env.stage) x.env.set(expo(p[6], 0.001f, 3.f), expo(p[7], 0.01f, 4.f), p[8], expo(p[9], 0.01f, 5.f), sr);
        for (int i = 0; i < n; ++i) {
            lfo += 5.2f / sr; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) x.env.stage = 3; }
                float e = x.env.tick();
                if (!x.env.stage) continue;
                float f = x.freq * vm / sr;
                x.mph += f * ratio; if (x.mph >= 1.f) x.mph -= 1.f;
                float m = std::sin(kTwoPi * x.mph + fb * x.fbz);
                x.fbz = m;
                x.cph += f; if (x.cph >= 1.f) x.cph -= 1.f;
                float s = std::sin(kTwoPi * x.cph + m * (idx + idxEnv * x.menv));
                x.menv *= mC;
                out += s * e * x.vel * 0.3f;
            }
            out = softclip(out * g) / gn;
            l[i] += out; r[i] += out;
        }
    }
};
constexpr float FMSynth::kRatios[12];

// ---------------------------------------------------------------- PM String (Karplus-Strong, two detuned strings per voice)
class PMString : public Machine {
    static constexpr int BUF = 4096;
    struct Str { float buf[BUF]; int w = 0; float len = 100, z = 0; };
    struct V { int note = -1; Str s[2]; float vel = 0, gateLeft = -1, fb = 0.99f, relFb = 0.9f; bool rel = false; int age = 0; bool on = false; float level = 0; };
    static constexpr int NV = 8;
    V v[NV];
    Rng rng{2468};
    SVF body; float lastBody = -1; OnePole dc;
    float damp = 0.3f;
public:
    PMString() { for (auto &x : v) for (auto &s : x.s) std::memset(s.buf, 0, sizeof(s.buf)); }
    void allOff() override { for (auto &x : v) x.rel = true; }
    void trigger(const TrigParams &t) override {
        for (int k = 0; k < t.nnotes; ++k) if (t.notes[k] >= 0) start(t, std::clamp(t.notes[k], 12, 120));
    }
    void start(const TrigParams &t, int note) {
        const float *p = t.p;
        V *nv = nullptr;
        for (auto &x : v) if (!x.on) { nv = &x; break; }
        if (!nv) { nv = &v[0]; for (auto &x : v) if (x.age > nv->age) nv = &x; }
        V &x = *nv;
        float f = mtof((float)note);
        float t60 = expo(p[0], 0.2f, 15.f), rt60 = expo(p[6], 0.03f, 2.f);
        x.fb = std::pow(10.f, -3.f / (t60 * f)); x.relFb = std::pow(10.f, -3.f / (rt60 * f));
        float det = p[5] * 0.012f;
        float bright = p[3];
        float pick = 0.05f + p[2] * 0.45f;
        for (int k = 0; k < 2; ++k) {
            Str &s = x.s[k];
            float ff = f * (k == 0 ? 1.f - det : 1.f + det);
            s.len = clampf(sr / ff, 2.f, BUF - 4.f);
            int N = (int)s.len + 2;
            float lp = 0.f, a = expo(bright, 0.05f, 1.f);
            float tmp[BUF];
            for (int i = 0; i < N; ++i) { lp += (rng.next() - lp) * a; tmp[i] = lp; }
            int P = std::max(1, (int)(pick * N));
            for (int i = 0; i < N; ++i) {
                float e = tmp[i] - (i >= P ? tmp[i - P] : 0.f);
                s.buf[(s.w - N + i + BUF) % BUF] += e * t.vel * 0.8f;
            }
        }
        x.note = note; x.vel = t.vel; x.gateLeft = t.gateSamples; x.rel = false; x.on = true; x.age = 0; x.level = 1.f;
    }
    void noteOff(int note) override { for (auto &x : v) if (x.on && x.note == note && x.gateLeft < 0) x.rel = true; }
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false;
        for (auto &x : v) any |= x.on;
        if (!any) return;
        damp = p[1] * 0.9f;
        if (p[4] != lastBody) { body.set(expo(p[4], 150.f, 3000.f), 1.2f, sr); lastBody = p[4]; dc.setLP(20.f, sr); }
        const float bodyMix = p[4] > 0.01f ? 0.5f : 0.f;
        const float drive = p[7], g = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(g) : 1.f;
        for (int i = 0; i < n; ++i) {
            float out = 0.f;
            for (auto &x : v) {
                if (!x.on) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f) x.rel = true; }
                float fb = x.rel ? x.relFb : x.fb;
                float vo = 0.f, peak = 0.f;
                for (auto &s : x.s) {
                    float rp = (float)s.w - s.len; if (rp < 0) rp += BUF;
                    int i0 = (int)rp; float fr = rp - i0; int i1 = (i0 + 1) % BUF;
                    float y = s.buf[i0] + (s.buf[i1] - s.buf[i0]) * fr;
                    s.z = y * (1.f - damp) + s.z * damp;
                    float nv = s.z * fb;
                    s.buf[s.w] = nv; s.w = (s.w + 1) % BUF;
                    vo += y; peak = std::max(peak, std::fabs(y));
                }
                x.level = x.level * 0.999f + peak * 0.001f;
                if (x.age > (int)sr / 10 && x.level < 1e-4f) { x.on = false; for (auto &s : x.s) std::memset(s.buf, 0, sizeof(s.buf)); }
                out += vo * 0.5f;
            }
            float b = body.bp(out);
            out = dc.hp(out + b * bodyMix * 2.f);
            out = softclip(out * g) / gn;
            l[i] += out; r[i] += out;
        }
    }
};

inline Machine *makeMachine(int id, float sr, SampleSlot *slot) {
    Machine *m = nullptr;
    switch (id) {
        case M_BD: m = new BassDrum(); break;
        case M_SD: m = new SnareDrum(); break;
        case M_HH: m = new HiHat(); break;
        case M_CP: m = new Clap(); break;
        case M_FM: m = new FMPerc(); break;
        case M_CB: m = new Cowbell(); break;
        case M_PMDRUM: m = new PMDrum(); break;
        case M_PMCYM: m = new PMCymbal(); break;
        case M_PMBAR: m = new PMBar(); break;
        case M_SLICER: { auto *s = new Slicer(); s->slot = slot; m = s; break; }
        case M_GRAIN: { auto *g = new Granular(); g->slot = slot; m = g; break; }
        case M_FMSYN: m = new FMSynth(); break;
        case M_STRING: m = new PMString(); break;
        case M_SYNTH: default: m = new PolySynth(); break;
    }
    m->sr = sr;
    return m;
}
