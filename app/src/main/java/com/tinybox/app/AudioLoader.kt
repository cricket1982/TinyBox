package com.tinybox.app

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.provider.OpenableColumns
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min

class Decoded(val l: FloatArray, val r: FloatArray, val sampleRate: Int, val name: String) {
    val frames get() = l.size
    /** min/max peaks for drawing */
    fun peaks(buckets: Int): FloatArray {
        val out = FloatArray(buckets * 2)
        if (frames == 0) return out
        for (b in 0 until buckets) {
            val s = (b.toLong() * frames / buckets).toInt()
            val e = max(s + 1, ((b + 1).toLong() * frames / buckets).toInt())
            var lo = 0f; var hi = 0f
            var i = s
            val stride = max(1, (e - s) / 64)
            while (i < e && i < frames) { val v = (l[i] + r[i]) * .5f; if (v < lo) lo = v; if (v > hi) hi = v; i += stride }
            out[b * 2] = lo; out[b * 2 + 1] = hi
        }
        return out
    }
    fun peaksRange(start: Int, end: Int, buckets: Int): FloatArray {
        val out = FloatArray(buckets * 2)
        val s0 = start.coerceIn(0, frames); val e0 = end.coerceIn(s0, frames)
        val span = e0 - s0
        if (span <= 0) return out
        for (b in 0 until buckets) {
            val s = s0 + (b.toLong() * span / buckets).toInt()
            val e = max(s + 1, s0 + ((b + 1).toLong() * span / buckets).toInt())
            var lo = 0f; var hi = 0f
            val stride = max(1, (e - s) / 48)
            var i = s
            while (i < e && i < frames) { val v = (l[i] + r[i]) * .5f; if (v < lo) lo = v; if (v > hi) hi = v; i += stride }
            out[b * 2] = lo; out[b * 2 + 1] = hi
        }
        return out
    }
}

object AudioLoader {
    private const val MAX_FRAMES = 48000 * 90 // ~90 s

