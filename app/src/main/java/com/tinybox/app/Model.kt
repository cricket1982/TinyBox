package com.tinybox.app

import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.roundToInt

const val TRACKS = 8
const val MAX_STEPS = 64
const val PATTERNS = 8
const val TRACK_PARAMS = 16
const val MASTER_PARAMS = 24

enum class Kind { DRUM, SLICER, SYNTH, GRAIN }

class MachineDef(val name: String, val short: String, val kind: Kind, val params: List<String>, val defaults: List<Float>)

object Defs {
    val machines = listOf(
        MachineDef("BD Classic", "BD", Kind.DRUM,
            listOf("Pitch", "Decay", "Sweep", "SwpTime", "Click", "Drive", "Tone", "Noise"),
            listOf(.35f, .45f, .5f, .35f, .3f, 0f, .7f, 0f)),
        MachineDef("SD Classic", "SD", Kind.DRUM,
            listOf("Pitch", "Decay", "Snappy", "NzDecay", "NzTone", "Detune", "Sweep", "Drive"),
            listOf(.4f, .35f, .55f, .4f, .5f, .4f, .2f, 0f)),
        MachineDef("HH Metal", "HH", Kind.DRUM,
            listOf("Tune", "Decay", "Color", "Metal", "Reso", "Drive"),
            listOf(.5f, .15f, .6f, .6f, .3f, 0f)),
        MachineDef("Clap", "CP", Kind.DRUM,
            listOf("Tone", "Decay", "Spread", "Reps", "Q", "Drive", "Room"),
            listOf(.5f, .4f, .4f, .5f, .3f, 0f, .4f)),
        MachineDef("FM Perc", "FM", Kind.DRUM,
            listOf("Pitch", "Decay", "Ratio", "FM Amt", "FM Dec", "Sweep", "Noise", "Drive"),
            listOf(.45f, .35f, .3f, .4f, .3f, .2f, .05f, 0f)),
        MachineDef("Metal Bell", "CB", Kind.DRUM,
            listOf("Pitch", "Decay", "Detune", "Tone", "Reso", "Drive"),
            listOf(.5f, .3f, .45f, .4f, .3f, 0f)),
        MachineDef("PM Drum", "PMD", Kind.DRUM,
            listOf("Pitch", "Decay", "Damp", "Tension", "Strike", "Hardness", "Wires", "Shape", "Drive"),
            listOf(.3f, .45f, .4f, .3f, .3f, .5f, 0f, 0f, 0f)),
        MachineDef("PM Cymbal", "PMC", Kind.DRUM,
            listOf("Pitch", "Decay", "Damp", "Spread", "Strike", "Hardness", "Sizzle", "Bell", "Drive"),
            listOf(.4f, .55f, .3f, .6f, .5f, .7f, .4f, .2f, 0f)),
        MachineDef("PM Bar", "PMB", Kind.DRUM,
            listOf("Pitch", "Decay", "Damp", "Material", "Strike", "Hardness", "Tube", "Noise", "Drive"),
            listOf(.5f, .45f, .3f, .5f, .3f, .5f, .3f, .05f, 0f)),
        MachineDef("Slicer", "SLC", Kind.SLICER,
            listOf("Pitch", "Start", "Length", "Decay", "Reverse", "Filter", "Reso", "Drive", "Choke", "Gain"),
            listOf(.5f, 0f, 1f, 1f, 0f, .5f, .2f, 0f, 1f, .77f)),
        MachineDef("Poly Synth", "SYN", Kind.SYNTH,
            listOf("Wave", "Detune", "Sub", "Cutoff", "Reso", "FEnv", "Attack", "Decay", "Sustain", "Release", "Vibrato", "Drive"),
            listOf(0f, .3f, .2f, .45f, .3f, .35f, .05f, .4f, .6f, .35f, 0f, 0f)),
        MachineDef("Granular", "GRN", Kind.GRAIN,
            listOf("Position", "Size", "Density", "Spray", "Pitch", "Jitter", "Spread", "Reverse", "Scan", "Attack", "Release", "Tone"),
            listOf(.3f, .5f, .5f, .2f, .5f, .1f, .5f, 0f, .5f, .25f, .4f, .85f)),
        MachineDef("FM Poly", "FMS", Kind.SYNTH,
            listOf("Ratio", "Fine", "Index", "IdxEnv", "IdxDec", "Feedbk", "Attack", "Decay", "Sustain", "Release", "Vibrato", "Drive"),
            listOf(.1f, .5f, .2f, .35f, .35f, .1f, .02f, .45f, .5f, .35f, 0f, 0f)),
        MachineDef("PM String", "STR", Kind.SYNTH,
            listOf("Decay", "Damp", "Pick", "Bright", "Body", "Detune", "Release", "Drive"),
            listOf(.55f, .35f, .3f, .6f, .3f, .2f, .35f, 0f)),
        MachineDef("Chip Perc", "CHP", Kind.DRUM,
            listOf("Pitch", "Decay", "Sweep", "SwpTime", "Noise", "Metal", "Bits", "Rate", "Duty"),
            listOf(.45f, .3f, .3f, .3f, .2f, 0f, .3f, .1f, .5f)),
        MachineDef("KS Drum", "KSD", Kind.DRUM,
            listOf("Pitch", "Decay", "Blend", "Bright", "Damp", "Bend", "Drive"),
            listOf(.35f, .4f, .5f, .7f, .3f, .3f, 0f)),
        MachineDef("Vox Perc", "VOX", Kind.DRUM,
            listOf("Vowel", "Decay", "Pitch", "Breath", "Shift", "Sweep", "Q", "Drive"),
            listOf(.3f, .3f, .4f, .5f, .5f, .5f, .4f, 0f)),
        MachineDef("West Coast", "WST", Kind.SYNTH,
            listOf("Ratio", "FM", "Fold", "Symm", "FoldEnv", "Vibrato", "Attack", "Decay", "Sustain", "Release", "LPG Res", "Drive"),
            listOf(.1f, .3f, .3f, .5f, .4f, 0f, .05f, .45f, .3f, .4f, .2f, 0f)),
        MachineDef("Phase Dist", "PD", Kind.SYNTH,
            listOf("Wave", "DCW", "DCW Env", "DCW Dec", "Reso", "Detune", "Attack", "Decay", "Sustain", "Release", "Vibrato", "Drive"),
            listOf(0f, .3f, .5f, .4f, .3f, .3f, .03f, .5f, .6f, .4f, 0f, 0f)),
        MachineDef("Formant", "FMT", Kind.SYNTH,
            listOf("Vowel", "VowelEnv", "Shift", "Q", "Breath", "Detune", "Attack", "Decay", "Sustain", "Release", "Vibrato", "Drive"),
            listOf(.2f, .65f, .5f, .5f, .1f, .3f, .1f, .5f, .7f, .45f, .2f, 0f)),
    )
    /** order + group names for the machine picker */
    val pickerOrder = listOf(0, 1, 2, 3, 4, 5, 14, 15, 16, 6, 7, 8, 13, 9, 11, 10, 12, 17, 18, 19)
    fun group(m: Int) = when (m) {
        in 0..5 -> "Classic drum"; 14, 15, 16 -> "Unusual drum"; 6, 7, 8, 13 -> "Physical model"
        9, 11 -> "Sample"; else -> "Synth"
    }
    val trackParams = listOf("Level", "Pan", "Delay", "Reverb")
    val trackDefaults = listOf(.75f, .5f, 0f, 0f)
    val masterParams = listOf("DlyTime", "DlyFdbk", "DlyTone", "DlyLvl", "RevSize", "RevDamp", "RevLvl", "Drive", "Volume",
        "CmpThr", "CmpRatio", "CmpAtk", "CmpRel", "CmpGain", "CmpMix")
    val masterDefaults = listOf(.3f, .4f, .6f, .6f, .6f, .4f, .6f, 0f, .8f, .7f, .45f, .6f, .5f, 0f, 0f)
    val vowels = listOf("A", "E", "I", "O", "U")
    fun vowelName(v: Float) = vowels[(v * 4f + .5f).toInt().coerceIn(0, 4)]

