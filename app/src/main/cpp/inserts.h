// TinyBox - per-track insert effects (2 slots per track)
// params (all 0..1): p[0..2] = type-specific controls, p[3] = mix
#pragma once
#include "dsp.h"
#include <vector>
#include <atomic>
#include <cstring>

enum InsertType {
    INS_OFF = 0, INS_FILTER, INS_DRIVE, INS_CRUSH, INS_CHORUS, INS_PHASER, INS_COMP, INS_EQ,
    INS_TREM, INS_ECHO, INS_RING, INS_FOLD, INS_TAPE, INS_WIDTH, INS_COUNT
};

struct Insert {
    std::atomic<int> type{0};
    int lastType = -1;
    float sr = 48000.f;
    SVF fl, fr, eqLoL, eqLoR, eqHiL, eqHiR;
    OnePole tl, tr;
    std::vector<float> bl, br; int w = 0;
    float lfo = 0.f, env = 0.f, crL = 0.f, crR = 0.f; int crC = 0;
    float apL[6] = {0}, apR[6] = {0}, fbL = 0.f, fbR = 0.f, ringPh = 0.f;
    int ctr = 0; float lastP[4] = {-1, -1, -1, -1};

    static constexpr int BUF = 32768; // ~0.68 s at 48 kHz

    void prepare(float s) {
        sr = s;
        bl.assign(BUF, 0.f); br.assign(BUF, 0.f);
        reset();
    }
    void reset() {
        fl.reset(); fr.reset(); eqLoL.reset(); eqLoR.reset(); eqHiL.reset(); eqHiR.reset();
        tl.z = tr.z = 0.f;
        std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f);
        w = 0; lfo = 0; env = 0; crL = crR = 0; crC = 0; fbL = fbR = 0; ringPh = 0;
        std::memset(apL, 0, sizeof(apL)); std::memset(apR, 0, sizeof(apR));
        for (float &x : lastP) x = -1.f;
    }
    inline float readBuf(const std::vector<float> &b, float d) const {
        float rp = (float)w - d; while (rp < 0) rp += BUF;
        int i0 = (int)rp & (BUF - 1); float f = rp - std::floor(rp); int i1 = (i0 + 1) & (BUF - 1);
        return b[i0] + (b[i1] - b[i0]) * f;
    }
    static inline float allpass(float x, float &z, float a) { float y = -a * x + z; z = x + a * y; return y; }

    void process(float *L, float *R, int n, const float *p) {
        const int t = type.load(std::memory_order_relaxed);
        if (t != lastType) { reset(); lastType = t; }
        if (t <= INS_OFF || t >= INS_COUNT) return;
        const float mix = clampf(p[3], 0.f, 1.f);
        if (mix <= 0.f) return;
        // coefficient updates (only when params moved)
        bool chg = p[0] != lastP[0] || p[1] != lastP[1] || p[2] != lastP[2];
        if (chg) {
            for (int i = 0; i < 3; ++i) lastP[i] = p[i];
            switch (t) {
                case INS_FILTER: { float fc = expo(p[0], 30.f, 18000.f), q = 0.6f + p[1] * 10.f; fl.set(fc, q, sr); fr.set(fc, q, sr); break; }
                case INS_DRIVE: tl.setLP(expo(p[1], 800.f, 18000.f), sr); tr.a = tl.a; break;
                case INS_ECHO: tl.setLP(expo(p[2], 600.f, 16000.f), sr); tr.a = tl.a; break;
                case INS_TAPE: tl.setLP(expo(p[2], 1500.f, 18000.f), sr); tr.a = tl.a; break;
                case INS_WIDTH: fl.set(150.f, 0.7f, sr); break;
                case INS_EQ: eqLoL.set(220.f, 0.6f, sr); eqLoR.set(220.f, 0.6f, sr); eqHiL.set(3500.f, 0.6f, sr); eqHiR.set(3500.f, 0.6f, sr); break;
                default: break;
            }
        }
        for (int i = 0; i < n; ++i) {
            const float dl = L[i], dr = R[i];
            float wl = dl, wr = dr;
            switch (t) {
                case INS_FILTER: {
                    float a, b, c;
                    fl.proc(dl, a, b, c); wl = p[2] < 0.33f ? a : (p[2] < 0.66f ? b * fl.k : c);
                    fr.proc(dr, a, b, c); wr = p[2] < 0.33f ? a : (p[2] < 0.66f ? b * fr.k : c);
                    break;
                }
                case INS_DRIVE: {
                    float g = 1.f + p[0] * 20.f, o = 0.2f + p[2] * 1.2f;
                    wl = tl.lp(softclip(dl * g)) * o; wr = tr.lp(softclip(dr * g)) * o;
                    break;
                }
                case INS_CRUSH: {
                    float levels = std::pow(2.f, 1.f + (1.f - p[0]) * 13.f);
                    int hold = 1 + (int)(p[1] * 31.f);
                    if (++crC >= hold) { crC = 0; crL = std::round(dl * levels) / levels; crR = std::round(dr * levels) / levels; }
                    wl = crL; wr = crR;
                    break;
                }
                case INS_CHORUS: {
                    lfo += expo(p[0], 0.05f, 5.f) / sr; if (lfo >= 1.f) lfo -= 1.f;
                    float base = (0.004f + p[2] * 0.02f) * sr, dep = p[1] * 0.004f * sr;
                    bl[w] = dl; br[w] = dr;
                    wl = readBuf(bl, base + dep * std::sin(kTwoPi * lfo));
                    wr = readBuf(br, base + dep * std::sin(kTwoPi * (lfo + 0.25f)));
                    w = (w + 1) & (BUF - 1);
                    break;
                }
                case INS_PHASER: {
                    lfo += expo(p[0], 0.03f, 6.f) / sr; if (lfo >= 1.f) lfo -= 1.f;
                    float m = 0.5f + 0.5f * std::sin(kTwoPi * lfo);
                    float fc = expo(0.15f + m * p[1] * 0.8f, 100.f, 8000.f);
                    float tn = std::tan(kPi * fc / sr), a = (1.f - tn) / (1.f + tn);
                    float fb = p[2] * 0.85f;
                    float xl = dl + fbL * fb, xr = dr + fbR * fb;
                    for (int k = 0; k < 6; ++k) { xl = allpass(xl, apL[k], a); xr = allpass(xr, apR[k], a); }
                    fbL = xl; fbR = xr;
                    wl = (dl + xl) * 0.7f; wr = (dr + xr) * 0.7f;
                    break;
                }
                case INS_COMP: {
                    float x = std::max(std::fabs(dl), std::fabs(dr));
                    env += (x - env) * (x > env ? 0.004f : 0.0002f);
                    float thr = -40.f + p[0] * 40.f, ratio = expo(p[1], 1.f, 20.f);
                    float lvl = 20.f * std::log10(env + 1e-9f), over = lvl - thr;
                    float gr = over > 0.f ? over * (1.f - 1.f / ratio) : 0.f;
                    float g = std::pow(10.f, (p[2] * 24.f - gr) / 20.f);
                    wl = dl * g; wr = dr * g;
                    break;
                }
                case INS_EQ: {
                    auto gdb = [](float v) { return std::pow(10.f, (v - 0.5f) * 24.f / 20.f); };
                    float gl = gdb(p[0]), gm = gdb(p[1]), gh = gdb(p[2]);
                    float lo = eqLoL.lp(dl), hi = eqHiL.hp(dl); wl = lo * gl + (dl - lo - hi) * gm + hi * gh;
                    lo = eqLoR.lp(dr); hi = eqHiR.hp(dr); wr = lo * gl + (dr - lo - hi) * gm + hi * gh;
                    break;
                }
                case INS_TREM: {
                    lfo += expo(p[0], 0.1f, 20.f) / sr; if (lfo >= 1.f) lfo -= 1.f;
                    float s = std::sin(kTwoPi * lfo);
                    if (p[2] < 0.5f) { float g = 1.f - p[1] * (0.5f - 0.5f * s); wl = dl * g; wr = dr * g; }
                    else { float pan = 0.5f + 0.5f * s * p[1]; wl = dl * std::cos(pan * kPi * 0.5f) * 1.414f; wr = dr * std::sin(pan * kPi * 0.5f) * 1.414f; }
                    break;
                }
                case INS_ECHO: {
                    float d = expo(p[0], 0.01f, 0.6f) * sr;
                    float el = readBuf(bl, d), er = readBuf(br, d);
                    float fb = p[1] * 0.9f;
                    bl[w] = dl + tl.lp(el) * fb; br[w] = dr + tr.lp(er) * fb;
                    w = (w + 1) & (BUF - 1);
                    wl = dl + el; wr = dr + er;
                    break;
                }
                case INS_RING: {
                    ringPh += expo(p[0], 20.f, 2000.f) / sr; if (ringPh >= 1.f) ringPh -= 1.f;
                    float c = std::sin(kTwoPi * ringPh); wl = dl * c; wr = dr * c;
                    break;
                }
                case INS_TAPE: {   // saturation + wow/flutter + tone
                    lfo += 0.6f / sr; if (lfo >= 1.f) lfo -= 1.f;
                    env += 7.3f / sr; if (env >= 1.f) env -= 1.f;
                    float d = (0.003f + p[1] * 0.004f * (0.5f + 0.5f * std::sin(kTwoPi * lfo)) + p[1] * 0.0004f * std::sin(kTwoPi * env)) * sr;
                    float g = 1.f + p[0] * 6.f, gn = softclip(g);
                    bl[w] = softclip(dl * g) / gn; br[w] = softclip(dr * g) / gn;
                    wl = tl.lp(readBuf(bl, d)); wr = tr.lp(readBuf(br, d));
                    w = (w + 1) & (BUF - 1);
                    break;
                }
                case INS_WIDTH: {
                    float mid = (dl + dr) * 0.5f, side = (dl - dr) * 0.5f * (p[0] * 2.5f);
                    // bass stays mono below ~150 Hz when Bass Mono is up
                    float sideLow = fl.lp(side);
                    side -= sideLow * p[1];
                    wl = mid + side; wr = mid - side;
                    break;
                }
                case INS_FOLD: {
                    float a = 1.f + p[0] * 8.f, sym = (p[1] - 0.5f);
                    wl = std::sin((dl + sym) * a * kPi * 0.5f) * 0.7f; wr = std::sin((dr + sym) * a * kPi * 0.5f) * 0.7f;
                    break;
                }
                default: break;
            }
            L[i] = dl + (wl - dl) * mix; R[i] = dr + (wr - dr) * mix;
        }
    }
};
