// TinyBox - engine implementation
#include "engine.h"
#include <cstring>

static float trackGL[kTracks], trackGR[kTracks], trackGD[kTracks], trackGV[kTracks];

Engine::Engine(float sampleRate) : sr(sampleRate) {
    for (int t = 0; t < kTracks; ++t) {
        Track &tr = tracks[t];
        for (int i = 0; i < kTrackParams; ++i) tr.base[i] = tr.active[i] = 0.5f;
        tr.base[TP_LEVEL] = tr.active[TP_LEVEL] = 0.75f;
        tr.base[TP_DLY] = tr.active[TP_DLY] = 0.f;
        tr.base[TP_REV] = tr.active[TP_REV] = 0.f;
        tr.machineId = t < 6 ? t : M_SYNTH;
        tr.mach = makeMachine(tr.machineId, sr, &tr.slot);
        trackGL[t] = trackGR[t] = 1.f; trackGD[t] = trackGV[t] = 0.f;
    }
    for (float &m : master) m = 0.5f;
    master[MP_DLEVEL] = 0.6f; master[MP_RLEVEL] = 0.6f; master[MP_DRIVE] = 0.f; master[MP_VOLUME] = 0.8f;
    master[MP_CAMT] = 0.5f; master[MP_CSPEED] = 0.4f; master[MP_CKEY] = 0.f; master[MP_CMIX] = 0.f;
    for (int i = 0; i < 4; ++i) master[MP_FX0 + i * 4 + 3] = 1.f;
    for (auto &f : fxOn) f.store(false);
    for (auto &v : lfoVal) v.store(0.f);
    std::memset(trackMod, 0, sizeof(trackMod)); std::memset(masterMod, 0, sizeof(masterMod));
    for (int i = 0; i < kMasterParams; ++i) mEff[i] = master[i];
    for (auto &tr : tracks) std::memcpy(tr.eff, tr.active, sizeof(tr.eff));
    setSampleRate(sr);
}

Engine::~Engine() {
    for (auto &tr : tracks) { delete tr.mach; delete tr.slot.sample; }
    freeRetired();
}

void Engine::setSampleRate(float s) {
    sr = s;
    for (auto &tr : tracks) if (tr.mach) tr.mach->sr = sr;
    delay.init(sr); reverb.init(sr);
    for (auto &tr : tracks) { tr.ins[0].prepare(sr); tr.ins[1].prepare(sr); }
    for (auto &m : mfx) m.prepare(sr);
    dcL.setLP(8.f, sr); dcR.setLP(8.f, sr);
    pbL.assign((int)(sr * 4.f), 0.f); pbR.assign((int)(sr * 4.f), 0.f); pw = 0;
    wowL.assign(4096, 0.f); wowR.assign(4096, 0.f); ww = 0;
    lofiL.setLP(3500.f, sr); lofiR.setLP(3500.f, sr);
    kLoL.set(250.f, 0.6f, sr); kLoR.set(250.f, 0.6f, sr); kHiL.set(2500.f, 0.6f, sr); kHiR.set(2500.f, 0.6f, sr);
    flL.assign(2048, 0.f); flR.assign(2048, 0.f); flW = 0;
    distL.setLP(7000.f, sr); distR.setLP(7000.f, sr);
}

void Engine::freeRetired() {
    Retired r;
    while (retired.pop(r)) {
        if (r.kind == 0) delete (Machine *)r.ptr;
        else delete (Sample *)r.ptr;
    }
}

void Engine::handleCmd(const Cmd &c) {
    switch (c.type) {
        case C_PLAY: {
            songPos = 0; patStart = 0; lastBoundary = -1; lastClickBeat = -1;
            for (auto &tr : tracks) tr.skipK.store(-1);
            if (songMode.load() && songLen.load() > 0) {
                int r = std::clamp(songStart.load(), 0, songLen.load() - 1);
                songRow.store(r); songRepeat.store(0);
                curPattern.store(std::clamp((int)songPat[r], 0, kPatterns - 1));
                queuedPattern.store(curPattern.load());
            } else curPattern.store(queuedPattern.load());
            for (auto &tr : tracks) { tr.nextK = 0; tr.pre = true; tr.ratchLeft = 0; }
            playing.store(true);
            break;
        }
        case C_STOP: stopNow(); break;
        case C_NOTE_ON: {
            if (c.a < 0 || c.a >= kTracks) break;
            Track &tr = tracks[c.a];
            TrigParams tp;
            std::memcpy(tp.p, tr.base, sizeof(tp.p));
            tp.vel = c.v; tp.note = c.b; tp.chord = 0; tp.gateSamples = -1.f;
            tp.notes[0] = c.b; tp.nnotes = 1;
            emit(c.a, tp, 0);
            break;
        }
        case C_NOTE_OFF: {
            if (c.a < 0 || c.a >= kTracks || nev >= kMaxEv) break;
            Ev &e = evs[nev++]; e.track = c.a; e.offset = 0; e.tp.note = c.b; e.tp.vel = -1.f; // vel<0 marks note-off
            break;
        }
        case C_SET_MACHINE: {
            Track &tr = tracks[c.a];
            Machine *old = tr.mach;
            tr.mach = (Machine *)c.ptr; tr.machineId = c.b;
            if (old) retired.push({0, old});
            break;
        }
        case C_SET_SAMPLE: {
            Track &tr = tracks[c.a];
            if (tr.mach) tr.mach->allOff();
            Sample *old = tr.slot.sample;
            tr.slot.sample = (Sample *)c.ptr;
            if (old) retired.push({1, old});
            break;
        }
        case C_PANIC:
            for (auto &tr : tracks) if (tr.mach) tr.mach->allOff();
            break;
    }
}