    val conditions: List<String> = buildList {
        addAll(listOf("—", "FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"))
        for (b in 2..8) for (a in 1..b) add("$a:$b")
    }
    val chords = listOf("—", "maj", "min", "maj7", "min7", "sus4", "5th", "oct", "7")
    val fmRatios = listOf("0.5", "1", "1.5", "2", "2.5", "3", "3.5", "4", "5", "6", "7", "9")
    fun usesSample(m: Int) = m == 9 || m == 11
    val delaySteps = listOf("1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1/1")
    private val noteNames = listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
    fun noteName(n: Int) = noteNames[((n % 12) + 12) % 12] + (n / 12 - 1)

    fun paramName(machine: Int, i: Int): String =
        if (i >= 12) trackParams[i - 12] else machines[machine].params.getOrElse(i) { "" }

    fun paramDefault(machine: Int, i: Int): Float =
        if (i >= 12) trackDefaults[i - 12] else machines[machine].defaults.getOrElse(i) { 0.5f }

    fun formatParam(machine: Int, i: Int, v: Float): String {
        val name = paramName(machine, i)
        return when {
            i == 13 -> { val p = ((v - .5f) * 200).roundToInt(); if (p == 0) "C" else if (p < 0) "L${-p}" else "R$p" }
            machine == 9 && i == 0 -> { val s = ((v - .5f) * 48).roundToInt(); if (s > 0) "+$s" else "$s" }
            machine == 9 && (i == 4 || i == 8) -> if (v > .5f) "On" else "Off"
            machine == 9 && i == 5 -> { val p = ((v - .5f) * 200).roundToInt(); if (p == 0) "Off" else if (p < 0) "LP${-p}" else "HP$p" }
            machine == 9 && i == 3 -> if (v >= .99f) "Hold" else (v * 100).roundToInt().toString()
            name == "Ratio" && machine == 4 -> String.format("%.2f", .5f + v * 7.5f)
            machine == 11 && i == 4 -> { val st = ((v - .5f) * 48).roundToInt(); if (st > 0) "+$st" else "$st" }
            machine == 11 && i == 8 -> { val p = ((v - .5f) * 200).roundToInt(); if (p == 0) "Stop" else if (p < 0) "<$p" else ">$p" }
            machine == 12 && i == 0 -> fmRatios[(v * 11.99f).toInt().coerceIn(0, 11)]
            machine == 17 && i == 0 -> fmRatios[(v * 11.99f).toInt().coerceIn(0, 11)]
            machine == 18 && i == 0 -> listOf("Saw", "Square", "Reso")[(v * 2.99f).toInt().coerceIn(0, 2)]
            machine == 14 && i == 5 -> if (v > .5f) "On" else "Off"
            machine == 14 && i == 2 -> { val o = ((v - .5f) * 8f); String.format("%+.1f", o) }
            (machine == 16 || machine == 19) && i == 0 -> vowelName(v)
            (machine == 19 && i == 1) || (machine == 16 && i == 5) -> { val p = ((v - .5f) * 200).roundToInt(); if (p > 0) "+$p" else "$p" }
            else -> (v * 100).roundToInt().toString()
        }
    }
    fun formatMaster(i: Int, v: Float): String = when (i) {
        0 -> delaySteps[(v * 7.99f).toInt().coerceIn(0, 7)]
        9 -> "${(-40f + v * 40f).roundToInt()}dB"
        10 -> String.format("%.1f:1", 1.0 * Math.pow(20.0, v.toDouble()))
        11 -> { val ms = 0.1 * Math.pow(1000.0, v.toDouble()); if (ms < 10) String.format("%.1fms", ms) else "${ms.roundToInt()}ms" }
        12 -> "${(20.0 * Math.pow(50.0, v.toDouble())).roundToInt()}ms"
        13 -> "+${(v * 24f).roundToInt()}dB"
        14 -> if (v <= 0.001f) "Off" else "${(v * 100).roundToInt()}%"
        else -> (v * 100).roundToInt().toString()
    }
}

