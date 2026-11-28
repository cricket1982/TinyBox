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
constexpr int kTrackParams = 48; // 0-11 sound, 12-15 level/pan/delay/reverb, 16-23 inserts, 24-39 sound page 2

enum MachineId {
    M_BD = 0, M_SD, M_HH, M_CP, M_FM, M_CB,
    M_PMDRUM, M_PMCYM, M_PMBAR,
    M_SLICER, M_SYNTH,
    M_GRAIN, M_FMSYN, M_STRING,
    M_CHIP, M_KSDRUM, M_VOX,
    M_WEST, M_PD, M_FORMANT,
    M_ONESHOT, M_VOICE,
    M_FM4, M_ANALOG,
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
        int note = -1; bool gated = false; float gateLeft = -1, relStep = 0;
    };
    static constexpr int NV = 4;
    V v[NV];
    SVF fl, fr;
    float lastCut = -1, lastRes = -1;
    int ctr = 0;
public:
    SampleSlot *slot = nullptr;
    bool oneShot = false;   // Sampler mode: whole sample, step note transposes (60 = original pitch)
    void allOff() override { for (auto &x : v) x.on = false; }
    void trigger(const TrigParams &t) override {
        for (int k = 0; k < t.nnotes; ++k) triggerOne(t, t.notes[k], k == 0);
    }
    void triggerOne(const TrigParams &t, int note, bool first) {
        if (!slot || !slot->sample) return;
        const Sample *smp = slot->sample;
        const float *p = t.p;
        int cnt = std::max(1, slot->sliceCount);
        int si = oneShot ? 0 : ((note % cnt) + cnt) % cnt;
        int s0 = oneShot ? 0 : slot->sliceStart[si];
        int s1 = (!oneShot && si + 1 < cnt) ? slot->sliceStart[si + 1] : smp->frames();
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
        float semis = (p[0] - 0.5f) * 48.f + (oneShot ? (float)(note - 60) : 0.f);
        n.inc = std::pow(2.0, semis / 12.0) * smp->sr / sr;
        n.amp = t.vel * expo(p[9], 0.1f, 2.f);
        n.ampC = p[3] >= 0.99f ? 1.f : decayCoef(expo(p[3], 0.02f, 4.f), sr);
        n.fade = 1.f; n.fadeStep = 0.f; n.age = 0; n.attack = 0;
        n.note = note; n.gated = p[10] > 0.5f; n.gateLeft = n.gated ? t.gateSamples : -1.f;
        n.relStep = 1.f / (expo(p[11], 0.005f, 1.f) * sr);
    }
    // gate mode: releasing a held pad fades the slice out
    void noteOff(int note) override {
        for (auto &x : v) if (x.on && x.gated && x.note == note && x.gateLeft < 0.f && x.fadeStep == 0.f) x.fadeStep = x.relStep;
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
                if (x.gated && x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.fadeStep == 0.f) x.fadeStep = x.relStep; }
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

// ================================================================ v0.3 machines
template <class V, int N>
V *allocVoice(V (&v)[N]) {
    V *nv = nullptr;
    for (auto &x : v) if (x.env.stage == 0) { nv = &x; break; }
    if (!nv) for (auto &x : v) if (x.env.stage == 3 && (!nv || x.age > nv->age)) nv = &x;
    if (!nv) { nv = &v[0]; for (auto &x : v) if (x.age > nv->age) nv = &x; }
    return nv;
}
#define POLY_COMMON_TRIGGER \
    void trigger(const TrigParams &t) override { \
        for (int k = 0; k < t.nnotes; ++k) if (t.notes[k] >= 0) start(std::clamp(t.notes[k], 0, 127), t.vel, t.gateSamples); } \
    void noteOff(int note) override { \
        for (auto &x : v) if (x.note == note && x.env.stage && x.env.stage < 3 && x.gateLeft < 0) x.env.stage = 3; } \
    void allOff() override { for (auto &x : v) if (x.env.stage) x.env.stage = 3; }

// shared vowel formant filter (a e i o u)
struct Formants {
    SVF f[3];
    static constexpr float kVow[5][3] = {{650, 1080, 2650}, {400, 1700, 2600}, {290, 1870, 2800}, {400, 800, 2600}, {350, 600, 2700}};
    static constexpr float kGain[3] = {1.f, 0.6f, 0.35f};
    void set(float vowel, float shift, float q, float sr) {
        float pos = clampf(vowel, 0.f, 1.f) * 3.999f;
        int i = (int)pos; float fr = pos - i;
        for (int k = 0; k < 3; ++k) {
            float hz = lerpf(kVow[i][k], kVow[i + 1][k], fr) * shift;
            f[k].set(hz, q * (1.f + k * 0.3f), sr);
        }
    }
    inline float tick(float x) {
        float o = 0.f;
        for (int k = 0; k < 3; ++k) { float l, b, h; f[k].proc(x, l, b, h); o += b * f[k].k * kGain[k]; }
        return o;
    }
};
constexpr float Formants::kVow[5][3];
constexpr float Formants::kGain[3];

