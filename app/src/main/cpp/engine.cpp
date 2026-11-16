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
    dcL.setLP(8.f, sr); dcR.setLP(8.f, sr);
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
            songPos = 0; patStart = 0; lastBoundary = -1;
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

void Engine::fireStep(int t, long long k, int offset) {
    Track &tr = tracks[t];
    const Pattern &P = patterns[curPattern.load(std::memory_order_relaxed)];
    int len = std::clamp((int)P.length[t], 1, kMaxSteps);
    long long rel = k - patStart;
    int idx = (int)(rel % len);
    int loop = (int)(rel / len);
    tr.curStep.store(idx, std::memory_order_relaxed);
    const Step &s = P.steps[t][idx];
    if (!s.on) return;
    if (!evalCond(tr, s, loop)) return;
    if (tr.mute.load(std::memory_order_relaxed)) return;
    const float sps = sr * 60.f / (bpm.load() * 4.f);
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
    if (!playing.load(std::memory_order_relaxed)) return;
    const double stepsPerSample = bpm.load() / 60.0 * 4.0 / sr;
    const double sw = clampf(swing.load(), 50.f, 75.f) / 100.0 * 2.0 - 1.0;
    for (int i = 0; i < n; ++i) {
        int cp = curPattern.load(std::memory_order_relaxed);
        const Pattern *P = &patterns[cp];
        // bar boundary (master length = longest track): pattern queue / song advance
        {
            int masterLen = 1;
            for (int t = 0; t < kTracks; ++t) masterLen = std::max(masterLen, std::clamp((int)P->length[t], 1, kMaxSteps));
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
                    for (auto &tr : tracks) { tr.nextK = patStart; tr.ratchLeft = 0; }
                    curPattern.store(next); queuedPattern.store(next); cp = next; P = &patterns[cp];
                }
            }
        }
        for (int t = 0; t < kTracks; ++t) {
            Track &tr = tracks[t];
            int len = std::clamp((int)P->length[t], 1, kMaxSteps);
            for (int guard = 0; guard < 4; ++guard) {
                long long k = tr.nextK;
                long long rel = k - patStart;
                const Step &s = P->steps[t][(int)(rel % len)];
                double off = (s.on ? s.micro / 24.0 : 0.0) + ((rel & 1) ? sw : 0.0);
                if (songPos >= (double)k + off) { fireStep(t, k, i); tr.nextK = k + 1; }
                else break;
            }
            if (tr.ratchLeft > 0) {
                tr.ratchCount -= 1.f;
                if (tr.ratchCount <= 0.f) {
                    if (!tr.mute.load(std::memory_order_relaxed)) emit(t, tr.ratchTP, i);
                    --tr.ratchLeft; tr.ratchCount += tr.ratchInterval;
                }
            }
        }
        songPos += stepsPerSample;
    }
    relPos.store(songPos - (double)patStart, std::memory_order_relaxed);
}