class StepData {
    var on = false; var vel = 100; var prob = 100; var cond = 0; var ratchet = 1; var chord = 0
    var note = 60; var micro = 0; var len = 1f
    val extra = IntArray(3) { -1 }   // extra chord notes
    val lock = FloatArray(TRACK_PARAMS) { -1f }
    fun copyFrom(o: StepData) {
        on = o.on; vel = o.vel; prob = o.prob; cond = o.cond; ratchet = o.ratchet; chord = o.chord
        note = o.note; micro = o.micro; len = o.len; o.lock.copyInto(lock); o.extra.copyInto(extra)
    }
    fun isDefault() = !on && vel == 100 && prob == 100 && cond == 0 && ratchet == 1 && chord == 0 &&
        note == 60 && micro == 0 && len == 1f && lock.all { it < 0f } && extra.all { it < 0 }
    fun clearExtra() { extra.fill(-1) }
    fun addNote(n: Int): Boolean {
        if (n == note || extra.contains(n)) return true
        val i = extra.indexOf(-1); if (i < 0) return false
        extra[i] = n; return true
    }
    fun noteCount() = 1 + extra.count { it >= 0 }
    fun hasLocks() = lock.any { it >= 0f }
}

class PatternData {
    val len = IntArray(TRACKS) { 16 }
    val steps = Array(TRACKS) { Array(MAX_STEPS) { StepData() } }
    fun copyFrom(o: PatternData) {
        o.len.copyInto(len)
        for (t in 0 until TRACKS) for (i in 0 until MAX_STEPS) steps[t][i].copyFrom(o.steps[t][i])
    }
}