// ---------------------------------------------------------------- Chip percussion (8-bit)
class ChipPerc : public Machine {
    float ph = 0, f0 = 200, sweep = 0, pEnv = 0, pC = 0, amp = 0, ampC = 0, noiseMix = 0, duty = 0.5f;
    float nph = 0, levels = 64, held = 0; int holdN = 1, holdC = 0; bool shortMode = false;
    uint16_t lfsr = 1;
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        f0 = expo(p[0], 40.f, 2000.f);
        ampC = decayCoef(expo(p[1], 0.02f, 1.5f), sr);
        sweep = (p[2] - 0.5f) * 8.f;          // octaves, bipolar
        pC = decayCoef(expo(p[3], 0.005f, 0.5f), sr);
        noiseMix = p[4];
        shortMode = p[5] > 0.5f;
        levels = std::pow(2.f, 1.f + (1.f - p[6]) * 11.f);
        holdN = 1 + (int)(p[7] * 31.f);
        duty = 0.125f + p[8] * 0.375f;
        amp = t.vel; pEnv = 1.f; ph = 0.f; lfsr = 0x7fff;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-4f) return;
        for (int i = 0; i < n; ++i) {
            float f = f0 * std::pow(2.f, sweep * pEnv);
            ph += f / sr; ph -= std::floor(ph);
            float sq = ph < duty ? 1.f : -1.f;
            nph += f * 8.f / sr;
            while (nph >= 1.f) {
                nph -= 1.f;
                uint16_t bit = (lfsr ^ (lfsr >> (shortMode ? 6 : 1))) & 1;
                lfsr = (uint16_t)((lfsr >> 1) | (bit << 14));
            }
            float nz = (lfsr & 1) ? 1.f : -1.f;
            float s = (sq * (1.f - noiseMix) + nz * noiseMix) * amp;
            if (++holdC >= holdN) { holdC = 0; held = std::round(s * levels) / levels; }
            l[i] += held * 0.55f; r[i] += held * 0.55f;
            amp *= ampC; pEnv *= pC;
        }
    }
};

// ---------------------------------------------------------------- Karplus-Strong drum
class KSDrum : public Machine {
    static constexpr int BUF = 4096;
    float buf[BUF] = {0}; int w = 0;
    float len = 100, g = 0.99f, blend = 0.5f, z = 0, d = 0.5f, bend = 0, bendEnv = 0, bendC = 0, drive = 0, level = 0;
    bool on = false; Rng rng{1357};
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        float f = expo(p[0], 40.f, 900.f);
        len = clampf(sr / f, 4.f, BUF / 2.f);
        g = std::pow(10.f, -3.f / (expo(p[1], 0.05f, 3.f) * f));
        blend = p[2];
        float a = expo(p[3], 0.03f, 1.f), lp = 0.f;
        d = 0.15f + p[4] * 0.8f;
        bend = p[5] * 0.8f; bendEnv = 1.f; bendC = decayCoef(expo(p[1], 0.05f, 2.f), sr);
        drive = p[6];
        int N = (int)(len * (1.f + bend)) + 2;
        for (int i = 0; i < N; ++i) { lp += (rng.next() - lp) * a; buf[(w - N + i + BUF) % BUF] = lp * t.vel; }
        on = true; level = 1.f;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (!on) return;
        const float gg = 1.f + drive * 5.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        for (int i = 0; i < n; ++i) {
            float cl = len * (1.f + bend * (1.f - bendEnv));
            bendEnv *= bendC;
            float rp = (float)w - cl; if (rp < 0) rp += BUF;
            int i0 = (int)rp; float fr = rp - i0; int i1 = (i0 + 1) % BUF;
            float y = buf[i0] + (buf[i1] - buf[i0]) * fr;
            z = y * (1.f - d) + z * d;
            float sgn = rng.uni() < blend ? 1.f : -1.f;
            buf[w] = sgn * z * g; w = (w + 1) % BUF;
            level = level * 0.999f + std::fabs(y) * 0.001f;
            float s = softclip(y * 1.2f * gg) / gn;
            l[i] += s; r[i] += s;
        }
        if (level < 1e-5f) { on = false; std::memset(buf, 0, sizeof(buf)); }
    }
};

// ---------------------------------------------------------------- Vox percussion (formant bursts)
class VoxPerc : public Machine {
    Formants fm; Rng rng{8642};
    float amp = 0, ampC = 0, f0 = 120, ph = 0, breath = 0.5f, v0 = 0, v1 = 0, shift = 1, q = 6, env = 0, drive = 0;
    int ctr = 0;
public:
    void trigger(const TrigParams &t) override {
        const float *p = t.p;
        v0 = p[0];
        ampC = decayCoef(expo(p[1], 0.03f, 1.2f), sr);
        f0 = expo(p[2], 60.f, 600.f);
        breath = p[3];
        shift = expo(p[4], 0.6f, 1.8f);
        v1 = clampf(v0 + (p[5] - 0.5f) * 2.f, 0.f, 1.f);
        q = 2.f + p[6] * 14.f;
        drive = p[7];
        amp = t.vel; env = 1.f; ctr = 0;
    }
    void render(float *l, float *r, int n, const float *) override {
        if (amp < 1e-4f) return;
        const float gg = 1.f + drive * 5.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        for (int i = 0; i < n; ++i) {
            if ((ctr++ & 15) == 0) fm.set(lerpf(v1, v0, env), shift, q, sr);
            ph += f0 / sr; float buzz = 0.f;
            if (ph >= 1.f) { ph -= 1.f; buzz = 1.f; }
            float x = buzz * 4.f * (1.f - breath) + rng.next() * breath;
            float s = fm.tick(x) * amp * 4.5f;
            s = softclip(s * gg) / gn;
            l[i] += s; r[i] += s;
            amp *= ampC; env = amp;
        }
    }
};