void Engine::snapshotClock(long long frame) {
    clkSeq.fetch_add(1, std::memory_order_acq_rel);   // odd = being written
    clkFrame = frame; clkSongPos = songPos; clkPatStart = (double)patStart;
    clkStepsPerFrame = bpm.load() / 60.0 * 4.0 / sr;
    clkPlaying = playing.load(std::memory_order_relaxed);
    clkSeq.fetch_add(1, std::memory_order_acq_rel);
}

void Engine::stopNow() {
    playing.store(false);
    for (auto &tr : tracks) { tr.ratchLeft = 0; tr.curStep.store(-1); if (tr.mach) tr.mach->allOff(); }
}

void Engine::emit(int t, const TrigParams &tp, int offset) {
    if (nev >= kMaxEv) return;
    Ev &e = evs[nev++]; e.track = t; e.offset = offset; e.tp = tp;
}

bool Engine::evalCond(Track &tr, const Step &s, int loop) {
    bool probOk = s.prob >= 100 || rng.uni() * 100.f < (float)s.prob;
    bool condOk = true, updatesPre = true;
    int c = s.cond;
    int t = (int)(&tr - tracks);
    switch (c) {
        case 0: updatesPre = s.prob < 100; break;
        case 1: condOk = fill.load(); break;
        case 2: condOk = !fill.load(); break;
        case 3: condOk = tr.pre; updatesPre = false; break;
        case 4: condOk = !tr.pre; updatesPre = false; break;
        case 5: condOk = t > 0 ? tracks[t - 1].pre : tr.pre; updatesPre = false; break;
        case 6: condOk = !(t > 0 ? tracks[t - 1].pre : tr.pre); updatesPre = false; break;
        case 7: condOk = loop == 0; break;
        case 8: condOk = loop != 0; break;
        default: {
            int idx = c - kCondFixed, a = 1, b = 2;
            for (b = 2; b <= 8; ++b) { if (idx < b) { a = idx + 1; break; } idx -= b; }
            if (b > 8) { b = 8; a = 8; }
            condOk = (loop % b) == (a - 1);
        }
    }
    bool res = probOk && condOk;
    if (updatesPre) tr.pre = res;
    return res;
}

void Engine::fireStep(int t, long long k, int offset, int mult) {
    Track &tr = tracks[t];
    const Pattern &P = patterns[curPattern.load(std::memory_order_relaxed)];
    int len = std::clamp((int)P.length[t], 1, kMaxSteps);
    long long rel = k;
    int idx = (int)(rel % len);
    int loop = (int)(rel / len);
    tr.curStep.store(idx, std::memory_order_relaxed);
    const Step &s = P.steps[t][idx];
    if (!s.on) return;
    if (tr.skipK.load(std::memory_order_relaxed) == k) { tr.skipK.store(-1, std::memory_order_relaxed); return; }
    if (!evalCond(tr, s, loop)) return;
    if (tr.mute.load(std::memory_order_relaxed)) return;
    const float sps = sr * 60.f / (bpm.load() * 4.f) / mult;   // samples per step of this track
    TrigParams tp;
    for (int i = 0; i < kTrackParams; ++i) tp.p[i] = s.lock[i] >= 0.f ? s.lock[i] : tr.base[i];
    tp.vel = s.vel / 127.f; tp.note = s.note; tp.chord = s.chord;
    tp.notes[0] = s.note; tp.nnotes = 1;
    for (int k = 0; k < 3; ++k) if (s.extra[k] >= 0) tp.notes[tp.nnotes++] = s.extra[k];
    tp.gateSamples = std::max(1.f, s.len * sps);
    int rat = std::clamp((int)s.ratchet, 1, 8);
    if (rat > 1) {
        tp.gateSamples = std::min(tp.gateSamples, sps / rat * 0.8f);
        tr.ratchLeft = rat - 1; tr.ratchInterval = sps / rat; tr.ratchCount = tr.ratchInterval; tr.ratchTP = tp;
    } else tr.ratchLeft = 0;
    emit(t, tp, offset);
}