    fun displayName(ctx: Context, uri: Uri): String {
        try {
            ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) return c.getString(0)
            }
        } catch (_: Exception) {}
        return uri.lastPathSegment ?: "sample"
    }

    fun load(ctx: Context, uri: Uri): Decoded? {
        val name = displayName(ctx, uri)
        try {
            val bytes = ctx.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return null
            parseWav(bytes, name)?.let { return it }
        } catch (_: OutOfMemoryError) { return null } catch (_: Exception) {}
        return try { decodeMedia(ctx, uri, name) } catch (_: Exception) { null }
    }

    private fun parseWav(b: ByteArray, name: String): Decoded? {
        if (b.size < 44) return null
        val bb = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        if (String(b, 0, 4) != "RIFF" || String(b, 8, 4) != "WAVE") return null
        var pos = 12
        var fmt = 1; var ch = 2; var sr = 44100; var bits = 16
        var dataOff = -1; var dataLen = 0
        while (pos + 8 <= b.size) {
            val id = String(b, pos, 4)
            val len = bb.getInt(pos + 4)
            val body = pos + 8
            if (id == "fmt ") {
                fmt = bb.getShort(body).toInt() and 0xffff
                ch = bb.getShort(body + 2).toInt()
                sr = bb.getInt(body + 4)
                bits = bb.getShort(body + 14).toInt()
                if (fmt == 0xFFFE && len >= 26) fmt = bb.getShort(body + 24).toInt() and 0xffff
            } else if (id == "data") {
                dataOff = body; dataLen = min(len.let { if (it < 0) Int.MAX_VALUE else it }, b.size - body)
                break
            }
            pos = body + len + (len and 1)
            if (len < 0) return null
        }
        if (dataOff < 0 || ch < 1) return null
        val bpsm = bits / 8
        if (bpsm < 1) return null
        val frames = min(dataLen / (bpsm * ch), MAX_FRAMES)
        val l = FloatArray(frames); val r = FloatArray(frames)
        for (f in 0 until frames) {
            for (c in 0 until min(ch, 2)) {
                val o = dataOff + (f * ch + c) * bpsm
                val v = when {
                    fmt == 3 && bits == 32 -> bb.getFloat(o)
                    fmt == 3 && bits == 64 -> bb.getDouble(o).toFloat()
                    bits == 8 -> ((b[o].toInt() and 0xff) - 128) / 128f
                    bits == 16 -> bb.getShort(o) / 32768f
                    bits == 24 -> ((b[o].toInt() and 0xff) or ((b[o + 1].toInt() and 0xff) shl 8) or (b[o + 2].toInt() shl 16)) / 8388608f
                    bits == 32 -> bb.getInt(o) / 2147483648f
                    else -> 0f
                }
                if (c == 0) l[f] = v else r[f] = v
            }
            if (ch == 1) r[f] = l[f]
        }
        return Decoded(l, r, sr, name)
    }

    private fun decodeMedia(ctx: Context, uri: Uri, name: String): Decoded? {
        val ex = MediaExtractor()
        ex.setDataSource(ctx, uri, null)
        var track = -1
        for (i in 0 until ex.trackCount) {
            if (ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) { track = i; break }
        }
        if (track < 0) { ex.release(); return null }
        ex.selectTrack(track)
        val fmt = ex.getTrackFormat(track)
        val codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME)!!)
        codec.configure(fmt, null, null, 0)
        codec.start()
        var ch = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        var sr = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        var isFloat = false
        var outL = FloatArray(1 shl 16); var outR = FloatArray(1 shl 16); var n = 0
        val info = MediaCodec.BufferInfo()
        var inDone = false; var outDone = false
        while (!outDone && n < MAX_FRAMES) {
            if (!inDone) {
                val ii = codec.dequeueInputBuffer(5000)
                if (ii >= 0) {
                    val buf = codec.getInputBuffer(ii)!!
                    val sz = ex.readSampleData(buf, 0)
                    if (sz < 0) { codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true }
                    else { codec.queueInputBuffer(ii, 0, sz, ex.sampleTime, 0); ex.advance() }
                }
            }
            val oi = codec.dequeueOutputBuffer(info, 5000)
            if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                val of = codec.outputFormat
                ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT); sr = of.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                isFloat = of.containsKey(MediaFormat.KEY_PCM_ENCODING) && of.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT
            } else if (oi >= 0) {
                val buf = codec.getOutputBuffer(oi)!!.order(ByteOrder.LITTLE_ENDIAN)
                buf.position(info.offset); buf.limit(info.offset + info.size)
                val bytesPer = if (isFloat) 4 else 2
                val frames = info.size / (bytesPer * ch)
                if (n + frames > outL.size) {
                    val ns = max(outL.size * 2, n + frames)
                    outL = outL.copyOf(ns); outR = outR.copyOf(ns)
                }
                for (f in 0 until frames) {
                    val a = if (isFloat) buf.float else buf.short / 32768f
                    var bR = a
                    if (ch >= 2) { bR = if (isFloat) buf.float else buf.short / 32768f; repeat(ch - 2) { if (isFloat) buf.float else buf.short } }
                    outL[n] = a; outR[n] = bR; n++
                }
                codec.releaseOutputBuffer(oi, false)
                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outDone = true
            }
        }
        codec.stop(); codec.release(); ex.release()
        if (n == 0) return null
        return Decoded(outL.copyOf(n), outR.copyOf(n), sr, name)
    }

    fun evenSlices(frames: Int, count: Int): IntArray = IntArray(count) { (it.toLong() * frames / count).toInt() }

    /**
     * Transient detection on the rise of log energy (so quiet passages are found as well as
     * loud ones). sensitivity 0..1: higher = more slices. Always includes 0.
     */
    fun transientSlices(d: Decoded, sensitivity: Float, maxSlices: Int = 64): IntArray {
        val hop = 256; val win = 512
        val n = d.frames
        val nb = (n - win) / hop
        if (nb < 4) return intArrayOf(0)
        val dbF = FloatArray(nb); val dbH = FloatArray(nb)
        for (b in 0 until nb) {
            val s = b * hop
            var ef = 0.0; var eh = 0.0
            var prev = if (s > 0) (d.l[s - 1] + d.r[s - 1]) * .5f else 0f
            for (i in s until s + win) {
                val x = (d.l[i] + d.r[i]) * .5f
                val h = x - prev; prev = x
                ef += (x * x).toDouble(); eh += (h * h).toDouble()
            }
            dbF[b] = (10.0 * log10(ef / win + 1e-12)).toFloat()
            dbH[b] = (10.0 * log10(eh / win + 1e-12)).toFloat()
        }
        val loudest = dbF.maxOrNull() ?: 0f
        val floor = loudest - 55f
        val flux = FloatArray(nb)
        for (b in 2 until nb) {
            if (dbF[b] < floor) continue
            val rf = dbF[b] - max(dbF[b - 1], dbF[b - 2])
            val rh = dbH[b] - max(dbH[b - 1], dbH[b - 2])
            flux[b] = max(0f, rf) * .5f + max(0f, rh)
        }
        val delta = 14f - sensitivity.coerceIn(0f, 1f) * 12f   // dB of rise needed above the local median
        val half = 20
        val tmp = FloatArray(2 * half + 1)
        val cand = ArrayList<Pair<Int, Float>>()
        for (b in 2 until nb - 1) {
            if (flux[b] <= 0f || flux[b] < flux[b - 1] || flux[b] < flux[b + 1]) continue
            var c = 0
            for (k in max(0, b - half)..min(nb - 1, b + half)) tmp[c++] = flux[k]
            java.util.Arrays.sort(tmp, 0, c)
            val med = tmp[c / 2]
            if (flux[b] > med + delta) cand.add(b to flux[b])
        }
        // enforce a minimum gap (keep the stronger onset), then cap the count
        val minGap = max(1, (0.045f * d.sampleRate / hop).toInt())
        val byTime = ArrayList<Pair<Int, Float>>()
        for (c in cand) {
            if (c.first < minGap) continue
            val last = byTime.lastOrNull()
            if (last != null && c.first - last.first < minGap) { if (c.second > last.second) byTime[byTime.size - 1] = c }
            else byTime.add(c)
        }
        val kept = if (byTime.size > maxSlices - 1) byTime.sortedByDescending { it.second }.take(maxSlices - 1).sortedBy { it.first } else byTime
        val out = ArrayList<Int>(); out.add(0)
        for ((b, _) in kept) out.add(refineOnset(d, b * hop, win))
        return out.distinct().sorted().toIntArray()
    }

    /** move a coarse onset to just before the attack, at a zero crossing */
    private fun refineOnset(d: Decoded, start: Int, win: Int): Int {
        val end = min(d.frames - 1, start + win)
        var pk = 0f
        for (i in start until end) pk = max(pk, abs(d.l[i] + d.r[i]) * .5f)
        var at = start
        for (i in start until end) if (abs(d.l[i] + d.r[i]) * .5f > pk * .25f) { at = i; break }
        at = max(0, at - 48)
        var i = at
        val lim = max(0, at - 192)
        while (i > lim) {
            val a = d.l[i] + d.r[i]; val b = d.l[i - 1] + d.r[i - 1]
            if ((a >= 0f) != (b >= 0f)) return i
            i--
        }
        return at
    }
}
