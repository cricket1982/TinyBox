// TinyBox - engine: sequencer, tracks, mixer, master FX
#pragma once
#include "machines.h"
#include "inserts.h"
#include <atomic>
#include <vector>

constexpr int kTracks = 8;
constexpr int kMaxSteps = 128;
constexpr int kDestStride = 32;   // LFO destination = track * 32 + param
constexpr int kPatterns = 8;
constexpr int kMasterParams = 24;
enum { TP_LEVEL = 12, TP_PAN = 13, TP_DLY = 14, TP_REV = 15 };
enum { MP_DTIME = 0, MP_DFB, MP_DTONE, MP_DLEVEL, MP_RSIZE, MP_RDAMP, MP_RLEVEL, MP_DRIVE, MP_VOLUME,
       MP_CTHR, MP_CRATIO, MP_CATK, MP_CREL, MP_CGAIN, MP_CMIX };
// performance FX ids
enum { FX_REP4 = 0, FX_REP8, FX_REP16, FX_REP32, FX_REVERSE, FX_TAPESTOP, FX_GATE, FX_CRUSH, FX_DUB, FX_FREEZE, FX_LOFI, FX_FILTER,
       FX_REP8T, FX_REP16T, FX_GLITCH, FX_HALF, FX_PUMP, FX_KILLLOW, FX_KILLMID, FX_KILLHIGH, FX_RING, FX_FLANGE, FX_DIST, FX_MONO, FX_WIDE,
       FX_COUNT };
inline bool fxIsBuffer(int id) { return id <= FX_TAPESTOP || (id >= FX_REP8T && id <= FX_HALF); }

// LFOs: 7 waves, free or tempo-synced, 3 destinations each.
// destination: -1 none, t*16+param = track param, 1000+i = master param
constexpr int kLfos = 4;
struct Lfo {
    int wave = 0;            // 0 sine 1 tri 2 saw up 3 saw down 4 square 5 S&H 6 smooth random
    float rate = 0.4f;       // 0..1
    bool sync = true, retrig = false;
    int dest[3] = {-1, -1, -1};
    float depth[3] = {0.f, 0.f, 0.f};   // -1..1
    // state (audio thread)
    double phase = 0; long long cycle = -1; float sh = 0, shPrev = 0; int lastTrig = 0;
};

// Trig conditions: 0 none, 1 FILL, 2 !FILL, 3 PRE, 4 !PRE, 5 NEI, 6 !NEI, 7 1ST, 8 !1ST, then A:B for B=2..8, A=1..B
constexpr int kCondFixed = 9;
constexpr int kCondCount = kCondFixed + 35;

struct Step {
    uint8_t on = 0, vel = 100, prob = 100, cond = 0, ratchet = 1, chord = 0, note = 60;
    int8_t micro = 0;   // -12..12 in 1/24ths of a step
    float len = 1.f;    // gate length in steps (synth)
    int8_t extra[3] = {-1, -1, -1}; // extra chord notes recorded from the keyboard (-1 = none)
    float lock[kTrackParams];
    Step() { for (float &l : lock) l = -1.f; }
};

struct Pattern {
    Step steps[kTracks][kMaxSteps];
    uint8_t length[kTracks];
    uint8_t res[kTracks];      // step resolution: 0 = 1/16, 1 = 1/32, 2 = 1/64
    Pattern() { for (auto &l : length) l = 16; for (auto &r : res) r = 0; }
};

enum CmdType { C_PLAY, C_STOP, C_NOTE_ON, C_NOTE_OFF, C_SET_MACHINE, C_SET_SAMPLE, C_PANIC };
struct Cmd { int type; int a, b; float v; void *ptr; };

template <typename T, int N>
struct SpscQueue {
    T buf[N];
    std::atomic<int> head{0}, tail{0};
    bool push(const T &v) {
        int h = head.load(std::memory_order_relaxed);
        int nh = (h + 1) % N;
        if (nh == tail.load(std::memory_order_acquire)) return false;
        buf[h] = v; head.store(nh, std::memory_order_release); return true;
    }
    bool pop(T &v) {
        int t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        v = buf[t]; tail.store((t + 1) % N, std::memory_order_release); return true;
    }
};

struct Retired { int kind; void *ptr; }; // 0 machine, 1 sample

struct Track {
    Machine *mach = nullptr;
    int machineId = 0;
    float base[kTrackParams];
    float active[kTrackParams];
    float eff[kTrackParams];   // active + LFO modulation (what the sound actually uses)
    SampleSlot slot;
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<float> peak{0.f};
    Insert ins[2];
    // sequencer state (audio thread)
    long long nextK = 0;   // next step to fire, counted in this track's own steps since the pattern started
    bool pre = true;
    int ratchLeft = 0; float ratchInterval = 0, ratchCount = 0; TrigParams ratchTP;
    std::atomic<int> curStep{-1};
    std::atomic<int> trigCount{0};
};

struct Delay {
    std::vector<float> bl, br; int w = 0; float len = 1000, target = 1000; OnePole tl, tr; float zl = 0, zr = 0;
    void init(float sr) { bl.assign((int)(sr * 2.5f), 0.f); br.assign((int)(sr * 2.5f), 0.f); }
    inline float read(std::vector<float> &b, float d) {
        int n = (int)b.size(); float rp = (float)w - d; while (rp < 0) rp += n;
        int i0 = (int)rp; float f = rp - i0; int i1 = (i0 + 1) % n;
        return b[i0] + (b[i1] - b[i0]) * f;
    }
};