void Engine::runSequencer(int n) {
    clickAt = -1;
    if (!playing.load(std::memory_order_relaxed)) return;
    const double stepsPerSample = bpm.load() / 60.0 * 4.0 / sr;
    const double sw = clampf(swing.load(), 50.f, 75.f) / 100.0 * 2.0 - 1.0;
    for (int i = 0; i < n; ++i) {
        int cp = curPattern.load(std::memory_order_relaxed);
        const Pattern *P = &patterns[cp];
        // bar boundary (master length = longest track): pattern queue / song advance
        {
            int masterLen = 1;
            for (int t = 0; t < kTracks; ++t) {
                int m = 1 << std::clamp((int)P->res[t], 0, 2);
                masterLen = std::max(masterLen, (std::clamp((int)P->length[t], 1, kMaxSteps) + m - 1) / m);
            }
            double rel = songPos - (double)patStart;
            long long boundaryAbs = patStart + ((long long)(rel / masterLen) + 1) * masterLen;
            if (songPos >= (double)boundaryAbs - 0.5 && boundaryAbs != lastBoundary) {
                lastBoundary = boundaryAbs;
                int next = cp; bool reset = false;
                if (songMode.load(std::memory_order_relaxed) && songLen.load() > 0) {
                    int row = songRow.load(), rep = songRepeat.load() + 1;
                    if (rep >= std::max(1, (int)songRep[row])) {
                        rep = 0; ++row;
                        if (row >= songLen.load()) {
                            if (songLoop.load()) row = 0;
                            else { stopNow(); relPos.store(0.0); return; }
                        }
                        next = std::clamp((int)songPat[row], 0, kPatterns - 1);
                        reset = true;
                    }
                    songRow.store(row); songRepeat.store(rep);
                } else {
                    int q = queuedPattern.load(std::memory_order_relaxed);
                    if (q != cp && q >= 0 && q < kPatterns) { next = q; reset = true; }
                }
                if (reset) {
                    patStart = boundaryAbs;
                    for (auto &tr : tracks) { tr.nextK = 0; tr.ratchLeft = 0; tr.skipK.store(-1); }
                    curPattern.store(next); queuedPattern.store(next); cp = next; P = &patterns[cp];
                }
            }
        }
        for (int t = 0; t < kTracks; ++t) {
            Track &tr = tracks[t];
            int len = std::clamp((int)P->length[t], 1, kMaxSteps);
            const int mult = 1 << std::clamp((int)P->res[t], 0, 2);
            const double tpos = (songPos - (double)patStart) * mult;   // position in this track's steps
            for (int guard = 0; guard < 8; ++guard) {
                long long k = tr.nextK;
                const Step &s = P->steps[t][(int)(k % len)];
                double off = (s.on ? s.micro / 24.0 : 0.0) + ((k & 1) ? sw : 0.0);
                if (tpos >= (double)k + off) { fireStep(t, k, i, mult); tr.nextK = k + 1; }
                else break;
            }
            { long long sk = tr.skipK.load(std::memory_order_relaxed); if (sk >= 0 && sk + 1 < tr.nextK) tr.skipK.store(-1, std::memory_order_relaxed); }
            if (tr.ratchLeft > 0) {
                tr.ratchCount -= 1.f;
                if (tr.ratchCount <= 0.f) {
                    if (!tr.mute.load(std::memory_order_relaxed)) emit(t, tr.ratchTP, i);
                    --tr.ratchLeft; tr.ratchCount += tr.ratchInterval;
                }
            }
        }
        if (click.load(std::memory_order_relaxed)) {
            long long beat = (long long)std::floor(songPos / 4.0);
            if (beat != lastClickBeat) {
                lastClickBeat = beat; clickEnv = 0.45f; clickPh = 0.f;
                clickFreq = (beat % 4 == 0) ? 1760.f : 1175.f;
                clickAt = i;
            }
        }
        songPos += stepsPerSample;
    }
    relPos.store(songPos - (double)patStart, std::memory_order_relaxed);
}

