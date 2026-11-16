package com.tinybox.app

import android.annotation.SuppressLint
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

@SuppressLint("ViewConstructor")
class GrooveView(private val app: MainActivity) : View(app) {

    private val proj get() = app.project

    // ---------------------------------------------------------------- UI state
    var selTrack = 0
    var showMaster = false
    var page = 0
    var editStep = -1           // step index being edited (p-lock mode) or -1
    var rec = false
    var fillHeld = false
    var octave = 3              // keyboard base = C(octave)
    var curPattern = 0          // pattern shown/edited
    var pageMode = 0            // 0 = SEQ, 1 = MIX, 2 = SONG, 3 = FX
    var sliceEdit = false
    var zoom = 1
    var viewStart = 0
    var fxLatch = false
    val fxState = BooleanArray(12)
    var fxX = .5f; var fxY = .2f
    var songSel = 0             // selected song row
    private var clipboard: PatternData? = null
    private val status = IntArray(30)
    private val lastTrig = IntArray(TRACKS)
    private val flash = LongArray(TRACKS)
    private var playing = false

    // ---------------------------------------------------------------- colours / paints
    private val trackCol = intArrayOf(
        0xFFFF6B5A.toInt(), 0xFFFFA94D.toInt(), 0xFFF5E663.toInt(), 0xFF6FE07A.toInt(),
        0xFF4FD6C8.toInt(), 0xFF5AA9FF.toInt(), 0xFFA884FF.toInt(), 0xFFFF7AC8.toInt()
    )
    private val bg = 0xFF121316.toInt()
    private val panel = 0xFF1D1F24.toInt()
    private val panel2 = 0xFF272A31.toInt()
    private val dim = 0xFF4A4E58.toInt()
    private val textCol = 0xFFE6E6E6.toInt()
    private val subText = 0xFF9AA0AA.toInt()
    private val lockCol = 0xFFFFD34D.toInt()
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = textCol; textAlign = Paint.Align.CENTER }

    // ---------------------------------------------------------------- layout
    private var u = 10f
    private val topBtns = ArrayList<Pair<String, RectF>>()
    private val trackRows = Array(TRACKS + 1) { RectF() }
    private val pageBtns = Array(4) { RectF() }
    private val lenMinus = RectF(); private val lenPlus = RectF(); private val lenBox = RectF()
    private val stepCells = Array(16) { RectF() }
    private val stepKnobs = Array(8) { RectF() }
    private val clrLockBtn = RectF(); private val doneBtn = RectF()
    private val soundKnobs = Array(16) { RectF() }
    private val perf = RectF()
    private val pads = Array(TRACKS) { RectF() }
    private val octDown = RectF(); private val octUp = RectF()
    private val keysRect = RectF()
    private val slicerBtns = ArrayList<Pair<String, RectF>>()
    private val waveRect = RectF()
    private var stepRowRect = RectF(); private var knobArea = RectF()

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) { layoutAll(w.toFloat(), h.toFloat()) }

    private fun layoutAll(w: Float, h: Float) {
        u = h / 100f
        // top bar
        topBtns.clear()
        val spec = listOf(
            "PLAY" to 9f, "BPM" to 9.5f, "SWING" to 8.5f, "|" to 1.2f,
            "SEQ" to 5.5f, "MIX" to 5.5f, "SONG" to 6.5f, "FX" to 5f, "|" to 1.2f,
            "A" to 4.3f, "B" to 4.3f, "C" to 4.3f, "D" to 4.3f, "E" to 4.3f, "F" to 4.3f, "G" to 4.3f, "H" to 4.3f, "|" to 1.2f,
            "FILL" to 6f, "REC" to 6.5f, "|" to 1.2f, "COPY" to 6.5f, "PASTE" to 6.5f, "CLR" to 5.5f, "|" to 1.2f,
            "SAVE" to 6f, "LOAD" to 6f, "NEW" to 5.5f
        )
        val total = spec.sumOf { it.second.toDouble() }.toFloat()
        val scale = (w - 2 * u) / total
        var x = u
        for ((id, wt) in spec) {
            val bw = wt * scale
            if (id != "|") topBtns.add(id to RectF(x + .3f * u, u, x + bw - .3f * u, 8f * u))
            x += bw
        }
        // track column
        val leftW = w * 0.16f
        val ty0 = 9.5f * u; val ty1 = h - u
        val rh = (ty1 - ty0) / (TRACKS + 1)
        for (i in 0..TRACKS) trackRows[i].set(u, ty0 + i * rh + .25f * u, leftW, ty0 + (i + 1) * rh - .25f * u)
        // right side
        val x0 = leftW + 1.5f * u; val x1 = w - u
        val pw = 6f * u
        for (i in 0 until 4) pageBtns[i].set(x0 + i * (pw + .6f * u), 9.5f * u, x0 + i * (pw + .6f * u) + pw, 13.5f * u)
        val lx = x0 + 4 * (pw + .6f * u) + 2f * u
        lenMinus.set(lx, 9.5f * u, lx + 4.5f * u, 13.5f * u)
        lenBox.set(lenMinus.right + .5f * u, 9.5f * u, lenMinus.right + 12f * u, 13.5f * u)
        lenPlus.set(lenBox.right + .5f * u, 9.5f * u, lenBox.right + 5f * u, 13.5f * u)
        val cw = (x1 - x0) / 16f
        for (i in 0 until 16) {
            val gap = if (i % 4 == 3) .9f * u else .35f * u
            stepCells[i].set(x0 + i * cw, 14.5f * u, x0 + (i + 1) * cw - gap, 27f * u)
        }
        stepRowRect.set(x0, 28.5f * u, x1, 41f * u)
        val sw = (x1 - x0) / 10f
        for (i in 0 until 8) stepKnobs[i].set(x0 + i * sw, 28.5f * u, x0 + (i + 1) * sw, 41f * u)
        clrLockBtn.set(x0 + 8 * sw + .5f * u, 30f * u, x0 + 9 * sw - .5f * u, 39.5f * u)
        doneBtn.set(x0 + 9 * sw + .5f * u, 30f * u, x1 - .5f * u, 39.5f * u)
        knobArea.set(x0, 42f * u, x1, 68f * u)
        val kw = (x1 - x0) / 8f
        for (i in 0 until 16) {
            val r = i / 8; val c = i % 8
            soundKnobs[i].set(x0 + c * kw, (42f + r * 13f) * u, x0 + (c + 1) * kw, (55f + r * 13f) * u)
        }
        perf.set(x0, 69.5f * u, x1, h - u)
        val pdw = (x1 - x0) / 8f
        for (i in 0 until TRACKS) pads[i].set(x0 + i * pdw + .4f * u, perf.top, x0 + (i + 1) * pdw - .4f * u, perf.bottom)
        octDown.set(x0, perf.top, x0 + 8f * u, perf.centerY() - .4f * u)
        octUp.set(x0, perf.centerY() + .4f * u, x0 + 8f * u, perf.bottom)
        keysRect.set(x0 + 9f * u, perf.top, x1, perf.bottom)
        slicerBtns.clear()
        val sl = listOf("LOAD", "DETECT", "SENS-", "SENS+", "EDIT", "ZOOM-", "ZOOM+")
        val bw = 8.5f * u
        for ((i, id) in sl.withIndex()) slicerBtns.add(id to RectF(x0 + i * (bw + .6f * u), perf.top, x0 + i * (bw + .6f * u) + bw, perf.top + 5f * u))
        waveRect.set(x0, perf.top + 6f * u, x1, perf.bottom)
        // granular: toolbar, small waveform, keyboard
        grainLoad.set(x0, perf.top, x0 + 9f * u, perf.top + 5f * u)
        grainWave.set(x0 + 10f * u, perf.top, x1, perf.top + 9f * u)
        grainOctDown.set(x0, perf.top + 10f * u, x0 + 8f * u, perf.top + 10f * u + (perf.bottom - perf.top - 10f * u) / 2 - .4f * u)
        grainOctUp.set(x0, grainOctDown.bottom + .8f * u, x0 + 8f * u, perf.bottom)
        grainKeys.set(x0 + 9f * u, perf.top + 10f * u, x1, perf.bottom)
        // mixer strips (8 tracks + master)
        val stw = (x1 - x0) / (TRACKS + 1)
        for (i in 0..TRACKS) {
            val l = x0 + i * stw + .3f * u; val r = x0 + (i + 1) * stw - .3f * u
            mixStrip[i].set(l, 9.5f * u, r, h - u)
            for (k in 0 until 3) mixKnob[i][k].set(l, (15f + k * 10f) * u, r, (25f + k * 10f) * u)
            val fy0 = 46f * u; val fy1 = h - 9f * u
            mixFader[i].set(l + (r - l) * .25f, fy0, l + (r - l) * .62f, fy1)
            mixMeter[i].set(l + (r - l) * .7f, fy0, l + (r - l) * .8f, fy1)
            mixMute[i].set(l + .4f * u, h - 7.5f * u, (l + r) / 2 - .3f * u, h - 1.5f * u)
            mixSolo[i].set((l + r) / 2 + .3f * u, h - 7.5f * u, r - .4f * u, h - 1.5f * u)
        }
        // song editor
        songBtns.clear()
        val sb = listOf("SONGMODE" to 16f, "SONGLOOP" to 10f, "SADD" to 10f, "SINS" to 10f, "SDEL" to 10f, "SCLR" to 10f)
        var sx = x0
        for ((id, bw2) in sb) { songBtns.add(id to RectF(sx, 9.5f * u, sx + bw2 * u, 14f * u)); sx += bw2 * u + .8f * u }
        songHint.set(sx + u, 9.5f * u, x1, 14f * u)
        // FX page
        fxLatchBtn.set(x0, 9.5f * u, x0 + 12f * u, 14f * u)
        fxClearBtn.set(x0 + 13f * u, 9.5f * u, x0 + 25f * u, 14f * u)
        val xyW = (x1 - x0) * .42f
        fxXY.set(x0, 15.5f * u, x0 + xyW, h - u)
        val gx0 = x0 + xyW + 1.5f * u
        val pw2 = (x1 - gx0) / 4f; val ph2 = (h - u - 15.5f * u) / 3f
        for (i in 0 until 11) {
            val c = i % 4; val r = i / 4
            fxPads[i].set(gx0 + c * pw2 + .4f * u, 15.5f * u + r * ph2 + .4f * u, gx0 + (c + 1) * pw2 - .4f * u, 15.5f * u + (r + 1) * ph2 - .4f * u)
        }
        val colW = (x1 - x0) / 4f; val rowH = (h - u - 16f * u) / 16f
        for (k in 0 until 64) {
            val c = k / 16; val r = k % 16
            songCells[k].set(x0 + c * colW + .3f * u, 16f * u + r * rowH + .2f * u, x0 + (c + 1) * colW - .3f * u, 16f * u + (r + 1) * rowH - .2f * u)
        }
    }

    private val grainLoad = RectF(); private val grainWave = RectF(); private val grainKeys = RectF()
    private val grainOctDown = RectF(); private val grainOctUp = RectF()
    private val mixStrip = Array(TRACKS + 1) { RectF() }
    private val mixKnob = Array(TRACKS + 1) { Array(3) { RectF() } }
    private val mixFader = Array(TRACKS + 1) { RectF() }
    private val mixMeter = Array(TRACKS + 1) { RectF() }
    private val mixMute = Array(TRACKS + 1) { RectF() }
    private val mixSolo = Array(TRACKS + 1) { RectF() }
    private val songBtns = ArrayList<Pair<String, RectF>>()
    private val songHint = RectF()
    private val songCells = Array(64) { RectF() }
    private val fxXY = RectF(); private val fxLatchBtn = RectF(); private val fxClearBtn = RectF()
    private val fxPads = Array(11) { RectF() }
    private val fxNames = listOf("REPEAT\n1/4", "REPEAT\n1/8", "REPEAT\n1/16", "REPEAT\n1/32", "REVERSE", "TAPE\nSTOP",
        "GATE\n1/16", "CRUSH", "DUB\nTHROW", "FREEZE", "LO-FI")

    // ---------------------------------------------------------------- helpers
    private val tr get() = proj.tracks[selTrack]
    private val def get() = Defs.machines[tr.machine]
    private val pat get() = proj.patterns[curPattern]
    private fun stepIndex(cell: Int) = page * 16 + cell

    fun refreshFromEngine() {
        Native.status(status)
        playing = status[0] == 1
        val enginePat = status[1]
        if (playing && enginePat != curPattern && enginePat in 0 until PATTERNS) curPattern = enginePat
        val now = SystemClock.uptimeMillis()
        for (t in 0 until TRACKS) if (status[10 + t] != lastTrig[t]) { lastTrig[t] = status[10 + t]; flash[t] = now }
        invalidate()
    }

    private fun drawBtn(c: Canvas, r: RectF, label: String, on: Boolean = false, col: Int = panel2, onCol: Int = 0xFF3D7BFF.toInt(), ts: Float = 2.4f) {
        fill.color = if (on) onCol else col
        c.drawRoundRect(r, .8f * u, .8f * u, fill)
        text.textSize = ts * u; text.color = if (on) Color.BLACK else textCol
        c.drawText(label, r.centerX(), r.centerY() + ts * u * .35f, text)
    }

    private fun drawKnob(c: Canvas, r: RectF, label: String, value: Float, valueText: String, col: Int, active: Boolean = true, locked: Boolean = false) {
        val cx = r.centerX(); val cy = r.top + r.height() * .42f
        val rad = min(r.width(), r.height()) * .3f
        stroke.strokeWidth = .9f * u; stroke.strokeCap = Paint.Cap.ROUND
        stroke.color = panel2
        val oval = RectF(cx - rad, cy - rad, cx + rad, cy + rad)
        c.drawArc(oval, 135f, 270f, false, stroke)
        stroke.color = if (!active) dim else if (locked) lockCol else col
        c.drawArc(oval, 135f, 270f * value.coerceIn(0f, 1f), false, stroke)
        text.color = if (active) textCol else dim
        text.textSize = 2.1f * u
        c.drawText(valueText, cx, cy + .8f * u, text)
        text.textSize = 1.9f * u; text.color = if (locked) lockCol else subText
        c.drawText(label, cx, r.bottom - .8f * u, text)
    }

    // ---------------------------------------------------------------- drawing
    override fun onDraw(c: Canvas) {
        c.drawColor(bg)
        drawTop(c)
        drawTracks(c)
        when (pageMode) {
            1 -> drawMixer(c)
            2 -> drawSong(c)
            3 -> drawFx(c)
            else -> { drawGridHeader(c); drawSteps(c); drawStepEditor(c); drawSoundKnobs(c); drawPerf(c) }
        }
    }

    private fun drawTop(c: Canvas) {
        for ((id, r) in topBtns) when (id) {
            "PLAY" -> drawBtn(c, r, if (playing) "■ STOP" else "▶ PLAY", playing, onCol = 0xFF6FE07A.toInt())
            "BPM" -> drawBtn(c, r, "${(proj.bpm * 10).roundToInt() / 10f} BPM")
            "SWING" -> drawBtn(c, r, "SW ${proj.swing.roundToInt()}%")
            "SEQ" -> drawBtn(c, r, "SEQ", pageMode == 0, onCol = 0xFFDADDE3.toInt())
            "MIX" -> drawBtn(c, r, "MIX", pageMode == 1, onCol = 0xFFDADDE3.toInt())
            "SONG" -> drawBtn(c, r, if (proj.songMode) "SONG ●" else "SONG", pageMode == 2, onCol = 0xFFDADDE3.toInt())
            "FX" -> drawBtn(c, r, if (fxState.any { it }) "FX ●" else "FX", pageMode == 3, onCol = 0xFFDADDE3.toInt())
            "FILL" -> drawBtn(c, r, "FILL", fillHeld, onCol = lockCol)
            "REC" -> drawBtn(c, r, "● REC", rec, onCol = 0xFFFF5050.toInt())
            "A", "B", "C", "D", "E", "F", "G", "H" -> {
                val p = id[0] - 'A'
                val queued = app.queuedPattern == p && p != curPattern
                drawBtn(c, r, id, p == curPattern, if (queued) 0xFF5A4A1A.toInt() else panel2)
            }
            else -> drawBtn(c, r, id)
        }
    }

    private fun drawTracks(c: Canvas) {
        val now = SystemClock.uptimeMillis()
        for (t in 0..TRACKS) {
            val r = trackRows[t]
            val isSel = if (t == TRACKS) showMaster else t == selTrack
            val sel = isSel && (t == TRACKS || !showMaster)
            fill.color = if (sel) 0xFF30343D.toInt() else panel
            c.drawRoundRect(r, .8f * u, .8f * u, fill)
            if (t == TRACKS) {
                text.textAlign = Paint.Align.LEFT; text.textSize = 2.4f * u; text.color = textCol
                c.drawText("MASTER", r.left + 2f * u, r.centerY() + .8f * u, text)
                // peak meter
                val pk = (status[18] / 1000f).coerceIn(0f, 1f)
                fill.color = if (pk > .95f) 0xFFFF5050.toInt() else 0xFF6FE07A.toInt()
                c.drawRect(r.right - 3f * u, r.bottom - .8f * u - (r.height() - 1.6f * u) * pk, r.right - 1.6f * u, r.bottom - .8f * u, fill)
                text.textAlign = Paint.Align.CENTER
                continue
            }
            val td = proj.tracks[t]
            // colour strip + trigger flash
            val age = now - flash[t]
            fill.color = trackCol[t]
            fill.alpha = if (age < 120) 255 else 150
            c.drawRoundRect(RectF(r.left, r.top, r.left + 1.2f * u, r.bottom), .5f * u, .5f * u, fill)
            fill.alpha = 255
            if (age < 120) { fill.color = trackCol[t]; fill.alpha = 60; c.drawRoundRect(r, .8f * u, .8f * u, fill); fill.alpha = 255 }
            if (isSel && !showMaster) { stroke.strokeWidth = .3f * u; stroke.color = trackCol[t]; c.drawRoundRect(r, .8f * u, .8f * u, stroke) }
            text.textAlign = Paint.Align.LEFT
            text.textSize = 2.3f * u; text.color = if (td.mute) dim else textCol
            c.drawText("${t + 1}  ${Defs.machines[td.machine].name}", r.left + 2.2f * u, r.top + r.height() * .45f, text)
            text.textSize = 1.7f * u; text.color = subText
            val sub = if (Defs.usesSample(td.machine)) (td.sampleName ?: "no sample loaded") else "len ${pat.len[t]}"
            c.drawText(sub.take(28), r.left + 2.2f * u, r.top + r.height() * .8f, text)
            text.textAlign = Paint.Align.CENTER
            // mute button
            val m = muteRect(r)
            drawBtn(c, m, "M", td.mute, onCol = 0xFFFF5050.toInt(), ts = 2f)
        }
    }

    private fun muteRect(r: RectF) = RectF(r.right - 5.5f * u, r.top + .7f * u, r.right - .7f * u, r.bottom - .7f * u)

    private fun drawGridHeader(c: Canvas) {
        val len = pat.len[selTrack]
        for (i in 0 until 4) {
            val has = i * 16 < len
            drawBtn(c, pageBtns[i], "${i + 1}", i == page, if (has) panel2 else panel, onCol = trackCol[selTrack], ts = 2f)
        }
        drawBtn(c, lenMinus, "−", ts = 2.4f)
        drawBtn(c, lenPlus, "+", ts = 2.4f)
        fill.color = panel; c.drawRoundRect(lenBox, .8f * u, .8f * u, fill)
        text.textSize = 2f * u; text.color = textCol
        c.drawText("LEN $len", lenBox.centerX(), lenBox.centerY() + .7f * u, text)
        text.textAlign = Paint.Align.LEFT; text.textSize = 2f * u; text.color = subText
        val hint = if (editStep >= 0) "Editing step ${editStep + 1}: turn sound knobs to p-lock • tap other steps to switch"
        else "Tap step = on/off • long-press step = edit (prob, cond, micro, ratchet, p-locks)"
        c.drawText(hint, lenPlus.right + 2f * u, lenBox.centerY() + .7f * u, text)
        text.textAlign = Paint.Align.CENTER
    }

    private fun drawSteps(c: Canvas) {
        val len = pat.len[selTrack]
        val cur = if (playing) status[2 + selTrack] else -1
        val col = trackCol[selTrack]
        val kind = def.kind
        for (cell in 0 until 16) {
            val i = stepIndex(cell)
            val r = stepCells[cell]
            val s = pat.steps[selTrack][i]
            val inLen = i < len
            fill.color = when {
                !inLen -> 0xFF17181B.toInt()
                s.on -> col
                cell % 4 == 0 -> 0xFF2E3138.toInt()
                else -> panel2
            }
            if (s.on && inLen) fill.alpha = 90 + (s.vel * 165 / 127)
            // micro timing: shift the pad a little
            val shift = if (s.on) s.micro / 24f * r.width() * .5f else 0f
            val rr = RectF(r.left + shift, r.top, r.right + shift, r.bottom)
            c.drawRoundRect(rr, u, u, fill)
            fill.alpha = 255
            if (i == cur) { stroke.strokeWidth = .45f * u; stroke.color = Color.WHITE; c.drawRoundRect(r, u, u, stroke) }
            if (i == editStep) { stroke.strokeWidth = .6f * u; stroke.color = lockCol; c.drawRoundRect(r, u, u, stroke) }
            if (!s.on || !inLen) {
                text.textSize = 1.7f * u; text.color = dim
                c.drawText("${i + 1}", r.centerX(), r.bottom - u, text)
                continue
            }
            text.color = Color.BLACK
            text.textSize = 1.9f * u
            var ly = r.top + 2.6f * u
            val more = if (s.noteCount() > 1) " +${s.noteCount() - 1}" else ""
            if (kind == Kind.SYNTH || kind == Kind.GRAIN) { c.drawText(Defs.noteName(s.note) + more + if (s.chord > 0) " " + Defs.chords[s.chord] else "", rr.centerX(), ly, text); ly += 2.3f * u }
            if (kind == Kind.SLICER) { c.drawText("S${s.note % 64 + 1}$more", rr.centerX(), ly, text); ly += 2.3f * u }
            if (s.prob < 100) { c.drawText("${s.prob}%", rr.centerX(), ly, text); ly += 2.3f * u }
            if (s.cond > 0) { c.drawText(Defs.conditions[s.cond], rr.centerX(), ly, text); ly += 2.3f * u }
            if (s.ratchet > 1) {
                fill.color = Color.BLACK
                for (k in 0 until s.ratchet) c.drawCircle(rr.left + 1.4f * u + k * 1.3f * u, r.bottom - 1.3f * u, .45f * u, fill)
            }
            if (s.hasLocks()) { fill.color = lockCol; c.drawCircle(rr.right - 1.3f * u, r.top + 1.3f * u, .7f * u, fill) }
        }
    }

    // step editor: knob definitions
    private val stepKnobNames = listOf("VEL", "PROB", "COND", "MICRO", "RATCH", "NOTE", "LEN", "CHORD")
    private fun stepKnobRange(k: Int): IntRange = when (k) {
        0 -> 1..127; 1 -> 0..100; 2 -> 0 until Defs.conditions.size; 3 -> -12..12; 4 -> 1..8
        5 -> if (def.kind == Kind.SLICER) 0..63 else 0..127; 6 -> 1..64; else -> 0 until Defs.chords.size
    }
    private fun stepKnobGet(s: StepData, k: Int): Int = when (k) {
        0 -> s.vel; 1 -> s.prob; 2 -> s.cond; 3 -> s.micro; 4 -> s.ratchet; 5 -> s.note
        6 -> (s.len * 4).roundToInt(); else -> s.chord
    }
    private fun stepKnobSet(s: StepData, k: Int, v: Int) {
        when (k) {
            0 -> s.vel = v; 1 -> s.prob = v; 2 -> s.cond = v; 3 -> s.micro = v; 4 -> s.ratchet = v
            5 -> s.note = v; 6 -> s.len = v / 4f; else -> s.chord = v
        }
    }
    private fun stepKnobText(s: StepData, k: Int): String = when (k) {
        1 -> "${s.prob}%"; 2 -> Defs.conditions[s.cond]
        3 -> if (s.micro == 0) "0" else (if (s.micro > 0) "+" else "") + "${s.micro}/24"
        4 -> "×${s.ratchet}"
        5 -> (when (def.kind) { Kind.SLICER -> "S${s.note + 1}"; Kind.SYNTH, Kind.GRAIN -> Defs.noteName(s.note); else -> if (tr.machine == 8) Defs.noteName(s.note) else "—" }) +
            (if (s.noteCount() > 1) " +${s.noteCount() - 1}" else "")
        6 -> { val l = s.len; if (l == l.toInt().toFloat()) "${l.toInt()}" else "$l" }
        7 -> if (def.kind == Kind.SYNTH || def.kind == Kind.GRAIN) Defs.chords[s.chord] else "—"
        else -> "${stepKnobGet(s, k)}"
    }

    private fun drawStepEditor(c: Canvas) {
        fill.color = panel
        c.drawRoundRect(stepRowRect, u, u, fill)
        if (showMaster) {
            val gr = status[29] / 100f
            text.textAlign = Paint.Align.LEFT; text.textSize = 2.2f * u; text.color = subText
            val on = proj.master[14] > 0.001f
            c.drawText(if (on) "Compressor gain reduction" else "Compressor off — raise CmpMix to engage (GR shown for reference)",
                stepRowRect.left + 2f * u, stepRowRect.top + 4f * u, text)
            val bar = RectF(stepRowRect.left + 2f * u, stepRowRect.top + 6f * u, stepRowRect.right - 14f * u, stepRowRect.top + 9.5f * u)
            fill.color = 0xFF0E0F12.toInt(); c.drawRoundRect(bar, .5f * u, .5f * u, fill)
            val frac = (gr / 24f).coerceIn(0f, 1f)
            fill.color = if (on) 0xFFFFA94D.toInt() else dim
            c.drawRoundRect(RectF(bar.right - bar.width() * frac, bar.top, bar.right, bar.bottom), .5f * u, .5f * u, fill)
            text.color = textCol; text.textSize = 2.4f * u
            c.drawText(String.format("-%.1f dB", gr), bar.right + 2f * u, bar.bottom - .6f * u, text)
            text.textAlign = Paint.Align.CENTER
            return
        }
        if (editStep < 0) {
            text.textSize = 2.2f * u; text.color = subText
            c.drawText("Long-press a step to edit velocity, probability, trig condition, micro-timing, ratchets, note, length, chord and p-locks",
                stepRowRect.centerX(), stepRowRect.centerY() + .8f * u, text)
            return
        }
        val s = pat.steps[selTrack][editStep]
        for (k in 0 until 8) {
            val range = stepKnobRange(k)
            val v = stepKnobGet(s, k)
            val frac = (v - range.first).toFloat() / max(1, range.last - range.first)
            val poly = def.kind == Kind.SYNTH || def.kind == Kind.GRAIN
            val active = when (k) { 7 -> poly; 6 -> poly; 5 -> def.kind != Kind.DRUM || tr.machine == 8; else -> true }
            drawKnob(c, stepKnobs[k], stepKnobNames[k], frac, stepKnobText(s, k), lockCol, active)
        }
        drawBtn(c, clrLockBtn, "CLR LOCKS", ts = 1.9f)
        drawBtn(c, doneBtn, "DONE", true, onCol = lockCol, ts = 2.2f)
    }

    private fun drawSoundKnobs(c: Canvas) {
        if (showMaster) {
            for (i in 0 until Defs.masterParams.size) {
                drawKnob(c, soundKnobs[i], Defs.masterParams[i], proj.master[i], Defs.formatMaster(i, proj.master[i]), 0xFF5AA9FF.toInt())
            }
            return
        }
        val s = if (editStep >= 0) pat.steps[selTrack][editStep] else null
        for (i in 0 until 16) {
            val name = Defs.paramName(tr.machine, i)
            if (name.isEmpty()) continue
            val locked = s != null && s.lock[i] >= 0f
            val v = if (locked) s!!.lock[i] else tr.params[i]
            val col = if (i >= 12) 0xFF8A93A6.toInt() else trackCol[selTrack]
            drawKnob(c, soundKnobs[i], name, v, Defs.formatParam(tr.machine, i, v), col, true, locked)
        }
    }

    private fun drawPerf(c: Canvas) {
        if (showMaster || def.kind == Kind.DRUM) {
            for (t in 0 until TRACKS) {
                val r = pads[t]
                val age = SystemClock.uptimeMillis() - flash[t]
                fill.color = trackCol[t]; fill.alpha = if (age < 100) 255 else 70
                c.drawRoundRect(r, 1.2f * u, 1.2f * u, fill); fill.alpha = 255
                text.textSize = 2.3f * u; text.color = textCol
                c.drawText("${t + 1}", r.centerX(), r.top + 4f * u, text)
                text.textSize = 1.9f * u
                c.drawText(Defs.machines[proj.tracks[t].machine].short, r.centerX(), r.top + 7f * u, text)
            }
            return
        }
        if (def.kind == Kind.SYNTH) {
            drawBtn(c, octDown, "OCT −", ts = 2f)
            drawBtn(c, octUp, "OCT +", ts = 2f)
            drawKeys(c)
            return
        }
        if (def.kind == Kind.GRAIN) {
            drawBtn(c, grainLoad, "LOAD", ts = 2f)
            drawBtn(c, grainOctDown, "OCT −", ts = 2f)
            drawBtn(c, grainOctUp, "OCT +", ts = 2f)
            fill.color = panel; c.drawRoundRect(grainWave, u, u, fill)
            val d = app.decoded[selTrack]
            if (d == null) {
                text.textSize = 2f * u; text.color = subText
                c.drawText("Tap LOAD to pick a sample for the granular engine", grainWave.centerX(), grainWave.centerY() + .7f * u, text)
            } else {
                val pk = app.peakCache(selTrack, grainWave.width().toInt() / 3)
                val n = pk.size / 2; val bw = grainWave.width() / n
                val mid = grainWave.centerY(); val hh = grainWave.height() * .45f
                stroke.strokeWidth = 2f; stroke.color = trackCol[selTrack]
                for (b in 0 until n) { val x = grainWave.left + b * bw; c.drawLine(x, mid - pk[b * 2 + 1] * hh, x, mid - pk[b * 2] * hh, stroke) }
                // position + spray window
                val pos = knobValue(0); val spray = knobValue(3)
                fill.color = lockCol; fill.alpha = 50
                val px = grainWave.left + grainWave.width() * pos
                val sw = grainWave.width() * spray * .25f
                c.drawRect(px - sw, grainWave.top, px + sw, grainWave.bottom, fill); fill.alpha = 255
                stroke.color = lockCol; stroke.strokeWidth = 3f
                c.drawLine(px, grainWave.top, px, grainWave.bottom, stroke)
                text.textAlign = Paint.Align.LEFT; text.textSize = 1.7f * u; text.color = subText
                c.drawText(d.name, grainWave.left + u, grainWave.top + 2f * u, text); text.textAlign = Paint.Align.CENTER
            }
            drawKeys(c)
            return
        }
        // slicer
        for ((id, r) in slicerBtns) when (id) {
            "EDIT" -> drawBtn(c, r, "EDIT", sliceEdit, onCol = lockCol, ts = 2f)
            "SENS-" -> drawBtn(c, r, "SENS −", ts = 2f)
            "SENS+" -> drawBtn(c, r, "SENS +", ts = 2f)
            "ZOOM-" -> drawBtn(c, r, "ZOOM −", ts = 2f)
            "ZOOM+" -> drawBtn(c, r, "ZOOM +", ts = 2f)
            else -> drawBtn(c, r, id, ts = 2f)
        }
        text.textAlign = Paint.Align.LEFT; text.textSize = 2f * u; text.color = subText
        val nm = tr.sampleName ?: "No sample loaded — tap LOAD (wav, mp3, flac, ogg, m4a)"
        val info = if (sliceEdit) "EDIT: tap = add slice • drag marker = move • hold marker = delete • drag empty = scroll"
            else "$nm  •  ${tr.slices.size} slices  •  sens ${(tr.sliceSens * 100).roundToInt()}%  •  zoom ${zoom}x"
        c.drawText(info.take(110), slicerBtns.last().second.right + 2f * u, slicerBtns[0].second.centerY() + .7f * u, text)
        text.textAlign = Paint.Align.CENTER
        fill.color = panel; c.drawRoundRect(waveRect, u, u, fill)
        val d = app.decoded[selTrack] ?: return
        clampView(d)
        val vis = visFrames(d)
        val buckets = (waveRect.width() / 3f).toInt().coerceAtLeast(1)
        val pk = d.peaksRange(viewStart, viewStart + vis, buckets)
        val mid = waveRect.centerY(); val hh = waveRect.height() * .45f
        val frames = d.frames
        val sl = tr.slices
        c.save(); c.clipRect(waveRect)
        for (k in sl.indices) {
            val a = frameToX(sl[k])
            val b = frameToX(if (k + 1 < sl.size) sl[k + 1] else frames)
            if (b < waveRect.left || a > waveRect.right) continue
            fill.color = if (k % 2 == 0) 0xFF23262D.toInt() else 0xFF1A1C21.toInt()
            if (k == activeSlice && SystemClock.uptimeMillis() - activeSliceTime < 200) fill.color = 0xFF3A3F4A.toInt()
            c.drawRect(max(a, waveRect.left), waveRect.top, min(b, waveRect.right), waveRect.bottom, fill)
            text.textSize = 1.7f * u; text.color = subText
            if (b - a > 2.5f * u) c.drawText("${k + 1}", (max(a, waveRect.left) + min(b, waveRect.right)) / 2, waveRect.top + 2.2f * u, text)
        }
        stroke.strokeWidth = 2f; stroke.color = trackCol[selTrack]
        val bw = waveRect.width() / buckets
        for (b in 0 until buckets) {
            val x = waveRect.left + b * bw
            c.drawLine(x, mid - pk[b * 2 + 1] * hh, x, mid - pk[b * 2] * hh, stroke)
        }
        stroke.color = lockCol; stroke.strokeWidth = if (sliceEdit) 4f else 3f
        fill.color = lockCol
        for (k in sl.indices) {
            val a = frameToX(sl[k])
            if (a < waveRect.left - 2 || a > waveRect.right + 2) continue
            c.drawLine(a, waveRect.top, a, waveRect.bottom, stroke)
            if (sliceEdit) c.drawCircle(a, waveRect.bottom - 1.2f * u, .9f * u, fill)
        }
        c.restore()
        if (zoom > 1) { // scroll indicator
            val tw = waveRect.width() * vis / frames
            val tx = waveRect.left + waveRect.width() * viewStart / frames
            fill.color = dim; c.drawRect(tx, waveRect.bottom - .5f * u, tx + tw, waveRect.bottom, fill)
        }
    }

    private fun visFrames(d: Decoded) = max(64, d.frames / zoom)
    private fun clampView(d: Decoded) { viewStart = viewStart.coerceIn(0, max(0, d.frames - visFrames(d))) }
    private fun frameToX(f: Int): Float {
        val d = app.decoded[selTrack] ?: return waveRect.left
        return waveRect.left + waveRect.width() * (f - viewStart).toFloat() / visFrames(d)
    }
    private fun xToFrame(x: Float): Int {
        val d = app.decoded[selTrack] ?: return 0
        return (viewStart + (x - waveRect.left) / waveRect.width() * visFrames(d)).toInt().coerceIn(0, d.frames - 1)
    }
    private fun setSlices(list: List<Int>) {
        val d = app.decoded[selTrack]
        val lim = (d?.frames ?: Int.MAX_VALUE) - 32
        val v = list.map { it.coerceIn(0, max(0, lim)) }.distinct().sorted().take(64)
        tr.slices = if (v.isEmpty()) intArrayOf(0) else v.toIntArray()
        Native.setSlices(selTrack, tr.slices)
    }

    // ---------------------------------------------------------------- FX page
    private fun drawFx(c: Canvas) {
        drawBtn(c, fxLatchBtn, "LATCH", fxLatch, onCol = lockCol, ts = 2f)
        drawBtn(c, fxClearBtn, "ALL OFF", ts = 2f)
        text.textAlign = Paint.Align.LEFT; text.textSize = 1.9f * u; text.color = subText
        c.drawText(if (fxLatch) "Latch on: tap to toggle effects" else "Hold a pad to apply it • turn on LATCH to make them stay on",
            fxClearBtn.right + 2f * u, fxClearBtn.centerY() + .6f * u, text)
        text.textAlign = Paint.Align.CENTER
        // XY filter
        val on = fxState[11]
        fill.color = if (on) 0xFF2A2F38.toInt() else panel
        c.drawRoundRect(fxXY, u, u, fill)
        stroke.strokeWidth = 2f; stroke.color = dim
        c.drawLine(fxXY.centerX(), fxXY.top, fxXY.centerX(), fxXY.bottom, stroke)
        text.textSize = 2f * u; text.color = subText
        c.drawText("◀ LOW-PASS", fxXY.left + fxXY.width() * .25f, fxXY.bottom - 1.5f * u, text)
        c.drawText("HIGH-PASS ▶", fxXY.left + fxXY.width() * .75f, fxXY.bottom - 1.5f * u, text)
        c.drawText("FILTER  •  up = resonance", fxXY.centerX(), fxXY.top + 3f * u, text)
        if (on) {
            val px = fxXY.left + fxXY.width() * fxX; val py = fxXY.bottom - fxXY.height() * fxY
            stroke.color = 0xFF5AA9FF.toInt(); stroke.strokeWidth = 3f
            c.drawLine(px, fxXY.top, px, fxXY.bottom, stroke); c.drawLine(fxXY.left, py, fxXY.right, py, stroke)
            fill.color = 0xFF5AA9FF.toInt(); c.drawCircle(px, py, 2f * u, fill)
        }
        val cols = intArrayOf(0xFFFF6B5A.toInt(), 0xFFFF6B5A.toInt(), 0xFFFF6B5A.toInt(), 0xFFFF6B5A.toInt(), 0xFFFFA94D.toInt(), 0xFFFFA94D.toInt(),
            0xFFF5E663.toInt(), 0xFF6FE07A.toInt(), 0xFF4FD6C8.toInt(), 0xFF5AA9FF.toInt(), 0xFFA884FF.toInt())
        for (i in 0 until 11) {
            val r = fxPads[i]
            fill.color = cols[i]; fill.alpha = if (fxState[i]) 255 else 55
            c.drawRoundRect(r, 1.2f * u, 1.2f * u, fill); fill.alpha = 255
            text.textSize = 2.3f * u; text.color = if (fxState[i]) Color.BLACK else textCol
            val lines = fxNames[i].split("\n")
            for ((k, ln) in lines.withIndex()) c.drawText(ln, r.centerX(), r.centerY() + (k - (lines.size - 1) / 2f) * 2.8f * u + .8f * u, text)
        }
    }

    private fun setFxOn(i: Int, on: Boolean) { fxState[i] = on; Native.setFx(i, on) }
    private fun fxXYFrom(x: Float, y: Float) {
        fxX = ((x - fxXY.left) / fxXY.width()).coerceIn(0f, 1f)
        fxY = ((fxXY.bottom - y) / fxXY.height()).coerceIn(0f, 1f)
        Native.setFxXY(fxX, fxY)
    }
    fun releaseFx() { for (i in 0 until 12) if (fxState[i]) setFxOn(i, false) }

    private var activeSlice = -1
    private var activeSliceTime = 0L
    private val heldKeys = HashSet<Int>()

    private fun whiteCount() = 15
    private fun kRect() = if (!showMaster && def.kind == Kind.GRAIN) grainKeys else keysRect
    private fun drawKeys(c: Canvas) {
        val keysRect = kRect()
        val base = (octave + 1) * 12
        val ww = keysRect.width() / whiteCount()
        val whiteSemis = intArrayOf(0, 2, 4, 5, 7, 9, 11)
        for (k in 0 until whiteCount()) {
            val note = base + (k / 7) * 12 + whiteSemis[k % 7]
            val r = RectF(keysRect.left + k * ww + 2, keysRect.top, keysRect.left + (k + 1) * ww - 2, keysRect.bottom)
            fill.color = if (note in heldKeys) trackCol[selTrack] else 0xFFDADDE3.toInt()
            c.drawRoundRect(r, .6f * u, .6f * u, fill)
            if (note % 12 == 0) { text.textSize = 1.8f * u; text.color = 0xFF444444.toInt(); c.drawText(Defs.noteName(note), r.centerX(), r.bottom - 1.2f * u, text) }
        }
        for (k in 0 until whiteCount() - 1) {
            val semi = whiteSemis[k % 7]
            if (semi == 4 || semi == 11) continue
            val note = base + (k / 7) * 12 + semi + 1
            val cx = keysRect.left + (k + 1) * ww
            val r = RectF(cx - ww * .32f, keysRect.top, cx + ww * .32f, keysRect.top + keysRect.height() * .6f)
            fill.color = if (note in heldKeys) trackCol[selTrack] else 0xFF22252B.toInt()
            c.drawRoundRect(r, .5f * u, .5f * u, fill)
        }
    }

    private fun keyAt(x: Float, y: Float): Int {
        val keysRect = kRect()
        if (!keysRect.contains(x, y)) return -1
        val base = (octave + 1) * 12
        val ww = keysRect.width() / whiteCount()
        val whiteSemis = intArrayOf(0, 2, 4, 5, 7, 9, 11)
        if (y < keysRect.top + keysRect.height() * .6f) {
            for (k in 0 until whiteCount() - 1) {
                val semi = whiteSemis[k % 7]
                if (semi == 4 || semi == 11) continue
                val cx = keysRect.left + (k + 1) * ww
                if (abs(x - cx) < ww * .32f) return base + (k / 7) * 12 + semi + 1
            }
        }
        val k = ((x - keysRect.left) / ww).toInt().coerceIn(0, whiteCount() - 1)
        return base + (k / 7) * 12 + whiteSemis[k % 7]
    }

    // ---------------------------------------------------------------- touch
    private sealed class Target {
        data class Knob(val idx: Int) : Target()
        data class StepKnob(val idx: Int) : Target()
        data class StepCell(val cell: Int) : Target()
        data class Key(val note: Int, val track: Int) : Target()
        data class Pad(val track: Int) : Target()
        data class Slice(val slice: Int) : Target()
        data class Btn(val id: String) : Target()
        data class TrackRow(val t: Int) : Target()
        data class MixKnob(val strip: Int, val k: Int) : Target()
        data class Fader(val strip: Int) : Target()
        data class SongCell(val row: Int, val right: Boolean) : Target()
        data class Marker(val idx: Int) : Target()
        object WaveEdit : Target()
        data class FxPad(val id: Int) : Target()
        object FxXY : Target()
        object Bpm : Target()
        object Swing : Target()
    }

    private class Touch(val target: Target, val x0: Float, val y0: Float, val start: Float, val t0: Long) {
        var moved = false; var longFired = false
        var recStep = -1; var recPos = 0.0; var recTrack = -1
    }

    private val touches = HashMap<Int, Touch>()
    private var lastTapKnob = -1; private var lastTapTime = 0L

    private val longPress = Runnable { checkLongPress() }

    private fun checkLongPress() {
        val now = SystemClock.uptimeMillis()
        for ((_, t) in touches) {
            if (t.moved || t.longFired || now - t.t0 < 380) continue
            when (val tg = t.target) {
                is Target.StepCell -> { t.longFired = true; editStep = stepIndex(tg.cell); invalidate() }
                is Target.TrackRow -> { t.longFired = true; if (tg.t < TRACKS) app.chooseMachine(tg.t) }
                is Target.Knob -> {
                    if (editStep >= 0 && !showMaster) {
                        t.longFired = true
                        pat.steps[selTrack][editStep].lock[tg.idx] = -1f
                        Native.setLock(curPattern, selTrack, editStep, tg.idx, -1f); invalidate()
                    }
                }
                is Target.Marker -> {
                    t.longFired = true
                    if (tr.slices.size > 1) setSlices(tr.slices.filterIndexed { i, _ -> i != tg.idx })
                    invalidate()
                }
                is Target.Btn -> if (tg.id == "CLR") { t.longFired = true; clearPattern() }
                    else if (tg.id == "SCLR") { t.longFired = true; proj.songPat.clear(); proj.songRep.clear(); songSel = 0; proj.pushSong(0); app.toast("Song cleared") }
                else -> {}
            }
        }
    }

    private fun hit(x: Float, y: Float): Target? {
        for ((id, r) in topBtns) if (r.contains(x, y)) return when (id) { "BPM" -> Target.Bpm; "SWING" -> Target.Swing; else -> Target.Btn(id) }
        for (t in 0..TRACKS) if (trackRows[t].contains(x, y)) {
            if (t < TRACKS && muteRect(trackRows[t]).contains(x, y)) return Target.Btn("MUTE$t")
            return Target.TrackRow(t)
        }
        if (pageMode == 1) return hitMixer(x, y)
        if (pageMode == 2) return hitSong(x, y)
        if (pageMode == 3) {
            if (fxLatchBtn.contains(x, y)) return Target.Btn("FXLATCH")
            if (fxClearBtn.contains(x, y)) return Target.Btn("FXCLEAR")
            if (fxXY.contains(x, y)) return Target.FxXY
            for (i in 0 until 11) if (fxPads[i].contains(x, y)) return Target.FxPad(i)
            return null
        }
        for (i in 0 until 4) if (pageBtns[i].contains(x, y)) return Target.Btn("PAGE$i")
        if (lenMinus.contains(x, y)) return Target.Btn("LEN-")
        if (lenPlus.contains(x, y)) return Target.Btn("LEN+")
        for (i in 0 until 16) if (stepCells[i].contains(x, y)) return Target.StepCell(i)
        if (!showMaster && editStep >= 0) {
            for (i in 0 until 8) if (stepKnobs[i].contains(x, y)) return Target.StepKnob(i)
            if (clrLockBtn.contains(x, y)) return Target.Btn("CLRLOCK")
            if (doneBtn.contains(x, y)) return Target.Btn("DONE")
        }
        for (i in 0 until 16) if (soundKnobs[i].contains(x, y)) {
            if (showMaster && i >= Defs.masterParams.size) return null
            if (!showMaster && Defs.paramName(tr.machine, i).isEmpty()) return null
            return Target.Knob(i)
        }
        if (perf.contains(x, y)) {
            if (showMaster || def.kind == Kind.DRUM) { for (t in 0 until TRACKS) if (pads[t].contains(x, y)) return Target.Pad(t); return null }
            if (def.kind == Kind.SYNTH) {
                if (octDown.contains(x, y)) return Target.Btn("OCT-")
                if (octUp.contains(x, y)) return Target.Btn("OCT+")
                val k = keyAt(x, y); if (k >= 0) return Target.Key(k, selTrack)
                return null
            }
            if (def.kind == Kind.GRAIN) {
                if (grainLoad.contains(x, y)) return Target.Btn("SLLOAD")
                if (grainOctDown.contains(x, y)) return Target.Btn("OCT-")
                if (grainOctUp.contains(x, y)) return Target.Btn("OCT+")
                if (grainWave.contains(x, y)) return Target.Knob(0)
                val k = keyAt(x, y); if (k >= 0) return Target.Key(k, selTrack)
                return null
            }
            for ((id, r) in slicerBtns) if (r.contains(x, y)) return Target.Btn("SL$id")
            if (waveRect.contains(x, y)) {
                app.decoded[selTrack] ?: return null
                if (sliceEdit) {
                    var best = -1; var bd = 1.8f * u
                    for (k in tr.slices.indices) { val dx = abs(frameToX(tr.slices[k]) - x); if (dx < bd) { bd = dx; best = k } }
                    return if (best >= 0) Target.Marker(best) else Target.WaveEdit
                }
                val pos = xToFrame(x)
                var s = 0
                for (k in tr.slices.indices) if (tr.slices[k] <= pos) s = k
                return Target.Slice(s)
            }
        }
        return null
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val pi = e.actionIndex
                val id = e.getPointerId(pi)
                val x = e.getX(pi); val y = e.getY(pi)
                val tg = hit(x, y) ?: return true
                val start = when (tg) {
                    is Target.Knob -> knobValue(tg.idx)
                    is Target.StepKnob -> if (editStep >= 0) stepKnobGet(pat.steps[selTrack][editStep], tg.idx).toFloat() else 0f
                    is Target.Bpm -> proj.bpm
                    is Target.Swing -> proj.swing
                    is Target.MixKnob -> mixGet(tg.strip, tg.k)
                    is Target.Marker -> tr.slices.getOrElse(tg.idx) { 0 }.toFloat()
                    is Target.WaveEdit -> viewStart.toFloat()
                    is Target.Fader -> mixGet(tg.strip, 3)
                    is Target.SongCell -> if (tg.row < proj.songPat.size) (if (tg.right) proj.songRep[tg.row] else proj.songPat[tg.row]).toFloat() else 0f
                    else -> 0f
                }
                val t = Touch(tg, x, y, start, SystemClock.uptimeMillis())
                touches[id] = t
                onDown(t, x, y)
                removeCallbacks(longPress); postDelayed(longPress, 400)
            }
            MotionEvent.ACTION_MOVE -> {
                for (pi in 0 until e.pointerCount) {
                    val t = touches[e.getPointerId(pi)] ?: continue
                    val dx = e.getX(pi) - t.x0; val dy = e.getY(pi) - t.y0
                    if (!t.moved && (abs(dx) > 1.2f * u || abs(dy) > 1.2f * u)) t.moved = true
                    if (t.moved) onDrag(t, -dy, dx, e.getX(pi), e.getY(pi))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                val pi = e.actionIndex
                val id = e.getPointerId(pi)
                val t = touches.remove(id)
                if (t != null) onUp(t, e.actionMasked == MotionEvent.ACTION_CANCEL)
                if (e.actionMasked != MotionEvent.ACTION_POINTER_UP) {
                    // safety: release everything on final up
                    for ((_, rest) in touches) onUp(rest, true)
                    touches.clear()
                }
            }
        }
        invalidate()
        return true
    }

    private fun velFromY(y: Float, r: RectF) = (0.35f + 0.65f * ((y - r.top) / r.height()).coerceIn(0f, 1f))

    private fun onDown(t: Touch, x: Float, y: Float) {
        when (val tg = t.target) {
            is Target.Pad -> {
                val v = velFromY(y, pads[tg.track])
                val note = padNote(tg.track)
                Native.noteOn(tg.track, note, v)
                recordHit(tg.track, note, v, t)
            }
            is Target.Key -> {
                val v = velFromY(y, kRect())
                heldKeys.add(tg.note); lastSynthNote = tg.note
                Native.noteOn(tg.track, tg.note, v)
                if (editStep >= 0) { if (heldKeys.size > 1) addEditStepNote(tg.note) else setEditStepNote(tg.note) }
                else recordHit(tg.track, tg.note, v, t)
            }
            is Target.Slice -> {
                activeSlice = tg.slice; activeSliceTime = SystemClock.uptimeMillis()
                Native.noteOn(selTrack, tg.slice, .9f)
                if (editStep >= 0) setEditStepNote(tg.slice)
                else recordHit(selTrack, tg.slice, .9f, t)
            }
            is Target.Btn -> if (tg.id == "FILL") { fillHeld = true; Native.setFill(true) }
            is Target.FxPad -> if (fxLatch) setFxOn(tg.id, !fxState[tg.id]) else setFxOn(tg.id, true)
            is Target.FxXY -> {
                fxXYFrom(x, y)
                if (fxLatch && fxState[11]) setFxOn(11, false) else setFxOn(11, true)
            }
            else -> {}
        }
    }

    private fun setEditStepNote(n: Int) {
        val s = pat.steps[selTrack][editStep]
        s.note = n; s.on = true; s.clearExtra()
        proj.pushStepFields(curPattern, selTrack, editStep)
    }
    private fun addEditStepNote(n: Int) {
        val s = pat.steps[selTrack][editStep]
        s.on = true; s.addNote(n)
        proj.pushStepFields(curPattern, selTrack, editStep)
    }
    private fun padNote(t: Int) = when (Defs.machines[proj.tracks[t].machine].kind) {
        Kind.SYNTH, Kind.GRAIN -> 48; Kind.SLICER -> 0; else -> 60
    }
    private val recPass = Array(TRACKS) { LongArray(MAX_STEPS) { Long.MIN_VALUE } }

    /** live recording: quantise to nearest step, keep the offset as micro-timing */
    private fun recordHit(track: Int, note: Int, vel: Float, t: Touch) {
        if (!rec || !playing) return
        val p = curPattern
        val len = proj.patterns[p].len[track]
        val rel = Native.relPos() - app.latencySteps()
        val pos = ((rel % len) + len) % len
        val nearest = floor(pos + .5)
        var micro = ((pos - nearest) * 24).roundToInt().coerceIn(-12, 12)
        var idx = (nearest.toInt()) % len
        if (abs(micro) <= 1) micro = 0
        val pass = floor(rel / len).toLong()
        // chord grouping: keys pressed within ~120 ms of another recorded key land on the same step
        var group = false
        val now = SystemClock.uptimeMillis()
        for ((_, o) in touches) if (o !== t && o.recStep >= 0 && o.recTrack == track && now - o.t0 < 120) { idx = o.recStep; group = true }
        val s = proj.patterns[p].steps[track][idx]
        val poly = Defs.machines[proj.tracks[track].machine].kind != Kind.DRUM
        if (poly && s.on && (group || recPass[track][idx] == pass)) {
            s.addNote(note)
        } else {
            s.on = true; s.note = note; s.vel = (vel * 127).roundToInt().coerceIn(1, 127); s.micro = micro
            s.len = 1f; s.prob = 100; s.cond = 0; s.ratchet = 1; s.clearExtra()
        }
        recPass[track][idx] = pass
        proj.pushStepFields(p, track, idx)
        t.recStep = idx; t.recPos = rel; t.recTrack = track
    }

    private fun knobValue(i: Int): Float {
        if (showMaster) return proj.master[i]
        if (editStep >= 0) { val l = pat.steps[selTrack][editStep].lock[i]; if (l >= 0f) return l }
        return tr.params[i]
    }

    private fun setKnob(i: Int, v: Float) {
        val nv = v.coerceIn(0f, 1f)
        if (showMaster) { proj.master[i] = nv; Native.setMaster(i, nv); return }
        if (editStep >= 0) {
            pat.steps[selTrack][editStep].lock[i] = nv
            Native.setLock(curPattern, selTrack, editStep, i, nv)
            // audition while locking
        } else {
            tr.params[i] = nv; Native.setParam(selTrack, i, nv)
        }
    }

    private fun onDrag(t: Touch, dyUp: Float, dx: Float, x: Float, y: Float) {
        when (val tg = t.target) {
            is Target.FxXY -> fxXYFrom(x, y)
            is Target.Marker -> {
                val d = app.decoded[selTrack] ?: return
                val sl = tr.slices
                if (tg.idx >= sl.size) return
                val lo = if (tg.idx > 0) sl[tg.idx - 1] + 64 else 0
                val hi = if (tg.idx + 1 < sl.size) sl[tg.idx + 1] - 64 else d.frames - 64
                sl[tg.idx] = xToFrame(x).coerceIn(lo, max(lo, hi))
                Native.setSlices(selTrack, sl)
            }
            is Target.WaveEdit -> {
                val d = app.decoded[selTrack] ?: return
                viewStart = (t.start - dx / waveRect.width() * visFrames(d)).toInt(); clampView(d)
            }
            is Target.Knob -> setKnob(tg.idx, t.start + dyUp / (30f * u))
            is Target.StepKnob -> {
                if (editStep < 0) return
                val range = stepKnobRange(tg.idx)
                val span = (range.last - range.first).coerceAtLeast(1)
                val pxPerUnit = (30f * u / span).coerceIn(.6f * u, 3f * u)
                val v = (t.start + dyUp / pxPerUnit).roundToInt().coerceIn(range.first, range.last)
                val s = pat.steps[selTrack][editStep]
                if (stepKnobGet(s, tg.idx) != v) {
                    stepKnobSet(s, tg.idx, v); s.on = true
                    proj.pushStepFields(curPattern, selTrack, editStep)
                }
            }
            is Target.MixKnob -> mixSet(tg.strip, tg.k, t.start + dyUp / (30f * u))
            is Target.Fader -> mixSet(tg.strip, 3, t.start + dyUp / mixFader[tg.strip].height())
            is Target.SongCell -> {
                if (tg.row >= proj.songPat.size) return
                if (tg.right) proj.songRep[tg.row] = (t.start + dyUp / (1.5f * u)).roundToInt().coerceIn(1, 64)
                else proj.songPat[tg.row] = (t.start + dyUp / (3f * u)).roundToInt().coerceIn(0, PATTERNS - 1)
                songSel = tg.row; proj.pushSong(songSel)
            }
            is Target.Bpm -> { proj.bpm = ((t.start + dyUp / (.8f * u)) * 1f).roundToInt().toFloat().coerceIn(40f, 250f); Native.setBpm(proj.bpm) }
            is Target.Swing -> { proj.swing = (t.start + dyUp / (1.5f * u)).roundToInt().toFloat().coerceIn(50f, 75f); Native.setSwing(proj.swing) }
            else -> {}
        }
    }

    private fun onUp(t: Touch, cancel: Boolean) {
        val tg = t.target
        when (tg) {
            is Target.Key -> {
                heldKeys.remove(tg.note); Native.noteOff(tg.track, tg.note)
                if (t.recStep >= 0 && !cancel) {
                    val held = Native.relPos() - app.latencySteps() - t.recPos
                    if (held > 0) {
                        val s = proj.patterns[curPattern].steps[tg.track][t.recStep]
                        s.len = ((held * 4).roundToInt() / 4f).coerceIn(.25f, 16f)
                        proj.pushStepFields(curPattern, tg.track, t.recStep)
                    }
                }
            }
            is Target.Pad -> {
                Native.noteOff(tg.track, padNote(tg.track))
            }
            is Target.Btn -> if (tg.id == "FILL") { fillHeld = false; Native.setFill(false) }
            is Target.FxPad -> if (!fxLatch) setFxOn(tg.id, false)
            is Target.FxXY -> if (!fxLatch) setFxOn(11, false)
            else -> {}
        }
        if (cancel || t.moved || t.longFired) return
        when (tg) {
            is Target.StepCell -> tapStep(tg.cell)
            is Target.TrackRow -> {
                if (tg.t == TRACKS) { showMaster = true; editStep = -1 }
                else { showMaster = false; if (tg.t != selTrack) editStep = -1; selTrack = tg.t; if (page * 16 >= pat.len[selTrack]) page = 0 }
            }
            is Target.Knob -> {
                val now = SystemClock.uptimeMillis()
                if (lastTapKnob == tg.idx && now - lastTapTime < 350) {
                    if (showMaster) setKnob(tg.idx, Defs.masterDefaults[tg.idx])
                    else if (editStep >= 0) { pat.steps[selTrack][editStep].lock[tg.idx] = -1f; Native.setLock(curPattern, selTrack, editStep, tg.idx, -1f) }
                    else setKnob(tg.idx, Defs.paramDefault(tr.machine, tg.idx))
                    lastTapKnob = -1
                } else { lastTapKnob = tg.idx; lastTapTime = now }
            }
            is Target.Bpm -> {
                val now = SystemClock.uptimeMillis()
                if (lastTapKnob == -100 && now - lastTapTime < 350) { app.editBpm(); lastTapKnob = -1 } else { lastTapKnob = -100; lastTapTime = now }
            }
            is Target.Btn -> button(tg.id)
            is Target.Fader, is Target.MixKnob -> {
                val key = if (tg is Target.Fader) 1000 + tg.strip else 2000 + (tg as Target.MixKnob).strip * 4 + tg.k
                val now = SystemClock.uptimeMillis()
                if (lastTapKnob == key && now - lastTapTime < 350) {
                    val strip = if (tg is Target.Fader) tg.strip else (tg as Target.MixKnob).strip
                    val k = if (tg is Target.Fader) 3 else (tg as Target.MixKnob).k
                    mixSet(strip, k, mixDefault(strip, k)); lastTapKnob = -1
                } else { lastTapKnob = key; lastTapTime = now }
            }
            is Target.WaveEdit -> {
                val f = xToFrame(t.x0)
                setSlices(tr.slices.toList() + f)
            }
            is Target.SongCell -> { songSel = min(tg.row, max(0, proj.songPat.size - 1)); if (!playing) proj.pushSong(songSel) }
            else -> {}
        }
    }

    private fun tapStep(cell: Int) {
        val i = stepIndex(cell)
        if (i >= pat.len[selTrack]) return
        if (editStep >= 0) { editStep = i; return }
        val s = pat.steps[selTrack][i]
        s.on = !s.on
        if (s.on) {
            when (def.kind) {
                Kind.SLICER -> s.note = i % max(1, tr.slices.size)
                Kind.SYNTH -> if (s.note == 60 && heldKeys.isEmpty()) s.note = lastSynthNote
                else -> {}
            }
            if (heldKeys.isNotEmpty()) {
                val ks = heldKeys.sorted()
                s.note = ks[0]; s.clearExtra()
                for (k in 1 until min(4, ks.size)) s.extra[k - 1] = ks[k]
            }
        }
        proj.pushStepFields(curPattern, selTrack, i)
    }

    private var lastSynthNote = 48

    private fun clearPattern() {
        for (t in 0 until TRACKS) for (i in 0 until MAX_STEPS) { val s = pat.steps[t][i]; s.copyFrom(StepData()) }
        proj.pushPattern(curPattern); editStep = -1
        app.toast("Pattern ${'A' + curPattern} cleared")
    }

    private fun button(id: String) {
        when {
            id == "PLAY" -> { if (playing) { Native.halt(); playing = false } else { Native.queuePattern(curPattern); app.queuedPattern = curPattern; Native.play(); playing = true } }
            id.length == 1 && id[0] in 'A'..'H' -> {
                val p = id[0] - 'A'
                app.queuedPattern = p
                Native.queuePattern(p)
                if (!playing) { curPattern = p; editStep = -1 }
            }
            id == "REC" -> rec = !rec
            id == "FX" -> pageMode = 3
            id == "FXLATCH" -> { fxLatch = !fxLatch; if (!fxLatch) releaseFx() }
            id == "FXCLEAR" -> releaseFx()
            id == "SEQ" -> pageMode = 0
            id == "MIX" -> pageMode = 1
            id == "SONG" -> pageMode = 2
            id.startsWith("SOLO") -> { val t = id.substring(4).toInt(); proj.tracks[t].solo = !proj.tracks[t].solo; Native.setSolo(t, proj.tracks[t].solo) }
            id == "SONGMODE" -> { proj.songMode = !proj.songMode; proj.pushSong(songSel)
                if (proj.songMode && proj.songPat.isEmpty()) app.toast("Add rows with ADD first") }
            id == "SONGLOOP" -> { proj.songLoop = !proj.songLoop; proj.pushSong(songSel) }
            id == "SADD" -> if (proj.songPat.size < 64) { proj.songPat.add(curPattern); proj.songRep.add(1); songSel = proj.songPat.size - 1; proj.pushSong(songSel) }
            id == "SINS" -> if (proj.songPat.size < 64) { val at = songSel.coerceIn(0, proj.songPat.size); proj.songPat.add(at, curPattern); proj.songRep.add(at, 1); songSel = at; proj.pushSong(songSel) }
            id == "SDEL" -> if (songSel in proj.songPat.indices) { proj.songPat.removeAt(songSel); proj.songRep.removeAt(songSel); songSel = songSel.coerceAtMost(max(0, proj.songPat.size - 1)); proj.pushSong(songSel) }
            id == "SCLR" -> app.toast("Long-press CLEAR to empty the song")
            id == "COPY" -> { clipboard = PatternData().also { it.copyFrom(pat) }; app.toast("Pattern ${'A' + curPattern} copied") }
            id == "PASTE" -> clipboard?.let { pat.copyFrom(it); proj.pushPattern(curPattern); app.toast("Pasted into ${'A' + curPattern}") }
            id == "CLR" -> {
                for (i in 0 until MAX_STEPS) { pat.steps[selTrack][i].copyFrom(StepData()); proj.pushStep(curPattern, selTrack, i) }
                editStep = -1; app.toast("Track ${selTrack + 1} cleared (long-press CLR = whole pattern)")
            }
            id == "SAVE" -> app.saveDialog()
            id == "LOAD" -> app.loadDialog()
            id == "NEW" -> app.newProject()
            id.startsWith("MUTE") -> { val t = id.substring(4).toInt(); proj.tracks[t].mute = !proj.tracks[t].mute; Native.setMute(t, proj.tracks[t].mute) }
            id.startsWith("PAGE") -> { val p = id.substring(4).toInt(); if (p * 16 < pat.len[selTrack]) page = p }
            id == "LEN-" || id == "LEN+" -> {
                val l = (pat.len[selTrack] + if (id == "LEN+") 1 else -1).coerceIn(1, MAX_STEPS)
                pat.len[selTrack] = l; Native.setLength(curPattern, selTrack, l)
                if (page * 16 >= l) page = (l - 1) / 16
            }
            id == "CLRLOCK" -> if (editStep >= 0) {
                val s = pat.steps[selTrack][editStep]
                for (k in 0 until TRACK_PARAMS) s.lock[k] = -1f
                proj.pushStep(curPattern, selTrack, editStep)
            }
            id == "DONE" -> editStep = -1
            id == "OCT-" -> octave = (octave - 1).coerceAtLeast(0)
            id == "OCT+" -> octave = (octave + 1).coerceAtMost(7)
            id == "SLLOAD" -> app.pickSample(selTrack)
            id == "SLDETECT" -> app.setAutoSlices(selTrack)
            id == "SLSENS-" -> { tr.sliceSens = (tr.sliceSens - .1f).coerceIn(0f, 1f); app.setAutoSlices(selTrack) }
            id == "SLSENS+" -> { tr.sliceSens = (tr.sliceSens + .1f).coerceIn(0f, 1f); app.setAutoSlices(selTrack) }
            id == "SLEDIT" -> sliceEdit = !sliceEdit
            id == "SLZOOM+" -> app.decoded[selTrack]?.let { d -> val c = viewStart + visFrames(d) / 2; zoom = (zoom * 2).coerceAtMost(64); viewStart = c - visFrames(d) / 2; clampView(d) }
            id == "SLZOOM-" -> app.decoded[selTrack]?.let { d -> val c = viewStart + visFrames(d) / 2; zoom = (zoom / 2).coerceAtLeast(1); viewStart = c - visFrames(d) / 2; clampView(d) }
        }
    }

    // ---------------------------------------------------------------- mixer page
    private fun mixGet(strip: Int, k: Int): Float =
        if (strip == TRACKS) proj.master[intArrayOf(3, 6, 7, 8)[k]] else proj.tracks[strip].params[intArrayOf(13, 14, 15, 12)[k]]

    private fun mixSet(strip: Int, k: Int, v: Float) {
        val nv = v.coerceIn(0f, 1f)
        if (strip == TRACKS) { val i = intArrayOf(3, 6, 7, 8)[k]; proj.master[i] = nv; Native.setMaster(i, nv) }
        else { val i = intArrayOf(13, 14, 15, 12)[k]; proj.tracks[strip].params[i] = nv; Native.setParam(strip, i, nv) }
    }
    private fun mixDefault(strip: Int, k: Int): Float =
        if (strip == TRACKS) Defs.masterDefaults[intArrayOf(3, 6, 7, 8)[k]] else Defs.trackDefaults[intArrayOf(1, 2, 3, 0)[k]]

    private fun hitMixer(x: Float, y: Float): Target? {
        for (i in 0..TRACKS) {
            if (!mixStrip[i].contains(x, y)) continue
            for (k in 0 until 3) if (mixKnob[i][k].contains(x, y)) return Target.MixKnob(i, k)
            if (i < TRACKS && mixMute[i].contains(x, y)) return Target.Btn("MUTE$i")
            if (i < TRACKS && mixSolo[i].contains(x, y)) return Target.Btn("SOLO$i")
            if (y >= mixFader[i].top - u && y <= mixFader[i].bottom + u) return Target.Fader(i)
        }
        return null
    }

    private fun drawMixer(c: Canvas) {
        val names = arrayOf("Pan", "Delay", "Reverb")
        val mnames = arrayOf("Dly Lvl", "Rev Lvl", "Drive")
        for (i in 0..TRACKS) {
            val r = mixStrip[i]
            val master = i == TRACKS
            fill.color = if (!master && i == selTrack) 0xFF262A31.toInt() else panel
            c.drawRoundRect(r, u, u, fill)
            val col = if (master) 0xFF5AA9FF.toInt() else trackCol[i]
            fill.color = col; c.drawRoundRect(RectF(r.left, r.top, r.right, r.top + .8f * u), .4f * u, .4f * u, fill)
            text.textSize = 2.2f * u; text.color = textCol
            c.drawText(if (master) "MASTER" else "${i + 1} ${Defs.machines[proj.tracks[i].machine].short}", r.centerX(), r.top + 3.6f * u, text)
            for (k in 0 until 3) {
                val v = mixGet(i, k)
                val vt = if (master) (v * 100).roundToInt().toString() else Defs.formatParam(proj.tracks[i].machine, 13 + k, v)
                drawKnob(c, mixKnob[i][k], if (master) mnames[k] else names[k], v, vt, col)
            }
            // fader
            val f = mixFader[i]
            fill.color = 0xFF0E0F12.toInt()
            c.drawRoundRect(RectF(f.centerX() - .35f * u, f.top, f.centerX() + .35f * u, f.bottom), .3f * u, .3f * u, fill)
            val v = mixGet(i, 3)
            val cy = f.bottom - f.height() * v
            fill.color = col
            c.drawRoundRect(RectF(f.left, cy - 1.6f * u, f.right, cy + 1.6f * u), .5f * u, .5f * u, fill)
            text.textSize = 1.7f * u; text.color = Color.BLACK
            val gain = v * v * (if (master) 1.25f else 1.6f)
            val db = if (gain < 1e-4f) "-∞" else String.format("%.1f", 20f * kotlin.math.log10(gain))
            c.drawText(db, f.centerX(), cy + .6f * u, text)
            // meter
            val m = mixMeter[i]
            fill.color = 0xFF0E0F12.toInt(); c.drawRect(m, fill)
            val pk = ((if (master) status[18] else status[19 + i]) / 1000f).coerceIn(0f, 1.2f)
            val hgt = (pk / 1.2f) * m.height()
            fill.color = if (pk > .98f) 0xFFFF5050.toInt() else if (pk > .7f) 0xFFF5E663.toInt() else 0xFF6FE07A.toInt()
            c.drawRect(m.left, m.bottom - hgt, m.right, m.bottom, fill)
            if (!master) {
                drawBtn(c, mixMute[i], "M", proj.tracks[i].mute, onCol = 0xFFFF5050.toInt(), ts = 2f)
                drawBtn(c, mixSolo[i], "S", proj.tracks[i].solo, onCol = 0xFFF5E663.toInt(), ts = 2f)
            }
        }
    }

    // ---------------------------------------------------------------- song page
    private fun hitSong(x: Float, y: Float): Target? {
        for ((id, r) in songBtns) if (r.contains(x, y)) return Target.Btn(id)
        for (k in 0 until 64) if (songCells[k].contains(x, y)) {
            if (k >= proj.songPat.size) return null
            return Target.SongCell(k, x > songCells[k].centerX())
        }
        return null
    }

    private fun drawSong(c: Canvas) {
        for ((id, r) in songBtns) when (id) {
            "SONGMODE" -> drawBtn(c, r, if (proj.songMode) "SONG MODE ON" else "SONG MODE OFF", proj.songMode, onCol = 0xFF6FE07A.toInt(), ts = 2f)
            "SONGLOOP" -> drawBtn(c, r, "LOOP", proj.songLoop, onCol = 0xFF5AA9FF.toInt(), ts = 2f)
            "SADD" -> drawBtn(c, r, "ADD", ts = 2f)
            "SINS" -> drawBtn(c, r, "INSERT", ts = 2f)
            "SDEL" -> drawBtn(c, r, "DELETE", ts = 2f)
            "SCLR" -> drawBtn(c, r, "CLEAR", ts = 2f)
        }
        text.textAlign = Paint.Align.LEFT; text.textSize = 1.8f * u; text.color = subText
        c.drawText("ADD/INSERT use the current pattern • drag a row's pattern or ×repeats up/down • tap = play from here",
            songHint.left, songHint.centerY() + .6f * u, text)
        text.textAlign = Paint.Align.CENTER
        val playRow = if (playing && proj.songMode) status[27] else -1
        for (k in 0 until 64) {
            val r = songCells[k]
            if (k >= proj.songPat.size) {
                if (k == proj.songPat.size) { stroke.strokeWidth = 2f; stroke.color = dim; c.drawRoundRect(r, .6f * u, .6f * u, stroke) }
                continue
            }
            val pIdx = proj.songPat[k]
            fill.color = when { k == playRow -> 0xFF2F5A35.toInt(); k == songSel -> 0xFF3A3F4A.toInt(); else -> panel }
            c.drawRoundRect(r, .6f * u, .6f * u, fill)
            if (k == songSel) { stroke.strokeWidth = .3f * u; stroke.color = lockCol; c.drawRoundRect(r, .6f * u, .6f * u, stroke) }
            text.textAlign = Paint.Align.LEFT; text.textSize = 1.8f * u; text.color = subText
            c.drawText("${k + 1}", r.left + u, r.centerY() + .6f * u, text)
            text.textAlign = Paint.Align.CENTER
            text.textSize = 2.6f * u; text.color = textCol
            c.drawText("${'A' + pIdx}", r.left + r.width() * .35f, r.centerY() + .9f * u, text)
            text.textSize = 2.2f * u; text.color = textCol
            val rep = if (k == playRow) "${status[28] + 1}/${proj.songRep[k]}" else "×${proj.songRep[k]}"
            c.drawText(rep, r.left + r.width() * .75f, r.centerY() + .8f * u, text)
        }
    }

    fun onMachineChanged() { editStep = -1; invalidate() }
    fun onProjectLoaded() { editStep = -1; page = 0; curPattern = 0; showMaster = false; invalidate() }
}
