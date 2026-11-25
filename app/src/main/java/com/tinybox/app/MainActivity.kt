package com.tinybox.app

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.EditText
import android.widget.Toast
import org.json.JSONObject
import java.io.File
import kotlin.concurrent.thread

class MainActivity : Activity() {

    var project = Project.demo()
    var queuedPattern = 0
    val decoded = arrayOfNulls<Decoded>(TRACKS)
    private val peakCaches = arrayOfNulls<FloatArray>(TRACKS)
    private lateinit var view: GrooveView
    private val ui = Handler(Looper.getMainLooper())
    private val ticker = object : Runnable {
        override fun run() { view.refreshFromEngine(); ui.postDelayed(this, 33) }
    }

    private val projectDir by lazy { File(filesDir, "projects").apply { mkdirs() } }
    private val autosave by lazy { File(filesDir, "autosave.json") }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        Native.start()
        view = GrooveView(this)
        setContentView(view)
        hideSystemBars()
        val restored = try { if (autosave.exists()) Project.fromJson(JSONObject(autosave.readText())) else null } catch (_: Exception) { null }
        applyProject(restored ?: Project.demo())
    }

    override fun onResume() { super.onResume(); hideSystemBars(); ui.post(ticker) }
    override fun onPause() { super.onPause(); ui.removeCallbacks(ticker); view.releaseFx(); saveTo(autosave) }
    override fun onDestroy() { super.onDestroy(); if (isFinishing) Native.stop() }
    override fun onWindowFocusChanged(hasFocus: Boolean) { super.onWindowFocusChanged(hasFocus); if (hasFocus) hideSystemBars() }

    @Suppress("DEPRECATION")
    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
        }
    }

    fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    /** fallback latency estimate in steps (only used if the audio timestamp isn't available) */
    fun latencySteps(): Double = 0.040 * project.bpm / 60.0 * 4.0

    // ---- recording settings (kept between sessions)
    private val prefs by lazy { getSharedPreferences("tinybox", MODE_PRIVATE) }
    var recQuant: Float
        get() = prefs.getFloat("recQuant", 0f)
        set(v) { prefs.edit().putFloat("recQuant", v).apply() }
    var recOffsetMs: Int
        get() = prefs.getInt("recOffsetMs", 0)
        set(v) { prefs.edit().putInt("recOffsetMs", v).apply() }
    var clickOn: Boolean
        get() = prefs.getBoolean("click", true)
        set(v) { prefs.edit().putBoolean("click", v).apply() }

    /** position heard at a touch's timestamp, in 1/16 steps since the pattern started */
    fun posAt(uptimeMs: Long): Double {
        val p = Native.posAtTime(uptimeMs + recOffsetMs)
        return if (p.isNaN()) Native.relPos() - latencySteps() else p
    }

    fun recOffsetDialog(done: () -> Unit) {
        val opts = listOf(-40, -30, -20, -10, 0, 10, 20, 30, 40)
        val names = opts.map { (if (it > 0) "+" else "") + "$it ms" + when { it < 0 -> " (hits land earlier)"; it > 0 -> " (hits land later)"; else -> " (default)" } }.toTypedArray()
        AlertDialog.Builder(this).setTitle("Record timing offset")
            .setSingleChoiceItems(names, opts.indexOf(recOffsetMs).coerceAtLeast(0)) { d, w -> recOffsetMs = opts[w]; d.dismiss(); done() }
            .setNegativeButton("Cancel", null).show()
    }

    // ------------------------------------------------------------ project handling
    private fun applyProject(p: Project) {
        Native.halt()
        project = p
        queuedPattern = 0
        Native.queuePattern(0)
        for (t in 0 until TRACKS) { decoded[t] = null; peakCaches[t] = null }
        project.pushAll()
        view.onProjectLoaded()
        for (t in 0 until TRACKS) {
            val uri = project.tracks[t].sampleUri ?: continue
            loadSampleAsync(t, Uri.parse(uri), keepSlices = true)
        }
    }

    private fun saveTo(f: File) {
        try { f.writeText(project.toJson().toString()) } catch (e: Exception) { toast("Save failed: ${e.message}") }
    }

    fun saveDialog() {
        val et = EditText(this).apply { setText(project.name); setSelectAllOnFocus(true); inputType = InputType.TYPE_CLASS_TEXT }
        AlertDialog.Builder(this).setTitle("Save project").setView(et)
            .setPositiveButton("Save") { _, _ ->
                val name = et.text.toString().trim().replace(Regex("[^A-Za-z0-9 _-]"), "").ifEmpty { "untitled" }
                project.name = name
                saveTo(File(projectDir, "$name.json")); toast("Saved \"$name\"")
            }
            .setNegativeButton("Cancel", null).show()
    }

    fun loadDialog() {
        val files = projectDir.listFiles { f -> f.name.endsWith(".json") }?.sortedByDescending { it.lastModified() } ?: emptyList()
        if (files.isEmpty()) { toast("No saved projects yet"); return }
        val names = files.map { it.name.removeSuffix(".json") }.toTypedArray()
        AlertDialog.Builder(this).setTitle("Load project")
            .setItems(names) { _, which ->
                try { applyProject(Project.fromJson(JSONObject(files[which].readText()))); toast("Loaded \"${names[which]}\"") }
                catch (e: Exception) { toast("Load failed: ${e.message}") }
            }
            .setNeutralButton("Delete…") { _, _ -> deleteDialog(files, names) }
            .setNegativeButton("Cancel", null).show()
    }

    private fun deleteDialog(files: List<File>, names: Array<String>) {
        AlertDialog.Builder(this).setTitle("Delete which project?")
            .setItems(names) { _, which ->
                AlertDialog.Builder(this).setMessage("Delete \"${names[which]}\"?")
                    .setPositiveButton("Delete") { _, _ -> files[which].delete(); toast("Deleted") }
                    .setNegativeButton("Cancel", null).show()
            }.setNegativeButton("Cancel", null).show()
    }

    fun newProject() {
        AlertDialog.Builder(this).setTitle("New project")
            .setItems(arrayOf("Demo kit + beat", "Empty (same kit, no steps)")) { _, which ->
                if (which == 0) applyProject(Project.demo())
                else {
                    val p = Project()
                    for (t in 0 until TRACKS) { p.tracks[t].machine = project.tracks[t].machine; project.tracks[t].params.copyInto(p.tracks[t].params)
                        p.tracks[t].sampleUri = project.tracks[t].sampleUri; p.tracks[t].sampleName = project.tracks[t].sampleName; p.tracks[t].slices = project.tracks[t].slices.copyOf() }
                    p.bpm = project.bpm
                    applyProject(p)
                }
            }.setNegativeButton("Cancel", null).show()
    }

    fun menu(items: List<Pair<String, () -> Unit>>) {
        AlertDialog.Builder(this).setTitle("TinyBox")
            .setItems(items.map { it.first }.toTypedArray()) { _, w -> items[w].second() }
            .setNegativeButton("Close", null).show()
    }

    /** two-step picker: track / master, then the parameter */
    fun chooseLfoDest(k: Int, slot: Int, done: () -> Unit) {
        val groups = listOf("— none —") + (0 until TRACKS).map { "Track ${it + 1} · ${Defs.machines[project.tracks[it].machine].name}" } + listOf("Master")
        AlertDialog.Builder(this).setTitle("LFO ${k + 1} → destination ${slot + 1}")
            .setItems(groups.toTypedArray()) { _, g ->
                val l = project.lfos[k]
                if (g == 0) { l.dest[slot] = -1; project.pushLfo(k); done(); return@setItems }
                val opts: List<Pair<Int, String>> = if (g <= TRACKS) {
                    val t = g - 1
                    (0 until TRACK_PARAMS).map { i -> (t * DEST_STRIDE + i) to project.paramName(t, i) }.filter { it.second.isNotEmpty() }
                } else Defs.masterParams.indices.map { i -> (1000 + i) to Defs.masterParams[i] }
                AlertDialog.Builder(this).setTitle(groups[g])
                    .setItems(opts.map { it.second }.toTypedArray()) { _, w ->
                        l.dest[slot] = opts[w].first
                        if (l.depth[slot] == 0f) l.depth[slot] = .5f
                        project.pushLfo(k); done()
                    }.setNegativeButton("Cancel", null).show()
            }.setNegativeButton("Cancel", null).show()
    }

    fun chooseInsert(t: Int, slot: Int, done: () -> Unit) {
        val names = Defs.inserts.map { it.name }.toTypedArray()
        AlertDialog.Builder(this).setTitle("Track ${t + 1} insert ${if (slot == 0) "A" else "B"}")
            .setSingleChoiceItems(names, project.tracks[t].insType[slot]) { d, w -> project.setInsert(t, slot, w); d.dismiss(); done() }
            .setNegativeButton("Cancel", null).show()
    }

    fun chooseFxSlot(slot: Int, done: (Int) -> Unit) {
        val opts = listOf(-1 to "— empty —") + Defs.fxLib
        AlertDialog.Builder(this).setTitle("FX pad ${slot + 1}")
            .setItems(opts.map { it.second.replace("\n", " ") }.toTypedArray()) { _, w -> done(opts[w].first) }
            .setNegativeButton("Cancel", null).show()
    }

    fun editBpm() {
        val et = EditText(this).apply { setText(project.bpm.toString()); inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL; setSelectAllOnFocus(true) }
        AlertDialog.Builder(this).setTitle("Tempo (BPM)").setView(et)
            .setPositiveButton("OK") { _, _ ->
                et.text.toString().toFloatOrNull()?.let { project.bpm = it.coerceIn(40f, 250f); Native.setBpm(project.bpm) }
            }.setNegativeButton("Cancel", null).show()
    }

    fun chooseMachine(t: Int) {
        val order = Defs.pickerOrder
        val names = order.map { "${Defs.machines[it].name}  (${Defs.group(it)})" }.toTypedArray()
        AlertDialog.Builder(this).setTitle("Track ${t + 1} machine")
            .setSingleChoiceItems(names, order.indexOf(project.tracks[t].machine)) { d, pick ->
                val which = order[pick]
                val tr = project.tracks[t]
                tr.setMachineDefaults(which)
                Native.setMachine(t, which)
                for (i in 0 until TRACK_PARAMS) Native.setParam(t, i, tr.params[i])
                if (Defs.usesSample(which)) {
                    decoded[t]?.let { Native.loadSample(t, it.l, it.r, it.sampleRate.toFloat()) }
                    Native.setSlices(t, tr.slices)
                }
                view.selTrack = t; view.showMaster = false
                view.onMachineChanged()
                d.dismiss()
            }.setNegativeButton("Cancel", null).show()
    }

    // ------------------------------------------------------------ samples
    private var pendingSampleTrack = -1

    fun pickSample(t: Int) {
        pendingSampleTrack = t
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("audio/*")
        @Suppress("DEPRECATION")
        startActivityForResult(i, 4242)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != 4242 || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) } catch (_: Exception) {}
        val t = pendingSampleTrack.takeIf { it in 0 until TRACKS } ?: return
        loadSampleAsync(t, uri, keepSlices = false)
    }

    private fun loadSampleAsync(t: Int, uri: Uri, keepSlices: Boolean) {
        if (!keepSlices) toast("Loading sample…")
        thread {
            val d = AudioLoader.load(this, uri)
            ui.post {
                if (d == null) { toast("Couldn't read that audio file"); return@post }
                val tr = project.tracks[t]
                decoded[t] = d; peakCaches[t] = null
                tr.sampleUri = uri.toString(); tr.sampleName = d.name
                if (!keepSlices || tr.slices.any { it >= d.frames }) tr.slices = AudioLoader.transientSlices(d, tr.sliceSens)
                Native.loadSample(t, d.l, d.r, d.sampleRate.toFloat())
                Native.setSlices(t, tr.slices)
                view.invalidate()
            }
        }
    }


    fun setAutoSlices(t: Int) {
        val d = decoded[t] ?: run { toast("Load a sample first"); return }
        val sens = project.tracks[t].sliceSens
        thread {
            val s = AudioLoader.transientSlices(d, sens)
            ui.post { project.tracks[t].slices = s; Native.setSlices(t, s); toast("Found ${s.size} slices"); view.invalidate() }
        }
    }

    fun peakCache(t: Int, buckets: Int): FloatArray {
        val c = peakCaches[t]
        if (c != null && c.size == buckets * 2) return c
        val d = decoded[t] ?: return FloatArray(2)
        return d.peaks(buckets.coerceAtLeast(1)).also { peakCaches[t] = it }
    }
}