void Engine::runLfos(int n) {
    std::memset(trackMod, 0, sizeof(trackMod)); std::memset(masterMod, 0, sizeof(masterMod));
    static const double kDivSteps[9] = {0.5, 1, 2, 4, 8, 16, 32, 64, 128};
    const bool play = playing.load(std::memory_order_relaxed);
    for (int k = 0; k < kLfos; ++k) {
        Lfo &L = lfos[k];
        bool used = false;
        for (int d = 0; d < 3; ++d) used |= L.dest[d] >= 0 && L.depth[d] != 0.f;
        double total;
        if (L.sync && play) {
            double div = kDivSteps[std::clamp((int)(L.rate * 8.99f), 0, 8)];
            total = songPos / div;
        } else {
            float hz = expo(L.rate, 0.02f, 20.f);
            if (L.retrig) {
                int dt = L.dest[0] >= 0 && L.dest[0] < kTracks * kDestStride ? L.dest[0] / kDestStride : -1;
                if (dt >= 0) { int c = tracks[dt].trigCount.load(std::memory_order_relaxed); if (c != L.lastTrig) { L.lastTrig = c; L.phase = std::floor(L.phase) + 1.0; } }
            }
            L.phase += hz * n / sr;
            total = L.phase;
        }
        long long cyc = (long long)std::floor(total);
        float f = (float)(total - (double)cyc);
        if (cyc != L.cycle) { L.cycle = cyc; L.shPrev = L.sh; L.sh = rng.next(); }
        float v = 0.f;
        switch (L.wave) {
            case 0: v = std::sin(kTwoPi * f); break;
            case 1: v = 1.f - 4.f * std::fabs(f - 0.5f); break;
            case 2: v = 2.f * f - 1.f; break;
            case 3: v = 1.f - 2.f * f; break;
            case 4: v = f < 0.5f ? 1.f : -1.f; break;
            case 5: v = L.sh; break;
            default: { float sm = f * f * (3.f - 2.f * f); v = L.shPrev + (L.sh - L.shPrev) * sm; }
        }
        lfoVal[k].store(v, std::memory_order_relaxed);
        if (!used) continue;
        for (int d = 0; d < 3; ++d) {
            int dst = L.dest[d];
            if (dst < 0 || L.depth[d] == 0.f) continue;
            float m = v * L.depth[d] * 0.5f;
            if (dst < kTracks * kDestStride) { int pi = dst % kDestStride; if (pi < kTrackParams) trackMod[dst / kDestStride][pi] += m; }
            else if (dst >= 1000 && dst < 1000 + kMasterParams) masterMod[dst - 1000] += m;
        }
    }
    for (int i = 0; i < kMasterParams; ++i) mEff[i] = clampf(master[i] + masterMod[i], 0.f, 1.f);
}