class TrackData {
    var machine = 0
    val params = FloatArray(TRACK_PARAMS) { .5f }
    var mute = false
    var solo = false
    var sampleUri: String? = null
    var sampleName: String? = null
    var slices: IntArray = intArrayOf(0)
    var sliceSens = .6f
    fun setMachineDefaults(m: Int) {
        machine = m
        for (i in 0 until 12) params[i] = Defs.paramDefault(m, i)
    }
}

class Project {
    var name = "untitled"
    var bpm = 120f
    var swing = 50f
    val master = FloatArray(MASTER_PARAMS) { i -> Defs.masterDefaults.getOrElse(i) { .5f } }
    val tracks = Array(TRACKS) { TrackData() }
    val patterns = Array(PATTERNS) { PatternData() }
    val songPat = ArrayList<Int>()
    val songRep = ArrayList<Int>()
    var songMode = false
    var songLoop = true

    // ---------------- engine sync
    fun pushStep(p: Int, t: Int, i: Int) {
        val s = patterns[p].steps[t][i]
        pushStepFields(p, t, i)
        for (k in 0 until TRACK_PARAMS) Native.setLock(p, t, i, k, s.lock[k])
    }
    fun pushStepFields(p: Int, t: Int, i: Int) {
        val s = patterns[p].steps[t][i]
        Native.setStep(p, t, i, if (s.on) 1 else 0, s.vel, s.prob, s.cond, s.ratchet, s.chord, s.note, s.micro, s.len,
            s.extra[0], s.extra[1], s.extra[2])
    }
    fun pushSong(startRow: Int = 0) {
        Native.setSong(songPat.toIntArray(), songRep.toIntArray())
        Native.setSongMode(songMode && songPat.isNotEmpty(), songLoop, startRow)
    }
    fun pushPattern(p: Int) {
        for (t in 0 until TRACKS) {
            Native.setLength(p, t, patterns[p].len[t])
            for (i in 0 until MAX_STEPS) pushStep(p, t, i)
        }
    }
    fun pushTrack(t: Int, withMachine: Boolean) {
        val tr = tracks[t]
        if (withMachine) Native.setMachine(t, tr.machine)
        for (i in 0 until TRACK_PARAMS) Native.setParam(t, i, tr.params[i])
        Native.setMute(t, tr.mute)
        Native.setSolo(t, tr.solo)
        Native.setSlices(t, tr.slices)
    }
    fun pushAll() {
        Native.setBpm(bpm); Native.setSwing(swing)
        for (i in 0 until MASTER_PARAMS) Native.setMaster(i, master[i])
        for (t in 0 until TRACKS) pushTrack(t, true)
        for (p in 0 until PATTERNS) pushPattern(p)
        pushSong()
    }