// ---------------------------------------------------------------- West Coast (FM + wavefolder + low-pass gate)
class WestCoast : public Machine {
    struct V { int note = -1; ADSR env; float vel = 0, gateLeft = -1, freq = 0, cph = 0, mph = 0; int age = 0; SVF lpg; };
    static constexpr int NV = 8;
    V v[NV]; float lfo = 0; int ctr = 0;
    static constexpr float kRatios[12] = {0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f, 5.f, 6.f, 7.f, 9.f};
    void start(int note, float vel, float gate) {
        V *x = allocVoice(v);
        x->note = note; x->vel = vel; x->gateLeft = gate; x->env.stage = 1; x->age = 0; x->freq = mtof((float)note);
        if (x->env.env < 0.001f) { x->cph = x->mph = 0; x->lpg.reset(); }
    }
public:
    POLY_COMMON_TRIGGER
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false; for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        const float ratio = kRatios[std::clamp((int)(p[0] * 11.99f), 0, 11)];
        const float idx = p[1] * 5.f, fold = p[2], sym = (p[3] - 0.5f) * 1.5f, foldEnv = p[4];
        const float vib = p[5] * 0.6f, q = 0.5f + p[10] * 6.f;
        const float drive = p[11], gg = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        for (auto &x : v) if (x.env.stage) x.env.set(expo(p[6], 0.001f, 3.f), expo(p[7], 0.02f, 4.f), p[8], expo(p[9], 0.02f, 5.f), sr);
        for (int i = 0; i < n; ++i) {
            lfo += 5.f / sr; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            const bool upd = (ctr++ & 15) == 0;
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) x.env.stage = 3; }
                float e = x.env.tick();
                if (!x.env.stage) continue;
                float f = x.freq * vm / sr;
                x.mph += f * ratio; x.mph -= std::floor(x.mph);
                x.cph += f; x.cph -= std::floor(x.cph);
                float c = std::sin(kTwoPi * x.cph + std::sin(kTwoPi * x.mph) * idx);
                float a = 1.f + (fold + foldEnv * e) * 8.f;
                float y = std::sin((c + sym) * a * kPi * 0.5f);
                if (upd) x.lpg.set(30.f * std::pow(2.f, e * 9.5f), q, sr);
                out += x.lpg.lp(y) * e * x.vel * 0.35f;
            }
            out = softclip(out * gg) / gn;
            l[i] += out; r[i] += out;
        }
    }
};
constexpr float WestCoast::kRatios[12];

// ---------------------------------------------------------------- Phase distortion (CZ style)
class PhaseDist : public Machine {
    struct V { int note = -1; ADSR env; float vel = 0, gateLeft = -1, freq = 0, p1 = 0, p2 = 0, dcw = 0; int age = 0; };
    static constexpr int NV = 8;
    V v[NV]; float lfo = 0;
    void start(int note, float vel, float gate) {
        V *x = allocVoice(v);
        x->note = note; x->vel = vel; x->gateLeft = gate; x->env.stage = 1; x->age = 0; x->freq = mtof((float)note); x->dcw = 1.f;
        if (x->env.env < 0.001f) { x->p1 = 0; x->p2 = 0.25f; }
    }
    static inline float pd(float p, int wave, float a, float reso) {
        if (wave == 0) { // saw
            float k = 0.5f - a * 0.49f;
            float w = p < k ? p * 0.5f / k : 0.5f + (p - k) * 0.5f / (1.f - k);
            return -std::cos(kTwoPi * w);
        } else if (wave == 1) { // square-ish
            float d = 0.5f - a * 0.48f, w;
            if (p < d) w = p / d * 0.5f; else if (p < 0.5f) w = 0.5f;
            else if (p < 0.5f + d) w = 0.5f + (p - 0.5f) / d * 0.5f; else w = 1.f;
            return -std::cos(kTwoPi * w);
        }
        // resonant: windowed cosine at a higher ratio
        float ratio = 1.f + a * reso;
        return (1.f - p) * std::cos(kTwoPi * p * ratio) * 1.4f;
    }
public:
    POLY_COMMON_TRIGGER
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false; for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        const int wave = std::clamp((int)(p[0] * 2.99f), 0, 2);
        const float base = p[1], envAmt = p[2];
        const float dC = decayCoef(expo(p[3], 0.02f, 4.f), sr);
        const float reso = 1.f + p[4] * 15.f;
        const float det = p[5] * 20.f, m1 = std::pow(2.f, -det / 1200.f), m2 = std::pow(2.f, det / 1200.f);
        const float vib = p[10] * 0.6f;
        const float drive = p[11], gg = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        for (auto &x : v) if (x.env.stage) x.env.set(expo(p[6], 0.001f, 3.f), expo(p[7], 0.02f, 4.f), p[8], expo(p[9], 0.02f, 5.f), sr);
        for (int i = 0; i < n; ++i) {
            lfo += 5.f / sr; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) x.env.stage = 3; }
                float e = x.env.tick();
                if (!x.env.stage) continue;
                float a = clampf(base + envAmt * x.dcw, 0.f, 0.98f);
                x.dcw *= dC;
                float f = x.freq * vm / sr;
                x.p1 += f * m1; x.p1 -= std::floor(x.p1);
                x.p2 += f * m2; x.p2 -= std::floor(x.p2);
                float s = (pd(x.p1, wave, a, reso) + pd(x.p2, wave, a, reso)) * 0.5f;
                out += s * e * x.vel * 0.3f;
            }
            out = softclip(out * gg) / gn;
            l[i] += out; r[i] += out;
        }
    }
};