void Engine::process(float *out, int frames) {
    int done = 0;
    while (done < frames) {
        const int n = std::min(256, frames - done);
        nev = 0;
        Cmd c;
        while (cmds.pop(c)) handleCmd(c);
        runSequencer(n);
        runLfos(n);

        std::memset(busL, 0, sizeof(float) * n); std::memset(busR, 0, sizeof(float) * n);
        std::memset(dlyL, 0, sizeof(float) * n); std::memset(dlyR, 0, sizeof(float) * n);
        std::memset(revL, 0, sizeof(float) * n); std::memset(revR, 0, sizeof(float) * n);

        const int keyTrack = (int)std::lround(mEff[MP_CKEY] * 8.f) - 1;   // -1 = master itself
        std::memset(keyBuf, 0, sizeof(float) * n);
        bool anySolo = false;
        for (auto &tr : tracks) anySolo |= tr.solo.load(std::memory_order_relaxed);
        for (int t = 0; t < kTracks; ++t) {
            Track &tr = tracks[t];
            Machine *m = tr.mach;
            for (int i = 0; i < kTrackParams; ++i) tr.eff[i] = clampf(tr.active[i] + trackMod[t][i], 0.f, 1.f);
            std::memset(tbufL, 0, sizeof(float) * n); std::memset(tbufR, 0, sizeof(float) * n);
            int pos = 0;
            for (int e = 0; e < nev; ++e) {
                if (evs[e].track != t) continue;
                int off = std::clamp(evs[e].offset, 0, n);
                if (off > pos) { m->render(tbufL + pos, tbufR + pos, off - pos, tr.eff); pos = off; }
                if (evs[e].tp.vel < 0.f) { m->noteOff(evs[e].tp.note); continue; }
                std::memcpy(tr.active, evs[e].tp.p, sizeof(tr.active));
                for (int i = 0; i < kTrackParams; ++i) tr.eff[i] = clampf(tr.active[i] + trackMod[t][i], 0.f, 1.f);
                TrigParams tm = evs[e].tp;
                std::memcpy(tm.p, tr.eff, sizeof(tm.p));
                m->trigger(tm);
                tr.trigCount.fetch_add(1, std::memory_order_relaxed);
            }
            if (pos < n) m->render(tbufL + pos, tbufR + pos, n - pos, tr.eff);
            tr.ins[0].process(tbufL, tbufR, n, tr.eff + 16);
            tr.ins[1].process(tbufL, tbufR, n, tr.eff + 20);

            // mixer (gains ramp over the block to avoid zipper noise)
            float lv = tr.eff[TP_LEVEL]; lv = lv * lv * 1.6f;
            if (anySolo && !tr.solo.load(std::memory_order_relaxed)) lv = 0.f;
            float pa = clampf(tr.eff[TP_PAN], 0.f, 1.f) * kPi * 0.5f;
            float tgL = std::cos(pa) * 1.41421f * lv, tgR = std::sin(pa) * 1.41421f * lv;
            float tgD = tr.eff[TP_DLY] * tr.eff[TP_DLY], tgV = tr.eff[TP_REV] * tr.eff[TP_REV];
            float gL = trackGL[t], gR = trackGR[t], gD = trackGD[t], gV = trackGV[t];
            const float inv = 1.f / n;
            const float dL = (tgL - gL) * inv, dR = (tgR - gR) * inv, dD = (tgD - gD) * inv, dV = (tgV - gV) * inv;
            float tpk = tr.peak.load(std::memory_order_relaxed) * 0.9f;
            for (int i = 0; i < n; ++i) {
                gL += dL; gR += dR; gD += dD; gV += dV;
                float l = tbufL[i] * gL, r = tbufR[i] * gR;
                float a = std::max(std::fabs(l), std::fabs(r)); if (a > tpk) tpk = a;
                if (t == keyTrack) keyBuf[i] = std::max(std::fabs(tbufL[i]), std::fabs(tbufR[i])) * lv;
                busL[i] += l; busR[i] += r;
                dlyL[i] += l * gD; dlyR[i] += r * gD;
                revL[i] += l * gV; revR[i] += r * gV;
            }
            tr.peak.store(tpk, std::memory_order_relaxed);
            trackGL[t] = tgL; trackGR[t] = tgR; trackGD[t] = tgD; trackGV[t] = tgV;
        }

        // master FX
        static const int kDlySteps[8] = {1, 2, 3, 4, 6, 8, 12, 16};
        const float sps = sr * 60.f / (bpm.load() * 4.f);
        const bool dub = fxOn[FX_DUB].load(std::memory_order_relaxed);
        const bool freeze = fxOn[FX_FREEZE].load(std::memory_order_relaxed);
        delay.target = std::min((float)delay.bl.size() - 4.f, kDlySteps[std::clamp((int)(mEff[MP_DTIME] * 7.99f), 0, 7)] * sps);
        const float dfb = dub ? std::max(mEff[MP_DFB] * 0.95f, 0.85f) : mEff[MP_DFB] * 0.95f;
        delay.tl.setLP(expo(mEff[MP_DTONE], 400.f, 16000.f), sr); delay.tr.a = delay.tl.a;
        const float dlev = mEff[MP_DLEVEL] * 1.2f * (dub ? 1.4f : 1.f);
        const float rfb = freeze ? 0.9995f : 0.7f + mEff[MP_RSIZE] * 0.28f;
        const float rdamp = freeze ? 0.f : mEff[MP_RDAMP] * 0.6f;
        const float rlev = mEff[MP_RLEVEL] * 1.5f;
        const float drv = 1.f + mEff[MP_DRIVE] * 6.f;
        const float vol = mEff[MP_VOLUME] * mEff[MP_VOLUME] * 1.25f;
        // compressor settings
        // pumping bus compressor: Amount sets threshold + ratio + auto make-up, Speed sets release
        const float cAmt = mEff[MP_CAMT];
        const float cThr = -4.f - cAmt * 30.f;
        const float cSlope = 1.f - 1.f / (2.f + cAmt * 18.f);
        const float cAtk = 1.f - std::exp(-1.f / (0.002f * sr));
        const float cRel = 1.f - std::exp(-1.f / (expo(mEff[MP_CSPEED], 0.03f, 0.8f) * sr));
        const float cMake = cAmt * 9.f, cMix = mEff[MP_CMIX];
        float grMax = 0.f;
        // performance FX: detect newly pressed buffer effects (repeat / reverse / tape stop)
        const int pbSize = (int)pbL.size();
        for (int id = 0; id < FX_COUNT; ++id) {
            if (!fxIsBuffer(id)) continue;
            bool on = fxOn[id].load(std::memory_order_relaxed);
            if (on && !prevOn[id]) {
                bufMode = genMode = id; fxT = 0; fxStart = pw; readPos = pw - 1; tapeRate = 1.f; segT = 0;
                float st = id == FX_REP4 ? 4.f : id == FX_REP8 ? 2.f : id == FX_REP16 ? 1.f : id == FX_REP32 ? 0.5f
                         : id == FX_REP8T ? 4.f / 3.f : id == FX_REP16T ? 2.f / 3.f : 1.f;
                repLen = std::max(64, (int)(st * sps));
                segLen = std::max(64, (int)sps);
            }
            prevOn[id] = on;
        }
        if (bufMode >= 0 && !fxOn[bufMode].load(std::memory_order_relaxed)) bufMode = -1;
        const bool fFilter = fxOn[FX_FILTER].load(std::memory_order_relaxed);
        const bool fCrush = fxOn[FX_CRUSH].load(std::memory_order_relaxed);
        const bool fGate = fxOn[FX_GATE].load(std::memory_order_relaxed);
        const bool fLofi = fxOn[FX_LOFI].load(std::memory_order_relaxed);
        auto fx = [&](int id) { return fxOn[id].load(std::memory_order_relaxed); };
        const bool fKL = fx(FX_KILLLOW), fKM = fx(FX_KILLMID), fKH = fx(FX_KILLHIGH);
        const bool fPump = fx(FX_PUMP), fRing = fx(FX_RING), fFl = fx(FX_FLANGE), fDist = fx(FX_DIST), fMono = fx(FX_MONO), fWide = fx(FX_WIDE);
        {
            float x = fxX.load(), y = fxY.load();
            float q = 0.7f + y * 8.f;
            float fc = x < 0.5f ? expo(x * 2.f, 60.f, 20000.f) : expo((x - 0.5f) * 2.f, 20.f, 12000.f);
            pfL.set(fc, q, sr); pfR.set(fc, q, sr);
        }
        const bool filterHP = fxX.load() >= 0.5f;
        const double stepsPerSample = 1.0 / sps;
        if (playing.load(std::memory_order_relaxed)) gatePh = songPos - n * stepsPerSample;
        const float ramp = 1.f / 256.f;
        float pk = peak.load(std::memory_order_relaxed) * 0.9f;
        const int dsize = (int)delay.bl.size();
        for (int i = 0; i < n; ++i) {
            delay.len += (delay.target - delay.len) * 0.0005f;
            float dl = delay.read(delay.bl, delay.len), dr = delay.read(delay.br, delay.len);
            float dIn = (dlyL[i] + dlyR[i]) * 0.5f + (dub ? (busL[i] + busR[i]) * 0.35f : 0.f);
            delay.bl[delay.w] = dIn + delay.tr.lp(dr) * dfb;
            delay.br[delay.w] = delay.tl.lp(dl) * dfb;
            if (++delay.w >= dsize) delay.w = 0;
            float rl, rr;
            reverb.tick(freeze ? 0.f : revL[i], freeze ? 0.f : revR[i], rfb, rdamp, rl, rr);
            float L = busL[i] + dl * dlev + rl * rlev;
            float R = busR[i] + dr * dlev + rr * rlev;
            L = dcL.hp(L); R = dcR.hp(R);

            // ---- buffer effects
            pbL[pw] = L; pbR[pw] = R;
            float target = bufMode >= 0 ? 1.f : 0.f;
            bufMix += clampf(target - bufMix, -ramp, ramp);
            if (genMode >= 0 && bufMix > 0.f) {
                float bl = L, br = R;
                const bool isRep = genMode <= FX_REP32 || genMode == FX_REP8T || genMode == FX_REP16T;
                if (isRep) {
                    if (fxT >= repLen) {
                        int ph = (int)(fxT % repLen);
                        int idx = (fxStart + ph) % pbSize;
                        float w = std::min(1.f, std::min(ph / 64.f, (repLen - ph) / 64.f));
                        bl = pbL[idx] * w; br = pbR[idx] * w;
                    }
                } else if (genMode == FX_REVERSE) {
                    long long back = std::min<long long>(fxT, pbSize - 2);
                    int idx = (int)(((long long)fxStart - back) % pbSize); if (idx < 0) idx += pbSize;
                    float w = std::min(1.f, fxT / 64.f);
                    bl = pbL[idx] * w + L * (1.f - w); br = pbR[idx] * w + R * (1.f - w);
                } else if (genMode == FX_GLITCH) {
                    long long st = segT % segLen;
                    if (st == 0) {
                        segStart = pw;
                        float r = rng.uni();
                        segMode = r < 0.25f ? 0 : (r < 0.8f ? 1 : 2);
                        static const float kFr[3] = {0.25f, 0.5f, 1.f};
                        segL = std::max(32, (int)(segLen * kFr[std::min(2, (int)(rng.uni() * 3.f))]));
                    }
                    if (segMode != 0) {
                        int ph = (int)(st % segL);
                        int idx = segMode == 1 ? segStart - segL + ph : segStart - 1 - ph;
                        idx %= pbSize; if (idx < 0) idx += pbSize;
                        float w = std::min(1.f, std::min(ph / 32.f, (segL - ph) / 32.f));
                        bl = pbL[idx] * w; br = pbR[idx] * w;
                    }
                    ++segT;
                } else if (genMode == FX_HALF) {
                    readPos += 0.5;
                    double lag = (double)pw - readPos; if (lag < 0) lag += pbSize;
                    if (lag > pbSize - 2048) readPos = pw - 1;
                    double rp = readPos; while (rp < 0) rp += pbSize; while (rp >= pbSize) rp -= pbSize;
                    int i0 = (int)rp; float fr = (float)(rp - i0); int i1 = (i0 + 1) % pbSize;
                    float w = std::min(1.f, fxT / 64.f);
                    bl = (pbL[i0] + (pbL[i1] - pbL[i0]) * fr) * w + L * (1.f - w);
                    br = (pbR[i0] + (pbR[i1] - pbR[i0]) * fr) * w + R * (1.f - w);
                } else { // tape stop
                    readPos += tapeRate;
                    tapeRate = std::max(0.f, tapeRate - 1.f / (0.9f * sr));
                    double rp = readPos; while (rp < 0) rp += pbSize; while (rp >= pbSize) rp -= pbSize;
                    int i0 = (int)rp; float fr = (float)(rp - i0); int i1 = (i0 + 1) % pbSize;
                    float g = std::min(1.f, tapeRate * 3.f);
                    bl = (pbL[i0] + (pbL[i1] - pbL[i0]) * fr) * g; br = (pbR[i0] + (pbR[i1] - pbR[i0]) * fr) * g;
                }
                ++fxT;
                L = L + (bl - L) * bufMix; R = R + (br - R) * bufMix;
            } else if (bufMix <= 0.f) genMode = -1;
            if (++pw >= pbSize) pw = 0;

            // ---- lo-fi wobble
            mixLofi += clampf((fLofi ? 1.f : 0.f) - mixLofi, -ramp, ramp);
            wowL[ww] = L; wowR[ww] = R;
            if (mixLofi > 0.f) {
                wowPh += 0.7f / sr; if (wowPh >= 1.f) wowPh -= 1.f;
                flutPh += 6.3f / sr; if (flutPh >= 1.f) flutPh -= 1.f;
                float d = 0.006f * sr * (1.f + 0.45f * std::sin(kTwoPi * wowPh) + 0.08f * std::sin(kTwoPi * flutPh));
                float rp = (float)ww - d; if (rp < 0) rp += 4096.f;
                int i0 = (int)rp & 4095; float fr = rp - std::floor(rp); int i1 = (i0 + 1) & 4095;
                float wl = lofiL.lp(softclip((wowL[i0] + (wowL[i1] - wowL[i0]) * fr) * 1.3f)) + rng.next() * 0.003f;
                float wr = lofiR.lp(softclip((wowR[i0] + (wowR[i1] - wowR[i0]) * fr) * 1.3f)) + rng.next() * 0.003f;
                L += (wl - L) * mixLofi; R += (wr - R) * mixLofi;
            }
            ww = (ww + 1) & 4095;

            // ---- kill EQ (3-band isolator)
            gLow += clampf((fKL ? 0.f : 1.f) - gLow, -ramp, ramp);
            gMid += clampf((fKM ? 0.f : 1.f) - gMid, -ramp, ramp);
            gHigh += clampf((fKH ? 0.f : 1.f) - gHigh, -ramp, ramp);
            if (gLow < 1.f || gMid < 1.f || gHigh < 1.f) {
                float lo = kLoL.lp(L), hi = kHiL.hp(L); L = lo * gLow + (L - lo - hi) * gMid + hi * gHigh;
                lo = kLoR.lp(R); hi = kHiR.hp(R); R = lo * gLow + (R - lo - hi) * gMid + hi * gHigh;
            } else { kLoL.lp(L); kHiL.hp(L); kLoR.lp(R); kHiR.hp(R); }
            // ---- DJ filter
            mixFilter += clampf((fFilter ? 1.f : 0.f) - mixFilter, -ramp, ramp);
            {
                float a, b, c;
                pfL.proc(L, a, b, c); float fl = filterHP ? c : a;
                pfR.proc(R, a, b, c); float fr = filterHP ? c : a;
                if (mixFilter > 0.f) { L += (fl - L) * mixFilter; R += (fr - R) * mixFilter; }
            }
            // ---- flanger
            mixFlange += clampf((fFl ? 1.f : 0.f) - mixFlange, -ramp, ramp);
            {
                flPh += 0.25f / sr; if (flPh >= 1.f) flPh -= 1.f;
                float d = (0.0005f + 0.0055f * (0.5f + 0.5f * std::sin(kTwoPi * flPh))) * sr;
                float rp = (float)flW - d; if (rp < 0) rp += 2048.f;
                int i0 = (int)rp & 2047; float fr = rp - std::floor(rp); int i1 = (i0 + 1) & 2047;
                float yl = flL[i0] + (flL[i1] - flL[i0]) * fr, yr = flR[i0] + (flR[i1] - flR[i0]) * fr;
                flL[flW] = L + yl * 0.65f * mixFlange; flR[flW] = R + yr * 0.65f * mixFlange; flW = (flW + 1) & 2047;
                if (mixFlange > 0.f) { L += ((L + yl) * 0.7f - L) * mixFlange; R += ((R + yr) * 0.7f - R) * mixFlange; }
            }
            // ---- ring mod
            mixRing += clampf((fRing ? 1.f : 0.f) - mixRing, -ramp, ramp);
            ringPh += 330.f / sr; if (ringPh >= 1.f) ringPh -= 1.f;
            if (mixRing > 0.f) { float c = std::sin(kTwoPi * ringPh); L += (L * c - L) * mixRing; R += (R * c - R) * mixRing; }
            // ---- distortion
            mixDist += clampf((fDist ? 1.f : 0.f) - mixDist, -ramp, ramp);
            if (mixDist > 0.f) {
                float dl = distL.lp(softclip(L * 6.f) * 0.5f), dr = distR.lp(softclip(R * 6.f) * 0.5f);
                L += (dl - L) * mixDist; R += (dr - R) * mixDist;
            }
            // ---- bit crush
            mixCrush += clampf((fCrush ? 1.f : 0.f) - mixCrush, -ramp, ramp);
            if (mixCrush > 0.f) {
                if (++crC >= 8) { crC = 0; crL = std::round(L * 24.f) / 24.f; crR = std::round(R * 24.f) / 24.f; }
                L += (crL - L) * mixCrush; R += (crR - R) * mixCrush;
            }
            // ---- trance gate (1/16)
            gatePh += stepsPerSample;
            mixGate += clampf((fGate ? 1.f : 0.f) - mixGate, -ramp, ramp);
            {
                float ph = (float)(gatePh - std::floor(gatePh));
                gateS += ((ph < 0.5f ? 1.f : 0.f) - gateS) * 0.02f;
                if (mixGate > 0.f) { float g = 1.f - mixGate * (1.f - gateS); L *= g; R *= g; }
            }
            // ---- sidechain-style pump (quarter notes)
            mixPump += clampf((fPump ? 1.f : 0.f) - mixPump, -ramp, ramp);
            if (mixPump > 0.f) {
                double q = gatePh / 4.0; float ph = (float)(q - std::floor(q));
                float e = std::min(1.f, ph / 0.55f); e = 0.08f + 0.92f * e * e;
                float g = 1.f - mixPump * (1.f - e); L *= g; R *= g;
            }
            // ---- mono / wide
            mixMono += clampf((fMono ? 1.f : 0.f) - mixMono, -ramp, ramp);
            mixWide += clampf((fWide ? 1.f : 0.f) - mixWide, -ramp, ramp);
            if (mixMono > 0.f || mixWide > 0.f) {
                float mid = (L + R) * 0.5f, side = (L - R) * 0.5f;
                side *= (1.f - mixMono) * (1.f + mixWide);
                L = mid + side; R = mid - side;
            }
            // ---- master FX chain (4 slots), processed per sample through a 1-sample block
            if (mfx[0].type.load(std::memory_order_relaxed) | mfx[1].type.load(std::memory_order_relaxed) |
                mfx[2].type.load(std::memory_order_relaxed) | mfx[3].type.load(std::memory_order_relaxed)) {
                for (int s2 = 0; s2 < 4; ++s2) mfx[s2].process(&L, &R, 1, mEff + MP_FX0 + s2 * 4);
            }
            // ---- pumping compressor (peak detector, smoothed gain reduction; optional key track)
            {
                float x = keyTrack >= 0 ? keyBuf[i] : std::max(std::fabs(L), std::fabs(R));
                float lvl = 20.f * std::log10(x + 1e-9f);
                float over = lvl - cThr, target = 0.f;
                if (over >= 3.f) target = over * cSlope;
                else if (over > -3.f) target = (over + 3.f) * (over + 3.f) / 12.f * cSlope;
                compGRs += (target - compGRs) * (target > compGRs ? cAtk : cRel);
                if (compGRs > grMax) grMax = compGRs;
                if (cMix > 0.f) {
                    float g = std::pow(10.f, (cMake - compGRs) / 20.f);
                    L = L + (L * g - L) * cMix; R = R + (R * g - R) * cMix;
                }
            }
            L = softclip(L * drv) * vol; R = softclip(R * drv) * vol;
            if (clickEnv > 1e-4f && (clickAt < 0 || i >= clickAt)) {
                clickPh += clickFreq / sr; if (clickPh >= 1.f) clickPh -= 1.f;
                float c = std::sin(kTwoPi * clickPh) * clickEnv; clickEnv *= 0.9985f;
                L += c; R += c;
            }
            out[(done + i) * 2] = L; out[(done + i) * 2 + 1] = R;
            float a = std::max(std::fabs(L), std::fabs(R)); if (a > pk) pk = a;
        }
        compGR.store(grMax, std::memory_order_relaxed);
        peak.store(pk, std::memory_order_relaxed);
        done += n;
    }
}