    // ---------------- JSON
    fun toJson(): JSONObject {
        val o = JSONObject()
        o.put("format", "tinybox-1"); o.put("name", name); o.put("bpm", bpm.toDouble()); o.put("swing", swing.toDouble())
        o.put("master", JSONArray(master.map { it.toDouble() }))
        val ta = JSONArray()
        for (tr in tracks) {
            val j = JSONObject()
            j.put("machine", tr.machine); j.put("mute", tr.mute); j.put("solo", tr.solo)
            j.put("params", JSONArray(tr.params.map { it.toDouble() }))
            tr.sampleUri?.let { j.put("sampleUri", it) }
            tr.sampleName?.let { j.put("sampleName", it) }
            j.put("slices", JSONArray(tr.slices.toList()))
            j.put("sliceSens", tr.sliceSens.toDouble())
            ta.put(j)
        }
        o.put("tracks", ta)
        val pa = JSONArray()
        for (p in patterns) {
            val j = JSONObject()
            j.put("len", JSONArray(p.len.toList()))
            val sa = JSONArray()
            for (t in 0 until TRACKS) for (i in 0 until MAX_STEPS) {
                val s = p.steps[t][i]
                if (s.isDefault()) continue
                val sj = JSONObject()
                sj.put("t", t); sj.put("i", i); sj.put("on", s.on); sj.put("vel", s.vel); sj.put("prob", s.prob)
                sj.put("cond", s.cond); sj.put("rat", s.ratchet); sj.put("chord", s.chord); sj.put("note", s.note)
                sj.put("micro", s.micro); sj.put("len", s.len.toDouble())
                if (s.extra.any { it >= 0 }) sj.put("ex", JSONArray(s.extra.toList()))
                if (s.hasLocks()) {
                    val la = JSONArray()
                    for (k in 0 until TRACK_PARAMS) if (s.lock[k] >= 0f) la.put(JSONArray(listOf(k, s.lock[k].toDouble())))
                    sj.put("locks", la)
                }
                sa.put(sj)
            }
            j.put("steps", sa)
            pa.put(j)
        }
        o.put("patterns", pa)
        val sa = JSONArray()
        for (k in songPat.indices) sa.put(JSONArray(listOf(songPat[k], songRep[k])))
        o.put("song", sa); o.put("songMode", songMode); o.put("songLoop", songLoop)
        return o
    }