// ---------------------------------------------------------------- Formant (vowel) synth
class FormantSynth : public Machine {
    struct V { int note = -1; ADSR env; float vel = 0, gateLeft = -1, freq = 0, ph1 = 0, ph2 = 0.3f; int age = 0; Formants fm; };
    static constexpr int NV = 8;
    V v[NV]; float lfo = 0; int ctr = 0; Rng rng{97531};
    void start(int note, float vel, float gate) {
        V *x = allocVoice(v);
        x->note = note; x->vel = vel; x->gateLeft = gate; x->env.stage = 1; x->age = 0; x->freq = mtof((float)note);
    }
public:
    POLY_COMMON_TRIGGER
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false; for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        const float vowel = p[0], vEnv = (p[1] - 0.5f) * 2.f, shift = expo(p[2], 0.6f, 1.8f), q = 2.f + p[3] * 16.f;
        const float breath = p[4];
        const float det = p[5] * 20.f, m1 = std::pow(2.f, -det / 1200.f), m2 = std::pow(2.f, det / 1200.f);
        const float vib = p[10] * 0.6f;
        const float drive = p[11], gg = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        for (auto &x : v) if (x.env.stage) x.env.set(expo(p[6], 0.001f, 3.f), expo(p[7], 0.02f, 4.f), p[8], expo(p[9], 0.02f, 5.f), sr);
        for (int i = 0; i < n; ++i) {
            lfo += 5.f / sr; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            const bool upd = (ctr++ & 15) == 0;
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) x.env.stage = 3; }
                float e = x.env.tick();
                if (!x.env.stage) continue;
                if (upd) x.fm.set(clampf(vowel + vEnv * e, 0.f, 1.f), shift, q, sr);
                float dt = x.freq * vm / sr;
                x.ph1 += dt * m1; if (x.ph1 >= 1.f) x.ph1 -= 1.f;
                x.ph2 += dt * m2; if (x.ph2 >= 1.f) x.ph2 -= 1.f;
                float saw = (2.f * x.ph1 - 1.f - polyblep(x.ph1, dt * m1)) + (2.f * x.ph2 - 1.f - polyblep(x.ph2, dt * m2));
                float exc = saw * 0.5f * (1.f - breath) + rng.next() * breath;
                out += x.fm.tick(exc) * e * x.vel * 1.1f;
            }
            out = softclip(out * gg) / gn;
            l[i] += out; r[i] += out;
        }
    }
};