struct Comb { std::vector<float> b; int i = 0; float store = 0;
    inline float tick(float x, float fb, float damp) {
        float o = b[i]; store = o * (1.f - damp) + store * damp; b[i] = x + store * fb; if (++i >= (int)b.size()) i = 0; return o; } };
struct Allpass { std::vector<float> b; int i = 0;
    inline float tick(float x) { float bo = b[i]; float o = -x + bo; b[i] = x + bo * 0.5f; if (++i >= (int)b.size()) i = 0; return o; } };

struct Reverb {
    Comb cl[4], cr[4]; Allpass al[2], ar[2];
    void init(float sr) {
        const int cs[4] = {1116, 1188, 1277, 1356}, as[2] = {556, 441};
        float k = sr / 44100.f;
        for (int i = 0; i < 4; ++i) { cl[i].b.assign((int)(cs[i] * k), 0.f); cr[i].b.assign((int)((cs[i] + 23) * k), 0.f); }
        for (int i = 0; i < 2; ++i) { al[i].b.assign((int)(as[i] * k), 0.f); ar[i].b.assign((int)((as[i] + 23) * k), 0.f); }
    }
    inline void tick(float inL, float inR, float fb, float damp, float &oL, float &oR) {
        float in = (inL + inR) * 0.04f; oL = 0; oR = 0;
        for (int i = 0; i < 4; ++i) { oL += cl[i].tick(in, fb, damp); oR += cr[i].tick(in, fb, damp); }
        for (int i = 0; i < 2; ++i) { oL = al[i].tick(oL); oR = ar[i].tick(oR); }
    }
};

class Engine {
public:
    explicit Engine(float sampleRate);
    ~Engine();
    void setSampleRate(float sr);
    void process(float *interleaved, int frames);

    // ---- UI thread API (pattern / params are written directly; single aligned writes)
    Pattern patterns[kPatterns];
    Track tracks[kTracks];
    float master[kMasterParams];
    std::atomic<float> bpm{120.f};
    std::atomic<float> swing{50.f};
    std::atomic<int> queuedPattern{0};
    std::atomic<int> curPattern{0};
    std::atomic<bool> fill{false};
    std::atomic<bool> playing{false};
    std::atomic<double> relPos{0.0};
    std::atomic<float> peak{0.f};
    std::atomic<float> compGR{0.f};      // dB of gain reduction
    std::atomic<bool> fxOn[32];
    Lfo lfos[kLfos];
    std::atomic<float> lfoVal[kLfos];
    std::atomic<float> fxX{0.5f}, fxY{0.2f};

    // song mode
    static constexpr int kSongRows = 64;
    uint8_t songPat[kSongRows] = {0};
    uint8_t songRep[kSongRows] = {1};
    std::atomic<int> songLen{0};
    std::atomic<bool> songMode{false};
    std::atomic<bool> songLoop{true};
    std::atomic<int> songStart{0};
    std::atomic<int> songRow{0};
    std::atomic<int> songRepeat{0};

    SpscQueue<Cmd, 1024> cmds;
    SpscQueue<Retired, 256> retired;

    void setParam(int t, int i, float v) { tracks[t].base[i] = v; tracks[t].active[i] = v; }
    void post(const Cmd &c) { cmds.push(c); }
    void freeRetired();

    float sr;
private:
    void handleCmd(const Cmd &c);
    void runSequencer(int n);
    void fireStep(int t, long long k, int offset, int mult);
    void emit(int t, const TrigParams &tp, int offset);
    void stopNow();
    long long lastBoundary = -1;
    bool evalCond(Track &tr, const Step &s, int loop);

    double songPos = 0; long long patStart = 0;
    Rng rng{12345};
    struct Ev { int track; int offset; TrigParams tp; };
    static constexpr int kMaxEv = 256;
    Ev evs[kMaxEv]; int nev = 0;
    float tbufL[256], tbufR[256];
    float busL[256], busR[256], dlyL[256], dlyR[256], revL[256], revR[256];
    Delay delay; Reverb reverb; OnePole dcL, dcR;
    // performance FX state
    std::vector<float> pbL, pbR; int pw = 0;
    int bufMode = -1, genMode = -1; bool prevOn[32] = {false};
    float trackMod[kTracks][kTrackParams];
    float masterMod[kMasterParams];
    float mEff[kMasterParams];
    void runLfos(int n);
    // glitch / half speed
    long long segT = 0; int segLen = 1, segMode = 0, segL = 1, segStart = 0;
    // kill EQ, pump, ring, flanger, distortion, stereo
    SVF kLoL, kLoR, kHiL, kHiR; float gLow = 1, gMid = 1, gHigh = 1;
    float mixPump = 0, mixRing = 0, mixFlange = 0, mixDist = 0, mixMono = 0, mixWide = 0, ringPh = 0, flPh = 0;
    std::vector<float> flL, flR; int flW = 0; OnePole distL, distR;
    long long fxT = 0; int fxStart = 0; int repLen = 1; double readPos = 0; float tapeRate = 1.f; float bufMix = 0.f;
    float mixFilter = 0, mixCrush = 0, mixGate = 0, mixLofi = 0, gateS = 1.f; double gatePh = 0;
    SVF pfL, pfR; float crL = 0, crR = 0; int crC = 0;
    std::vector<float> wowL, wowR; int ww = 0; float wowPh = 0, flutPh = 0; OnePole lofiL, lofiR;
    float compEnv = 0.f;
};