void Engine::process(float *out, int frames) {
    int done = 0;
    while (done < frames) {
        const int n = std::min(256, frames - done);
        nev = 0;
        Cmd c;
        while (cmds.pop(c)) handleCmd(c);
        runSequencer(n);

        std::memset(busL, 0, sizeof(float) * n); std::memset(busR, 0, sizeof(float) * n);
        std::memset(dlyL, 0, sizeof(float) * n); std::memset(dlyR, 0, sizeof(float) * n);
        std::memset(revL, 0, sizeof(float) * n); std::memset(revR, 0, sizeof(float) * n);

        bool anySolo = false;
        for (auto &tr : tracks) anySolo |= tr.solo.load(std::memory_order_relaxed);
        for (int t = 0; t < kTracks; ++t) {
            Track &tr = tracks[t];
            Machine *m = tr.mach;
            std::memset(tbufL, 0, sizeof(float) * n); std::memset(tbufR, 0, sizeof(float) * n);
            int pos = 0;
            for (int e = 0; e < nev; ++e) {
                if (evs[e].track != t) continue;
                int off = std::clamp(evs[e].offset, 0, n);
                if (off > pos) { m->render(tbufL + pos, tbufR + pos, off - pos, tr.active); pos = off; }
                if (evs[e].tp.vel < 0.f) { m->noteOff(evs[e].tp.note); continue; }
                std::memcpy(tr.active, evs[e].tp.p, sizeof(tr.active));
                m->trigger(evs[e].tp);
                tr.trigCount.fetch_add(1, std::memory_order_relaxed);
            }
            if (pos < n) m->render(tbufL + pos, tbufR + pos, n - pos, tr.active);

            // mixer (gains ramp over the block to avoid zipper noise)
            float lv = tr.active[TP_LEVEL]; lv = lv * lv * 1.6f;
            if (anySolo && !tr.solo.load(std::memory_order_relaxed)) lv = 0.f;
            float pa = clampf(tr.active[TP_PAN], 0.f, 1.f) * kPi * 0.5f;
            float tgL = std::cos(pa) * 1.41421f * lv, tgR = std::sin(pa) * 1.41421f * lv;
            float tgD = tr.active[TP_DLY] * tr.active[TP_DLY], tgV = tr.active[TP_REV] * tr.active[TP_REV];
            float gL = trackGL[t], gR = trackGR[t], gD = trackGD[t], gV = trackGV[t];
            const float inv = 1.f / n;
            const float dL = (tgL - gL) * inv, dR = (tgR - gR) * inv, dD = (tgD - gD) * inv, dV = (tgV - gV) * inv;
            float tpk = tr.peak.load(std::memory_order_relaxed) * 0.9f;
            for (int i = 0; i < n; ++i) {
                gL += dL; gR += dR; gD += dD; gV += dV;
                float l = tbufL[i] * gL, r = tbufR[i] * gR;
                float a = std::max(std::fabs(l), std::fabs(r)); if (a > tpk) tpk = a;
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
        delay.target = std::min((float)delay.bl.size() - 4.f, kDlySteps[std::clamp((int)(master[MP_DTIME] * 7.99f), 0, 7)] * sps);
        const float dfb = master[MP_DFB] * 0.95f;
        delay.tl.setLP(expo(master[MP_DTONE], 400.f, 16000.f), sr); delay.tr.a = delay.tl.a;
        const float dlev = master[MP_DLEVEL] * 1.2f;
        const float rfb = 0.7f + master[MP_RSIZE] * 0.28f, rdamp = master[MP_RDAMP] * 0.6f;
        const float rlev = master[MP_RLEVEL] * 1.5f;
        const float drv = 1.f + master[MP_DRIVE] * 6.f;
        const float vol = master[MP_VOLUME] * master[MP_VOLUME] * 1.25f;
        float pk = peak.load(std::memory_order_relaxed) * 0.9f;
        const int dsize = (int)delay.bl.size();
        for (int i = 0; i < n; ++i) {
            delay.len += (delay.target - delay.len) * 0.0005f;
            float dl = delay.read(delay.bl, delay.len), dr = delay.read(delay.br, delay.len);
            delay.bl[delay.w] = (dlyL[i] + dlyR[i]) * 0.5f + delay.tr.lp(dr) * dfb;
            delay.br[delay.w] = delay.tl.lp(dl) * dfb;
            if (++delay.w >= dsize) delay.w = 0;
            float rl, rr; reverb.tick(revL[i], revR[i], rfb, rdamp, rl, rr);
            float L = busL[i] + dl * dlev + rl * rlev;
            float R = busR[i] + dr * dlev + rr * rlev;
            L = dcL.hp(L); R = dcR.hp(R);
            L = softclip(L * drv) * vol; R = softclip(R * drv) * vol;
            out[(done + i) * 2] = L; out[(done + i) * 2 + 1] = R;
            float a = std::max(std::fabs(L), std::fabs(R)); if (a > pk) pk = a;
        }
        peak.store(pk, std::memory_order_relaxed);
        done += n;
    }
}