// ---------------------------------------------------------------- PM Voice: vocal tract physical model
// Kelly-Lochbaum waveguide tube (after Pink Trombone) excited by a Rosenberg glottal pulse + aspiration.
class PMVoice : public Machine {
    static constexpr int NMAX = 30;
    struct V {
        int note = -1; ADSR env; float vel = 0, gateLeft = -1, freq = 100, ph = 0; int age = 0;
        float R[NMAX] = {0}, L[NMAX] = {0}, jR[NMAX + 1] = {0}, jL[NMAX + 1] = {0};
        float lastFlow = 0, jit = 0, jitT = 0, out = 0, dc = 0;
    };
    static constexpr int NV = 8;
    V v[NV];
    float refl[NMAX + 1] = {0}, diam[NMAX] = {0};
    int N = 22; float lfo = 0; Rng rng{3141}; float tractGain = 2.f;
    float lastShape[3] = {-1, -1, -1};
    // vowel area functions (cm^2), glottis -> lips, 10 control points; tuned so the tube's
    // first two resonances match measured vowel formants (a 730/1090, e 530/1840, i 270/2290, o 570/840, u 300/870 Hz)
    static constexpr float kArea[5][10] = {
        {0.64f, 0.33f, 0.16f, 0.58f, 2.48f, 6.15f, 7.65f, 5.57f, 1.79f, 2.92f},   // a
        {0.8f, 2.4f, 4.0f, 4.4f, 4.0f, 3.0f, 1.8f, 1.1f, 1.2f, 2.2f},   // e
        {0.9f, 3.2f, 6.0f, 7.0f, 6.0f, 3.6f, 1.4f, 0.45f, 0.35f, 1.6f},   // i
        {1.56f, 1.6f, 2.0f, 1.3f, 0.78f, 0.48f, 2.84f, 8.79f, 7.48f, 0.45f},   // o
        {0.76f, 3.0f, 1.97f, 1.46f, 1.36f, 0.18f, 3.5f, 4.36f, 2.29f, 0.15f},   // u
    };
    void start(int note, float vel, float gate) {
        V *x = allocVoice(v);
        x->note = note; x->vel = vel; x->gateLeft = gate; x->env.stage = 1; x->age = 0; x->freq = mtof((float)note);
        if (x->env.env < 0.001f) { std::memset(x->R, 0, sizeof(x->R)); std::memset(x->L, 0, sizeof(x->L)); x->ph = 0; }
    }
    void shapeTract(float vowel, float lips, float throat) {
        N = std::clamp(17 + (int)(throat * 11.f), 17, NMAX);          // tract length: bigger = lower formants
        float pos = clampf(vowel, 0.f, 1.f) * 3.999f; int v0 = (int)pos; float vf = pos - v0;
        for (int i = 0; i < N; ++i) {
            float cp = (float)i / (N - 1) * 9.f; int c0 = std::min(8, (int)cp); float cf = cp - c0;
            float a0 = lerpf(kArea[v0][c0], kArea[v0][c0 + 1], cf), a1 = lerpf(kArea[v0 + 1][c0], kArea[v0 + 1][c0 + 1], cf);
            float area = lerpf(a0, a1, vf);
            if (i >= N - 2) area *= (1.f - lips * 0.92f);               // lip rounding / closing
            diam[i] = std::sqrt(std::max(0.03f, area));
        }
        for (int i = 1; i < N; ++i) {
            float a0 = diam[i - 1] * diam[i - 1], a1 = diam[i] * diam[i];
            refl[i] = (a0 - a1) / (a0 + a1 + 1e-6f);
        }
        // rounded / closed lips radiate less sound: compensate so every vowel sits at a similar level
        float lipArea = diam[N - 1] * diam[N - 1];
        tractGain = 2.2f * clampf(std::sqrt(2.9f / std::max(0.03f, lipArea)), 1.f, 5.f);
    }
    static inline float glottal(float t, float oq) {
        // Rosenberg glottal flow: rise to 0.6*oq, fall to oq, closed after
        float a = 0.6f * oq;
        if (t < a) return 0.5f * (1.f - std::cos(kPi * t / a));
        if (t < oq) return std::cos(kPi * 0.5f * (t - a) / (oq - a));
        return 0.f;
    }
public:
    POLY_COMMON_TRIGGER
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false; for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        if (p[0] != lastShape[0] || p[1] != lastShape[1] || p[2] != lastShape[2]) {
            shapeTract(p[0], p[1], p[2]); lastShape[0] = p[0]; lastShape[1] = p[1]; lastShape[2] = p[2];
        }
        const float oq = 0.85f - p[3] * 0.5f;           // tension: tighter folds = shorter open phase, brighter
        const float breath = p[4] * 0.6f;
        const float vib = p[5] * 0.7f;
        const float jitAmt = p[10] * 0.6f;
        const float drive = p[11], gg = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        for (auto &x : v) if (x.env.stage) x.env.set(expo(p[6], 0.005f, 3.f), expo(p[7], 0.02f, 4.f), p[8], expo(p[9], 0.02f, 5.f), sr);
        const int NN = N;
        for (int i = 0; i < n; ++i) {
            lfo += 5.3f / sr; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f);
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) x.env.stage = 3; }
                float e = x.env.tick();
                if (!x.env.stage) continue;
                // slow random pitch wander (human jitter)
                if (--x.jitT <= 0.f) { x.jitT = sr * 0.05f; x.jit = rng.next() * jitAmt; }
                float f = x.freq * vm * std::pow(2.f, x.jit * 0.1f);
                x.ph += f / sr; if (x.ph >= 1.f) x.ph -= 1.f;
                float flow = glottal(x.ph, oq);
                float dflow = (flow - x.lastFlow) * 8.f; x.lastFlow = flow;     // flow derivative drives the tract
                float asp = rng.next() * breath * (0.3f + flow);
                float g = (dflow + asp) * e;
                // waveguide junctions
                x.jR[0] = x.L[0] * 0.75f + g;
                x.jL[NN] = x.R[NN - 1] * -0.85f;
                for (int k = 1; k < NN; ++k) {
                    float w = refl[k] * (x.R[k - 1] + x.L[k]);
                    x.jR[k] = x.R[k - 1] - w;
                    x.jL[k] = x.L[k] + w;
                }
                for (int k = 0; k < NN; ++k) { x.R[k] = x.jR[k] * 0.999f; x.L[k] = x.jL[k + 1] * 0.999f; }
                float o = x.R[NN - 1];
                x.dc += (o - x.dc) * 0.002f;
                out += (o - x.dc) * x.vel * 0.5f * tractGain;
            }
            out = softclip(out * gg) / gn;
            l[i] += out; r[i] += out;
        }
    }
};
constexpr float PMVoice::kArea[5][10];

