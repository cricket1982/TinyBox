// TinyBox - small DSP helpers
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

struct Rng {
    uint32_t s = 22222;
    explicit Rng(uint32_t seed = 22222) : s(seed) {}
    inline float next() { s = s * 1664525u + 1013904223u; return (int32_t)s * (1.0f / 2147483648.0f); } // -1..1
    inline float uni() { return (next() + 1.f) * 0.5f; } // 0..1
};

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// cheap tanh-ish soft clipper
inline float softclip(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float mtof(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }
// map 0..1 onto an exponential range
inline float expo(float p, float lo, float hi) { return lo * std::pow(hi / lo, clampf(p, 0.f, 1.f)); }
// per-sample multiplier that decays to -60 dB in t seconds
inline float decayCoef(float t, float sr) { return std::exp(-6.9078f / (std::max(t, 0.0005f) * sr)); }

// Cytomic / Simper state variable filter
struct SVF {
    float ic1 = 0, ic2 = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set(float fc, float q, float sr) {
        fc = clampf(fc, 10.f, sr * 0.45f);
        float g = std::tan(kPi * fc / sr);
        k = 1.f / std::max(q, 0.05f);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void proc(float v0, float &lp, float &bp, float &hp) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    inline float lp(float x) { float l, b, h; proc(x, l, b, h); return l; }
    inline float bp(float x) { float l, b, h; proc(x, l, b, h); return b; }
    inline float hp(float x) { float l, b, h; proc(x, l, b, h); return h; }
    void reset() { ic1 = ic2 = 0; }
};

struct OnePole {
    float z = 0, a = 0;
    void setLP(float fc, float sr) { a = std::exp(-kTwoPi * clampf(fc, 5.f, sr * 0.49f) / sr); }
    inline float lp(float x) { z = x + (z - x) * a; return z; }
    inline float hp(float x) { return x - lp(x); }
};

// polyBLEP residual for band-limited saw / pulse
inline float polyblep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

// fast sin(2*pi*t) for any t (parabolic approximation + one refinement, max error ~0.001)
inline float fsin2pi(float t) {
    t -= std::floor(t);
    float x = 2.f * t - 1.f;          // -1..1, sin(2pi t) = -sin(pi x)
    float y = 4.f * x * (1.f - std::fabs(x));
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}
