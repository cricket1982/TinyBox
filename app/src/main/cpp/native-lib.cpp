// TinyBox - AAudio output + JNI bridge
#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <thread>
#include <mutex>
#include <time.h>
#include "engine.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TinyBox", __VA_ARGS__)

static Engine *gEngine = nullptr;
static AAudioStream *gStream = nullptr;
static std::mutex gStreamLock;
static std::atomic<long long> gFramesWritten{0};
static std::atomic<int> gSampleRate{48000};

static inline long long nowNs() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline void enableFlushToZero() {
#if defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    asm volatile("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));
#elif defined(__x86_64__) || defined(__i386__)
    unsigned int csr;
    asm volatile("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040;
    asm volatile("ldmxcsr %0" ::"m"(csr));
#endif
}

static aaudio_data_callback_result_t dataCallback(AAudioStream *, void *, void *audioData, int32_t numFrames) {
    enableFlushToZero();
    long long f = gFramesWritten.load(std::memory_order_relaxed);
    if (gEngine) { gEngine->snapshotClock(f); gEngine->process((float *)audioData, numFrames); }
    else std::fill((float *)audioData, (float *)audioData + numFrames * 2, 0.f);
    gFramesWritten.store(f + numFrames, std::memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void startStream();
static void errorCallback(AAudioStream *, void *, aaudio_result_t err) {
    LOGI("AAudio error %d, restarting stream", err);
    if (err == AAUDIO_ERROR_DISCONNECTED) std::thread([] { startStream(); }).detach();
}

static void closeStream() {
    if (gStream) {
        AAudioStream_requestStop(gStream);
        AAudioStream_close(gStream);
        gStream = nullptr;
    }
}

static void startStream() {
    std::lock_guard<std::mutex> lock(gStreamLock);
    closeStream();
    AAudioStreamBuilder *b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setDataCallback(b, dataCallback, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, errorCallback, nullptr);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &gStream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { LOGI("openStream failed %d", r); gStream = nullptr; return; }
    int32_t sr = AAudioStream_getSampleRate(gStream);
    gSampleRate.store(sr); gFramesWritten.store(0);
    if (!gEngine) gEngine = new Engine((float)sr);
    else if ((float)sr != gEngine->sr) gEngine->setSampleRate((float)sr);
    int32_t burst = AAudioStream_getFramesPerBurst(gStream);
    AAudioStream_setBufferSizeInFrames(gStream, burst * 2);
    AAudioStream_requestStart(gStream);
    LOGI("stream started: %d Hz, burst %d", sr, burst);
}

#define JFN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_tinybox_app_Native_##name

JFN(void, start)(JNIEnv *, jclass) {
    startStream();
    if (!gEngine) gEngine = new Engine(48000.f);
}
JFN(void, stop)(JNIEnv *, jclass) { std::lock_guard<std::mutex> lock(gStreamLock); closeStream(); }
JFN(jint, sampleRate)(JNIEnv *, jclass) { return gEngine ? (jint)gEngine->sr : 48000; }

JFN(void, play)(JNIEnv *, jclass) { if (gEngine) gEngine->post({C_PLAY, 0, 0, 0, nullptr}); }
JFN(void, halt)(JNIEnv *, jclass) { if (gEngine) gEngine->post({C_STOP, 0, 0, 0, nullptr}); }
JFN(void, panic)(JNIEnv *, jclass) { if (gEngine) gEngine->post({C_PANIC, 0, 0, 0, nullptr}); }
JFN(void, setBpm)(JNIEnv *, jclass, jfloat v) { if (gEngine) gEngine->bpm.store(v); }
JFN(void, setSwing)(JNIEnv *, jclass, jfloat v) { if (gEngine) gEngine->swing.store(v); }
JFN(void, setFill)(JNIEnv *, jclass, jboolean v) { if (gEngine) gEngine->fill.store(v); }
JFN(void, queuePattern)(JNIEnv *, jclass, jint p) {
    if (!gEngine || p < 0 || p >= kPatterns) return;
    gEngine->queuedPattern.store(p);
    if (!gEngine->playing.load()) gEngine->curPattern.store(p);
}
JFN(void, setParam)(JNIEnv *, jclass, jint t, jint i, jfloat v) {
    if (gEngine && t >= 0 && t < kTracks && i >= 0 && i < kTrackParams) gEngine->setParam(t, i, v);
}
JFN(void, setMaster)(JNIEnv *, jclass, jint i, jfloat v) {
    if (gEngine && i >= 0 && i < kMasterParams) gEngine->master[i] = v;
}
JFN(void, setMute)(JNIEnv *, jclass, jint t, jboolean m) { if (gEngine && t >= 0 && t < kTracks) gEngine->tracks[t].mute.store(m); }
JFN(void, setSolo)(JNIEnv *, jclass, jint t, jboolean m) { if (gEngine && t >= 0 && t < kTracks) gEngine->tracks[t].solo.store(m); }
JFN(void, setSong)(JNIEnv *env, jclass, jintArray pats, jintArray reps) {
    if (!gEngine) return;
    jsize n = std::min(env->GetArrayLength(pats), (jsize)Engine::kSongRows);
    n = std::min(n, env->GetArrayLength(reps));
    jint p[Engine::kSongRows], r[Engine::kSongRows];
    env->GetIntArrayRegion(pats, 0, n, p); env->GetIntArrayRegion(reps, 0, n, r);
    for (int i = 0; i < n; ++i) { gEngine->songPat[i] = (uint8_t)std::clamp(p[i], 0, kPatterns - 1); gEngine->songRep[i] = (uint8_t)std::clamp(r[i], 1, 64); }
    gEngine->songLen.store(n);
}
JFN(void, setSongMode)(JNIEnv *, jclass, jboolean on, jboolean loop, jint startRow) {
    if (!gEngine) return;
    gEngine->songMode.store(on); gEngine->songLoop.store(loop); gEngine->songStart.store(startRow);
}
JFN(void, setLfo)(JNIEnv *, jclass, jint i, jint wave, jfloat rate, jboolean sync, jboolean retrig) {
    if (!gEngine || i < 0 || i >= kLfos) return;
    Lfo &L = gEngine->lfos[i];
    L.wave = std::clamp(wave, 0, 6); L.rate = clampf(rate, 0.f, 1.f); L.sync = sync; L.retrig = retrig;
}
JFN(void, setLfoDest)(JNIEnv *, jclass, jint i, jint slot, jint dest, jfloat depth) {
    if (!gEngine || i < 0 || i >= kLfos || slot < 0 || slot > 2) return;
    Lfo &L = gEngine->lfos[i];
    L.depth[slot] = 0.f; L.dest[slot] = dest; L.depth[slot] = clampf(depth, -1.f, 1.f);
}
JFN(void, setFx)(JNIEnv *, jclass, jint id, jboolean on) { if (gEngine && id >= 0 && id < FX_COUNT) gEngine->fxOn[id].store(on); }
JFN(void, setFxXY)(JNIEnv *, jclass, jfloat x, jfloat y) { if (gEngine) { gEngine->fxX.store(clampf(x, 0.f, 1.f)); gEngine->fxY.store(clampf(y, 0.f, 1.f)); } }
JFN(void, setMachine)(JNIEnv *, jclass, jint t, jint m) {
    if (!gEngine || t < 0 || t >= kTracks || m < 0 || m >= M_COUNT) return;
    gEngine->freeRetired();
    Machine *mach = makeMachine(m, gEngine->sr, &gEngine->tracks[t].slot);
    gEngine->post({C_SET_MACHINE, t, m, 0, mach});
}
JFN(void, setStep)(JNIEnv *, jclass, jint p, jint t, jint i, jint on, jint vel, jint prob, jint cond,
                   jint ratchet, jint chord, jint note, jint micro, jfloat len, jint n2, jint n3, jint n4) {
    if (!gEngine || p < 0 || p >= kPatterns || t < 0 || t >= kTracks || i < 0 || i >= kMaxSteps) return;
    Step &s = gEngine->patterns[p].steps[t][i];
    s.vel = (uint8_t)std::clamp(vel, 1, 127); s.prob = (uint8_t)std::clamp(prob, 0, 100);
    s.cond = (uint8_t)std::clamp(cond, 0, kCondCount - 1); s.ratchet = (uint8_t)std::clamp(ratchet, 1, 8);
    s.chord = (uint8_t)std::clamp(chord, 0, 8); s.note = (uint8_t)std::clamp(note, 0, 127);
    s.micro = (int8_t)std::clamp(micro, -12, 12); s.len = std::clamp(len, 0.25f, 64.f);
    s.extra[0] = (int8_t)std::clamp(n2, -1, 127); s.extra[1] = (int8_t)std::clamp(n3, -1, 127); s.extra[2] = (int8_t)std::clamp(n4, -1, 127);
    s.on = on ? 1 : 0;
}
JFN(void, setLock)(JNIEnv *, jclass, jint p, jint t, jint i, jint param, jfloat v) {
    if (!gEngine || p < 0 || p >= kPatterns || t < 0 || t >= kTracks || i < 0 || i >= kMaxSteps || param < 0 || param >= kTrackParams) return;
    gEngine->patterns[p].steps[t][i].lock[param] = v;
}
JFN(void, setRes)(JNIEnv *, jclass, jint p, jint t, jint res) {
    if (!gEngine || p < 0 || p >= kPatterns || t < 0 || t >= kTracks) return;
    gEngine->patterns[p].res[t] = (uint8_t)std::clamp(res, 0, 2);
}
JFN(void, setInsert)(JNIEnv *, jclass, jint t, jint slot, jint type) {
    if (!gEngine || t < 0 || t >= kTracks || slot < 0 || slot > 1) return;
    gEngine->tracks[t].ins[slot].type.store(std::clamp(type, 0, INS_COUNT - 1));
}
/** reset every pattern to empty (used before loading a project, so only non-empty steps need sending) */
JFN(void, clearPatterns)(JNIEnv *, jclass) {
    if (!gEngine) return;
    for (auto &P : gEngine->patterns) {
        for (int t = 0; t < kTracks; ++t) { P.length[t] = 16; P.res[t] = 0; for (auto &st : P.steps[t]) st.on = 0; }
    }
    for (auto &P : gEngine->patterns) for (int t = 0; t < kTracks; ++t) for (auto &st : P.steps[t]) st = Step();
}
JFN(void, setLength)(JNIEnv *, jclass, jint p, jint t, jint len) {
    if (!gEngine || p < 0 || p >= kPatterns || t < 0 || t >= kTracks) return;
    gEngine->patterns[p].length[t] = (uint8_t)std::clamp(len, 1, kMaxSteps);
}
JFN(void, noteOn)(JNIEnv *, jclass, jint t, jint note, jfloat vel) { if (gEngine) gEngine->post({C_NOTE_ON, t, note, vel, nullptr}); }
JFN(void, noteOff)(JNIEnv *, jclass, jint t, jint note) { if (gEngine) gEngine->post({C_NOTE_OFF, t, note, 0, nullptr}); }

JFN(void, loadSample)(JNIEnv *env, jclass, jint t, jfloatArray l, jfloatArray r, jfloat sr) {
    if (!gEngine || t < 0 || t >= kTracks) return;
    gEngine->freeRetired();
    auto *s = new Sample();
    jsize n = env->GetArrayLength(l);
    s->L.resize(n); s->R.resize(n); s->sr = sr;
    env->GetFloatArrayRegion(l, 0, n, s->L.data());
    env->GetFloatArrayRegion(r, 0, std::min(n, env->GetArrayLength(r)), s->R.data());
    gEngine->post({C_SET_SAMPLE, t, 0, 0, s});
}
JFN(void, setSlices)(JNIEnv *env, jclass, jint t, jintArray starts) {
    if (!gEngine || t < 0 || t >= kTracks) return;
    jsize n = std::min(env->GetArrayLength(starts), (jsize)64);
    SampleSlot &slot = gEngine->tracks[t].slot;
    jint tmp[64];
    env->GetIntArrayRegion(starts, 0, n, tmp);
    for (int i = 0; i < n; ++i) slot.sliceStart[i] = tmp[i];
    slot.sliceCount = std::max(1, (int)n);
}

// status: [0]=playing [1]=pattern [2..9]=current step per track [10..17]=trigger counters [18]=peak*1000
//         [19..26]=track peaks*1000 [27]=song row [28]=song repeat [29]=compressor GR (dB*100) [30..33]=LFO values*1000
JFN(void, status)(JNIEnv *env, jclass, jintArray out) {
    if (!gEngine) return;
    jint s[34];
    s[0] = gEngine->playing.load() ? 1 : 0;
    s[1] = gEngine->curPattern.load();
    for (int t = 0; t < kTracks; ++t) {
        s[2 + t] = gEngine->tracks[t].curStep.load();
        s[10 + t] = gEngine->tracks[t].trigCount.load();
    }
    s[18] = (jint)(gEngine->peak.load() * 1000.f);
    for (int t = 0; t < kTracks; ++t) s[19 + t] = (jint)(gEngine->tracks[t].peak.load() * 1000.f);
    s[27] = gEngine->songRow.load(); s[28] = gEngine->songRepeat.load();
    s[29] = (jint)(gEngine->compGR.load() * 100.f);
    for (int k = 0; k < kLfos; ++k) s[30 + k] = (jint)(gEngine->lfoVal[k].load() * 1000.f);
    env->SetIntArrayRegion(out, 0, std::min(34, (int)env->GetArrayLength(out)), s);
}
/**
 * Sequencer position (in 1/16 steps since the pattern started) that was audible at the given
 * moment (SystemClock.uptimeMillis / MotionEvent.getEventTime clock). Uses the stream's
 * presentation timestamp, so touch and output latency are both accounted for.
 * Returns NaN if not playing.
 */
JFN(jdouble, posAtTime)(JNIEnv *, jclass, jlong uptimeMs) {
    if (!gEngine) return NAN;
    long long frame0; double pos0, pat0, spf; bool play;
    for (int tries = 0; tries < 8; ++tries) {
        uint32_t a = gEngine->clkSeq.load(std::memory_order_acquire);
        if (a & 1) continue;
        frame0 = gEngine->clkFrame; pos0 = gEngine->clkSongPos; pat0 = gEngine->clkPatStart; spf = gEngine->clkStepsPerFrame; play = gEngine->clkPlaying;
        if (gEngine->clkSeq.load(std::memory_order_acquire) == a) goto ok;
    }
    return NAN;
ok:
    if (!play) return NAN;
    double sr = (double)gSampleRate.load();
    double evNs = (double)uptimeMs * 1e6;
    double frameAtEv;
    int64_t fpos = 0, tns = 0;
    std::lock_guard<std::mutex> lock(gStreamLock);
    if (gStream && AAudioStream_getTimestamp(gStream, CLOCK_MONOTONIC, &fpos, &tns) == AAUDIO_OK) {
        frameAtEv = (double)fpos + (evNs - (double)tns) * sr / 1e9;          // frame being heard at that moment
    } else {
        // fallback: assume one buffer of output latency
        int32_t buf = gStream ? AAudioStream_getBufferSizeInFrames(gStream) : 512;
        frameAtEv = (double)gFramesWritten.load() - buf + (evNs - (double)nowNs()) * sr / 1e9;
    }
    return pos0 + (frameAtEv - (double)frame0) * spf - pat0;
}
JFN(void, skipStep)(JNIEnv *, jclass, jint t, jlong k) { if (gEngine && t >= 0 && t < kTracks) gEngine->tracks[t].skipK.store(k); }
JFN(void, setClick)(JNIEnv *, jclass, jboolean on) { if (gEngine) gEngine->click.store(on); }
JFN(jdouble, relPos)(JNIEnv *, jclass) { return gEngine ? gEngine->relPos.load() : 0.0; }