// ---------------------------------------------------------------- FM4: 4-operator FM (DX style)
// page 1: 0 Algo 1 Feedback 2..9 Op1..4 Ratio/Level 10 Vibrato 11 Drive
// page 2: 24..31 Op1..4 Decay/Sustain 32 Attack 33 Release 34 Detune 35 VelSens 36 Cutoff 37 Reso 38 FEnv 39 FEnvDec
class FM4 : public Machine {
    struct V { int note = -1; ADSR op[4]; ADSR env; float vel = 0, gateLeft = -1, freq = 0, ph[4] = {0, 0, 0, 0}, fb1 = 0, fb2 = 0, fenv = 0; int age = 0; SVF f; };
    static constexpr int NV = 8;
    V v[NV]; float lfo = 0; int ctr = 0;
    static constexpr float kRatio[15] = {0.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f, 14.f, 16.f};
    // algorithm: for each op (0..3) a bitmask of the ops that modulate it, plus a carrier mask
    static constexpr uint8_t kMod[8][4] = {
        {0x2, 0x4, 0x8, 0}, {0x2, 0xC, 0, 0}, {0x6, 0x8, 0, 0}, {0x2, 0, 0x8, 0},
        {0x8, 0x8, 0x8, 0}, {0x2, 0, 0, 0}, {0, 0x4, 0x8, 0}, {0, 0, 0, 0}};
    static constexpr uint8_t kCar[8] = {0x1, 0x1, 0x1, 0x5, 0x7, 0xD, 0x3, 0xF};
    void start(int note, float vel, float gate) {
        V *x = allocVoice(v);
        x->note = note; x->vel = vel; x->gateLeft = gate; x->env.stage = 1; x->age = 0; x->freq = mtof((float)note); x->fenv = 1.f;
        for (auto &o : x->op) o.stage = 1;
        if (x->env.env < 0.001f) { for (float &p : x->ph) p = 0; x->fb1 = x->fb2 = 0; x->f.reset(); for (auto &o : x->op) o.env = 0; }
    }
public:
    void trigger(const TrigParams &t) override {
        for (int k = 0; k < t.nnotes; ++k) if (t.notes[k] >= 0) start(std::clamp(t.notes[k], 0, 127), t.vel, t.gateSamples);
    }
    void noteOff(int note) override {
        for (auto &x : v) if (x.note == note && x.env.stage && x.env.stage < 3 && x.gateLeft < 0) { x.env.stage = 3; for (auto &o : x.op) if (o.stage) o.stage = 3; }
    }
    void allOff() override { for (auto &x : v) if (x.env.stage) { x.env.stage = 3; for (auto &o : x.op) if (o.stage) o.stage = 3; } }
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false; for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        const int algo = std::clamp((int)(p[0] * 7.99f), 0, 7);
        const float fb = p[1] * 1.2f;
        float ratio[4], lvl[4];
        for (int k = 0; k < 4; ++k) { ratio[k] = kRatio[std::clamp((int)(p[2 + k * 2] * 14.99f), 0, 14)]; float lv = p[3 + k * 2]; lvl[k] = lv * lv; }
        const float det = p[34] * 12.f;   // cents spread over ops 2..4
        float dm[4] = {1.f, std::pow(2.f, det / 1200.f), std::pow(2.f, -det / 1200.f), std::pow(2.f, det * 0.5f / 1200.f)};
        const float vib = p[10] * 0.6f, velS = p[35];
        const float drive = p[11], gg = 1.f + drive * 4.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        const bool filt = p[36] < 0.99f;
        const float cut = expo(p[36], 60.f, 18000.f), q = 0.5f + p[37] * 8.f, fea = p[38] * 6.f;
        const float feC = decayCoef(expo(p[39], 0.01f, 3.f), sr);
        const float atk = expo(p[32], 0.001f, 3.f), rel = expo(p[33], 0.01f, 6.f);
        const uint8_t car = kCar[algo];
        int ncar = 0; for (int k = 0; k < 4; ++k) ncar += (car >> k) & 1;
        const float carNorm = 0.4f / std::sqrt((float)ncar);
        for (auto &x : v) if (x.env.stage) {
            for (int k = 0; k < 4; ++k) x.op[k].set(atk, expo(p[24 + k * 2], 0.01f, 6.f), p[25 + k * 2], rel, sr);
            x.env.set(atk, 6.f, 1.f, rel, sr);
        }
        for (int i = 0; i < n; ++i) {
            lfo += 5.2f / sr; if (lfo >= 1.f) lfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            const bool upd = (ctr++ & 15) == 0;
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) { x.env.stage = 3; for (auto &o : x.op) if (o.stage) o.stage = 3; } }
                x.env.tick();
                float e[4], o[4];
                bool alive = false;
                for (int k = 0; k < 4; ++k) { e[k] = x.op[k].tick(); if (((car >> k) & 1) && x.op[k].stage) alive = true; }
                if (!alive) { x.env.stage = 0; continue; }
                const float velMod = 1.f - velS + velS * x.vel;
                const float f = x.freq * vm / sr;
                for (int k = 3; k >= 0; --k) {
                    float m = 0.f;
                    for (int j = k + 1; j < 4; ++j) if ((kMod[algo][k] >> j) & 1) m += o[j];
                    if (k == 3) m += fb * (x.fb1 + x.fb2) * 0.5f;
                    x.ph[k] += f * ratio[k] * dm[k]; x.ph[k] -= std::floor(x.ph[k]);
                    float s = fsin2pi(x.ph[k] + m * 0.63662f);   // = sin(2pi*ph + 4m)
                    bool isCar = (car >> k) & 1;
                    o[k] = s * e[k] * lvl[k] * (isCar ? 1.f : velMod);
                    if (k == 3) { x.fb2 = x.fb1; x.fb1 = o[3]; }
                }
                float sum = 0.f;
                for (int k = 0; k < 4; ++k) if ((car >> k) & 1) sum += o[k];
                sum *= carNorm * x.vel;
                if (filt) {
                    if (upd) x.f.set(cut * std::pow(2.f, fea * x.fenv), q, sr);
                    sum = x.f.lp(sum);
                    x.fenv *= feC;
                }
                out += sum;
            }
            out = softclip(out * gg) / gn;
            l[i] += out; r[i] += out;
        }
    }
};
constexpr float FM4::kRatio[15];
constexpr uint8_t FM4::kMod[8][4];
constexpr uint8_t FM4::kCar[8];

