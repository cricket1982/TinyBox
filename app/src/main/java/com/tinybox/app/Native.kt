package com.tinybox.app

/** JNI bridge to the C++ engine (libtinybox.so). */
object Native {
    init { System.loadLibrary("tinybox") }

    @JvmStatic external fun start()
    @JvmStatic external fun stop()
    @JvmStatic external fun sampleRate(): Int

    @JvmStatic external fun play()
    @JvmStatic external fun halt()
    @JvmStatic external fun panic()
    @JvmStatic external fun setBpm(v: Float)
    @JvmStatic external fun setSwing(v: Float)
    @JvmStatic external fun setFill(on: Boolean)
    @JvmStatic external fun queuePattern(p: Int)

    @JvmStatic external fun setParam(track: Int, index: Int, value: Float)
    @JvmStatic external fun setMaster(index: Int, value: Float)
    @JvmStatic external fun setMute(track: Int, mute: Boolean)
    @JvmStatic external fun setSolo(track: Int, solo: Boolean)
    @JvmStatic external fun setFx(id: Int, on: Boolean)
    @JvmStatic external fun setLfo(i: Int, wave: Int, rate: Float, sync: Boolean, retrig: Boolean)
    @JvmStatic external fun setLfoDest(i: Int, slot: Int, dest: Int, depth: Float)
    @JvmStatic external fun setFxXY(x: Float, y: Float)
    @JvmStatic external fun setMachine(track: Int, machine: Int)
    @JvmStatic external fun setSong(patterns: IntArray, repeats: IntArray)
    @JvmStatic external fun setSongMode(on: Boolean, loop: Boolean, startRow: Int)

    @JvmStatic external fun setStep(
        pattern: Int, track: Int, step: Int, on: Int, vel: Int, prob: Int, cond: Int,
        ratchet: Int, chord: Int, note: Int, micro: Int, len: Float, n2: Int, n3: Int, n4: Int
    )
    @JvmStatic external fun setLock(pattern: Int, track: Int, step: Int, param: Int, value: Float)
    @JvmStatic external fun setLength(pattern: Int, track: Int, len: Int)
    @JvmStatic external fun setRes(pattern: Int, track: Int, res: Int)
    @JvmStatic external fun setInsert(track: Int, slot: Int, type: Int)
    @JvmStatic external fun setMasterFx(slot: Int, type: Int)
    @JvmStatic external fun clearPatterns()

    @JvmStatic external fun noteOn(track: Int, note: Int, vel: Float)
    @JvmStatic external fun noteOff(track: Int, note: Int)

    @JvmStatic external fun loadSample(track: Int, l: FloatArray, r: FloatArray, sampleRate: Float)
    @JvmStatic external fun setSlices(track: Int, starts: IntArray)

    /** [0]=playing [1]=pattern [2..9]=step per track [10..17]=trigger counters [18]=peak*1000
     *  [19..26]=track peaks*1000 [27]=song row [28]=song repeat [29]=compressor GR dB*100 [30..33]=LFO values*1000 */
    @JvmStatic external fun status(out: IntArray)
    @JvmStatic external fun relPos(): Double
    /** sequencer position (1/16 steps since pattern start) audible at an uptimeMillis moment, NaN if stopped */
    @JvmStatic external fun posAtTime(uptimeMs: Long): Double
    @JvmStatic external fun skipStep(track: Int, k: Long)
    @JvmStatic external fun setClick(on: Boolean)
}