    companion object {
        fun fromJson(o: JSONObject): Project {
            val pr = Project()
            pr.name = o.optString("name", "untitled")
            pr.bpm = o.optDouble("bpm", 120.0).toFloat()
            pr.swing = o.optDouble("swing", 50.0).toFloat()
            o.optJSONArray("master")?.let { a -> for (i in 0 until minOf(a.length(), MASTER_PARAMS)) pr.master[i] = a.getDouble(i).toFloat() }
            o.optJSONArray("tracks")?.let { ta ->
                for (t in 0 until minOf(ta.length(), TRACKS)) {
                    val j = ta.getJSONObject(t); val tr = pr.tracks[t]
                    tr.machine = j.optInt("machine", 0).coerceIn(0, Defs.machines.size - 1)
                    tr.mute = j.optBoolean("mute", false)
                    tr.solo = j.optBoolean("solo", false)
                    j.optJSONArray("params")?.let { a -> for (i in 0 until minOf(a.length(), TRACK_PARAMS)) tr.params[i] = a.getDouble(i).toFloat() }
                    tr.sampleUri = if (j.has("sampleUri")) j.getString("sampleUri") else null
                    tr.sampleName = if (j.has("sampleName")) j.getString("sampleName") else null
                    j.optJSONArray("slices")?.let { a -> tr.slices = IntArray(a.length()) { a.getInt(it) } }
                    if (tr.slices.isEmpty()) tr.slices = intArrayOf(0)
                    tr.sliceSens = j.optDouble("sliceSens", .6).toFloat()
                }
            }
            o.optJSONArray("patterns")?.let { pa ->
                for (p in 0 until minOf(pa.length(), PATTERNS)) {
                    val j = pa.getJSONObject(p); val pd = pr.patterns[p]
                    j.optJSONArray("len")?.let { a -> for (t in 0 until minOf(a.length(), TRACKS)) pd.len[t] = a.getInt(t).coerceIn(1, MAX_STEPS) }
                    j.optJSONArray("steps")?.let { sa ->
                        for (k in 0 until sa.length()) {
                            val sj = sa.getJSONObject(k)
                            val t = sj.getInt("t"); val i = sj.getInt("i")
                            if (t !in 0 until TRACKS || i !in 0 until MAX_STEPS) continue
                            val s = pd.steps[t][i]
                            s.on = sj.optBoolean("on"); s.vel = sj.optInt("vel", 100); s.prob = sj.optInt("prob", 100)
                            s.cond = sj.optInt("cond", 0); s.ratchet = sj.optInt("rat", 1); s.chord = sj.optInt("chord", 0)
                            s.note = sj.optInt("note", 60); s.micro = sj.optInt("micro", 0); s.len = sj.optDouble("len", 1.0).toFloat()
                            sj.optJSONArray("ex")?.let { ea -> for (q in 0 until minOf(3, ea.length())) s.extra[q] = ea.getInt(q) }
                            sj.optJSONArray("locks")?.let { la ->
                                for (q in 0 until la.length()) {
                                    val e = la.getJSONArray(q); val idx = e.getInt(0)
                                    if (idx in 0 until TRACK_PARAMS) s.lock[idx] = e.getDouble(1).toFloat()
                                }
                            }
                        }
                    }
                }
            }
            o.optJSONArray("song")?.let { sa ->
                for (k in 0 until minOf(sa.length(), 64)) {
                    val e = sa.getJSONArray(k)
                    pr.songPat.add(e.getInt(0).coerceIn(0, PATTERNS - 1)); pr.songRep.add(e.getInt(1).coerceIn(1, 64))
                }
            }
            pr.songMode = o.optBoolean("songMode", false)
            pr.songLoop = o.optBoolean("songLoop", true)
            return pr
        }

        /** A small starter kit + beat so the app makes sound straight away. */
        fun demo(): Project {
            val pr = Project()
            pr.name = "demo"
            val kit = intArrayOf(0, 1, 2, 3, 6, 7, 9, 10)
            for (t in 0 until TRACKS) {
                val tr = pr.tracks[t]
                tr.setMachineDefaults(kit[t])
                for (i in 0 until 4) tr.params[12 + i] = Defs.trackDefaults[i]
            }
            pr.tracks[2].params[13] = .62f
            pr.tracks[4].params[15] = .35f
            pr.tracks[5].params[15] = .45f; pr.tracks[5].params[12] = .55f
            pr.tracks[7].params[14] = .35f; pr.tracks[7].params[15] = .3f; pr.tracks[7].params[12] = .6f
            val p = pr.patterns[0]
            for (i in 0 until 16 step 4) p.steps[0][i].on = true
            p.steps[0][14].apply { on = true; prob = 50; vel = 80 }
            p.steps[1][4].on = true; p.steps[1][12].on = true
            p.steps[1][15].apply { on = true; vel = 50; cond = Defs.conditions.indexOf("4:4") }
            for (i in 0 until 16) p.steps[2][i].apply {
                on = true; vel = if (i % 4 == 2) 110 else 70
                if (i % 2 == 1) { prob = 65; micro = -2 }
            }
            p.steps[2][7].ratchet = 3
            p.steps[3][12].apply { on = true; cond = Defs.conditions.indexOf("2:2") }
            p.steps[4][3].on = true; p.steps[4][10].on = true; p.steps[4][11].apply { on = true; micro = 4; vel = 80; lock[0] = .38f }
            p.steps[5][0].apply { on = true; cond = Defs.conditions.indexOf("1:2") }
            val bass = intArrayOf(36, -1, 36, -1, 39, -1, -1, 36, -1, 43, -1, 36, 41, -1, 39, -1)
            for (i in 0 until 16) if (bass[i] >= 0) p.steps[7][i].apply { on = true; note = bass[i]; len = .75f }
            p.steps[7][0].lock[3] = .6f
            pr.tracks[7].params[3] = .3f; pr.tracks[7].params[5] = .5f; pr.tracks[7].params[8] = .2f; pr.tracks[7].params[9] = .2f
            // pattern B: variation
            pr.patterns[1].copyFrom(p)
            pr.patterns[1].steps[0][10].on = true
            pr.patterns[1].steps[1][4].ratchet = 2
            pr.songPat.addAll(listOf(0, 1)); pr.songRep.addAll(listOf(2, 2))
            return pr
        }
    }
}