// ---------------------------------------------------------------- Analog: dual-oscillator VA with filter + amp envelopes
// page 1: 0 Osc1 Wave 1 Osc2 Wave 2 Osc2 Tune 3 Osc2 Fine 4 Osc Mix 5 Sub 6 Noise 7 Cutoff 8 Reso 9 FEnv(bipolar) 10 Drive 11 Glide
// page 2: 24-27 Filter ADSR, 28-31 Amp ADSR, 32 FiltType 33 KeyTrack 34 PWM 35 Sync 36 Unison 37 Vel>Filter 38 Vel>Amp 39 Vibrato
class Analog : public Machine {
    struct V {
        int note = -1; ADSR fenv, env; float vel = 0, gateLeft = -1, freq = 0, target = 0;
        float p1[3] = {0, 0.33f, 0.66f}, p2 = 0, ps = 0; int age = 0; SVF f1, f2; float tri1[3] = {0, 0, 0}, tri2 = 0;
    };
    static constexpr int NV = 8;
    V v[NV]; float lfo = 0, pwmLfo = 0, lastFreq = 0; int ctr = 0; Rng rng{5551};
    void start(int note, float vel, float gate) {
        V *x = allocVoice(v);
        x->note = note; x->vel = vel; x->gateLeft = gate; x->age = 0;
        x->target = mtof((float)note);
        x->freq = (glide > 0.f && lastFreq > 0.f) ? lastFreq : x->target;
        lastFreq = x->target;
        if (x->env.env < 0.001f) { x->f1.reset(); x->f2.reset(); x->p2 = 0; x->ps = 0; x->fenv.env = 0; }
        x->env.stage = 1; x->fenv.stage = 1;
    }
    float glide = 0.f;
    // morphing oscillator: saw -> square (pulse width pw) -> triangle -> sine
    static inline float shape(float ph, float dt, float w, float pw, float &triState) {
        float seg = clampf(w, 0.f, 1.f) * 3.f;
        float saw = 2.f * ph - 1.f - polyblep(ph, dt);
        float p2 = ph + (1.f - pw); if (p2 >= 1.f) p2 -= 1.f;
        float sq = (ph < pw ? 1.f : -1.f) + polyblep(ph, dt) - polyblep(p2, dt);
        if (seg < 1.f) return saw + (sq - saw) * seg;
        // leaky-integrated square = band-limited triangle
        triState = triState * 0.999f + sq * dt * 4.f;
        float tri = triState;
        if (seg < 2.f) return sq + (tri - sq) * (seg - 1.f);
        float sn = std::sin(kTwoPi * ph);
        return tri + (sn - tri) * (seg - 2.f);
    }
public:
    void trigger(const TrigParams &t) override {
        glide = t.p[11];
        for (int k = 0; k < t.nnotes; ++k) if (t.notes[k] >= 0) start(std::clamp(t.notes[k], 0, 127), t.vel, t.gateSamples);
    }
    void noteOff(int note) override {
        for (auto &x : v) if (x.note == note && x.env.stage && x.env.stage < 3 && x.gateLeft < 0) { x.env.stage = 3; x.fenv.stage = 3; }
    }
    void allOff() override { for (auto &x : v) if (x.env.stage) { x.env.stage = 3; x.fenv.stage = 3; } }
    void render(float *l, float *r, int n, const float *p) override {
        bool any = false; for (auto &x : v) any |= x.env.stage != 0;
        if (!any) return;
        glide = p[11];
        const float w1 = p[0], w2 = p[1];
        const float semi2 = std::round((p[2] - 0.5f) * 48.f), fine2 = (p[3] - 0.5f) * 100.f;
        const float m2 = std::pow(2.f, (semi2 * 100.f + fine2) / 1200.f);
        const float mix = p[4], sub = p[5], noise = p[6] * 0.7f;
        const float cut = expo(p[7], 30.f, 18000.f), q = 0.5f + p[8] * 9.f, fAmt = (p[9] - 0.5f) * 2.f * 7.f;
        const float drive = p[10], gg = 1.f + drive * 5.f, gn = drive > 0.f ? softclip(gg) : 1.f;
        const float glideC = glide > 0.001f ? 1.f - std::exp(-1.f / (expo(glide, 0.005f, 2.f) * sr)) : 1.f;
        const int ftype = std::clamp((int)(p[32] * 3.99f), 0, 3);   // 0 LP12 1 LP24 2 BP 3 HP
        const float keyT = p[33];
        const float pwmAmt = p[34];
        const bool sync = p[35] > 0.5f;
        const int uni = 1 + std::clamp((int)(p[36] * 2.99f), 0, 2);
        const float uniDet = p[36] * 0.012f;
        const float velF = p[37] * 3.f, velA = p[38];
        const float vib = p[39] * 0.6f;
        for (auto &x : v) if (x.env.stage) {
            x.fenv.set(expo(p[24], 0.001f, 4.f), expo(p[25], 0.01f, 6.f), p[26], expo(p[27], 0.01f, 6.f), sr);
            x.env.set(expo(p[28], 0.001f, 4.f), expo(p[29], 0.01f, 6.f), p[30], expo(p[31], 0.01f, 8.f), sr);
        }
        const float uniNorm = 1.f / std::sqrt((float)uni);
        for (int i = 0; i < n; ++i) {
            lfo += 5.f / sr; if (lfo >= 1.f) lfo -= 1.f;
            pwmLfo += 0.6f / sr; if (pwmLfo >= 1.f) pwmLfo -= 1.f;
            const float vm = vib > 0.f ? std::pow(2.f, vib * std::sin(kTwoPi * lfo) / 12.f) : 1.f;
            const float pw = 0.5f - pwmAmt * 0.42f * (0.5f + 0.5f * std::sin(kTwoPi * pwmLfo));
            const bool upd = (ctr++ & 15) == 0;
            float out = 0.f;
            for (auto &x : v) {
                if (!x.env.stage) continue;
                ++x.age;
                if (x.gateLeft >= 0.f) { x.gateLeft -= 1.f; if (x.gateLeft < 0.f && x.env.stage < 3) { x.env.stage = 3; x.fenv.stage = 3; } }
                float ae = x.env.tick(), fe = x.fenv.tick();
                if (!x.env.stage) continue;
                x.freq += (x.target - x.freq) * glideC;
                const float base = x.freq * vm / sr;
                // osc 1 (+ unison copies)
                float o1 = 0.f; bool wrapped = false;
                for (int u = 0; u < uni; ++u) {
                    float d = base * (1.f + (u == 0 ? 0.f : (u == 1 ? uniDet : -uniDet)));
                    x.p1[u] += d; if (x.p1[u] >= 1.f) { x.p1[u] -= 1.f; if (u == 0) wrapped = true; }
                    o1 += shape(x.p1[u], d, w1, pw, x.tri1[u]);
                }
                o1 *= uniNorm;
                // osc 2 (optionally hard-synced to osc 1)
                float d2 = base * m2;
                if (sync && wrapped) x.p2 = x.p1[0] * m2 - std::floor(x.p1[0] * m2);
                else { x.p2 += d2; if (x.p2 >= 1.f) x.p2 -= 1.f; }
                float o2 = shape(x.p2, d2, w2, pw, x.tri2);
                x.ps += base * 0.5f; if (x.ps >= 1.f) x.ps -= 1.f;
                float s = o1 * (1.f - mix) + o2 * mix + (x.ps < 0.5f ? 1.f : -1.f) * sub + rng.next() * noise;
                if (upd) {
                    float oct = fAmt * fe + keyT * (x.note - 60) / 12.f + velF * (x.vel - 0.5f);
                    float fc = cut * std::pow(2.f, oct);
                    x.f1.set(fc, ftype == 1 ? q * 0.7f : q, sr); x.f2.set(fc, 0.6f, sr);
                }
                float lo, bp, hi;
                x.f1.proc(s * 0.25f, lo, bp, hi);
                float y = ftype == 0 ? lo : ftype == 1 ? x.f2.lp(lo) : ftype == 2 ? bp * x.f1.k : hi;
                float va = 1.f - velA + velA * x.vel;
                out += y * ae * va;
            }
            out = softclip(out * gg) / gn;
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
        case M_CHIP: m = new ChipPerc(); break;
        case M_KSDRUM: m = new KSDrum(); break;
        case M_VOX: m = new VoxPerc(); break;
        case M_WEST: m = new WestCoast(); break;
        case M_PD: m = new PhaseDist(); break;
        case M_FORMANT: m = new FormantSynth(); break;
        case M_ONESHOT: { auto *s = new Slicer(); s->slot = slot; s->oneShot = true; m = s; break; }
        case M_VOICE: m = new PMVoice(); break;
        case M_FM4: m = new FM4(); break;
        case M_ANALOG: m = new Analog(); break;
        case M_SYNTH: default: m = new PolySynth(); break;
    }
    m->sr = sr;
    return m;
}
