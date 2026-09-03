package com.dsmile.emulator.ui

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.net.Uri
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.SeekBar
import android.widget.Toast
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.dsmile.emulator.emu.AspectMode
import com.dsmile.emulator.emu.BackgroundMode
import com.dsmile.emulator.emu.BezelMode
import com.dsmile.emulator.emu.GameRenderer
import com.dsmile.emulator.emu.NativeCore
import com.dsmile.emulator.emu.ShaderMode
import com.dsmile.emulator.input.Action
import com.dsmile.emulator.input.HotkeyListener
import com.dsmile.emulator.input.InputMapper
import java.io.File

class EmuActivity : Activity(), TouchOverlayView.Listener, HotkeyListener {

    private lateinit var glView: GLSurfaceView
    private lateinit var rootLayout: FrameLayout
    private lateinit var overlay: TouchOverlayView

    // layout editor state
    private var layoutEditing = false
    private var editingBaseName: String? = null  // null = based on Default (must Save As)
    private var editPanel: android.widget.LinearLayout? = null
    private var scaleBox: android.widget.EditText? = null
    private var suppressScaleWatcher = false
    private lateinit var renderer: GameRenderer
    private lateinit var mapper: InputMapper
    private val prefs by lazy { getSharedPreferences("dsmile", Context.MODE_PRIVATE) }

    private var romName = "game"
    private var initialized = false
    private var menuOpen = false
    private var ffOn = false
    private var accurateRender = false
    private var touchJoyX = 0
    private var touchJoyY = 0
    private var touchButtons = 0
    private var captureAction: Action? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        mapper = InputMapper(prefs)
        renderer = GameRenderer().apply {
            shaderMode = ShaderMode.valueOf(prefs.getString("shader", ShaderMode.SHARP.name)!!)
            aspectMode = AspectMode.valueOf(prefs.getString("aspect", AspectMode.FOUR_THREE.name)!!)
            crtCurve = prefs.getFloat("crtCurveV", 1f)
            crtGlow = prefs.getFloat("crtGlowV", 1f)
            crtScan = prefs.getFloat("crtScanV", 1f)
            crtMask = prefs.getFloat("crtMaskV", 1f)
            crtVignette = prefs.getFloat("crtVigV", 1f)
            backgroundMode = BackgroundMode.valueOf(
                prefs.getString("background", BackgroundMode.BLACK.name)!!
            )
            bezelMode = BezelMode.valueOf(prefs.getString("bezel", BezelMode.NONE.name)!!)
        }
        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(2)
            setRenderer(renderer)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        overlay = TouchOverlayView(this).apply {
            listener = this@EmuActivity
            controlOpacity = prefs.getFloat("opacity", 0.55f)
            controlsVisible = prefs.getBoolean("touchControls", true)
            pinkTheme = prefs.getBoolean("pinkTheme", false)
            joySwap = prefs.getBoolean("joySwap", false)
        }
        rootLayout = FrameLayout(this)
        rootLayout.addView(glView)
        rootLayout.addView(overlay)
        setContentView(rootLayout)
        overlay.post { applyLayoutByName(prefs.getString("activeLayout", "Default")!!) }

        val rom = loadRomBytes()
        if (rom == null) {
            Toast.makeText(this, "Could not read ROM", Toast.LENGTH_LONG).show()
            finish()
            return
        }
        val pal = intent.getBooleanExtra("pal", prefs.getBoolean("pal", false))
        val sysrom = loadSysromBytes()  // always load when available (games call BIOS routines)
        if (!NativeCore.nativeInit(rom, sysrom, pal, prefs.getBoolean("playIntro", true))) {
            Toast.makeText(this, "Failed to initialize emulator", Toast.LENGTH_LONG).show()
            finish()
            return
        }
        initialized = true
        accurateRender = prefs.getBoolean("accurate", false)
        NativeCore.nativeSetAccurate(accurateRender)
        NativeCore.nativeSetFastForwardSpeed(prefs.getFloat("ffSpeed", 3f))
        NativeCore.nativeStart()

        // Poll controller LEDs into the overlay a few times a second.
        overlay.postDelayed(object : Runnable {
            override fun run() {
                if (!initialized) return
                overlay.leds = NativeCore.nativeGetLeds()
                overlay.postDelayed(this, 250)
            }
        }, 250)
    }

    /** Accepts: data Uri (content/file), or string extras rom/ROM/path (file path or uri). */
    private fun loadRomBytes(): ByteArray? {
        val uri: Uri? = intent.data
        val extraPath = intent.getStringExtra("rom")
            ?: intent.getStringExtra("ROM")
            ?: intent.getStringExtra("path")
            ?: intent.getStringExtra("uri")
        return try {
            when {
                uri != null -> readUri(uri)
                extraPath != null -> {
                    romName = extraPath.substringAfterLast('/').substringAfterLast('\\')
                    if (extraPath.startsWith("content:") || extraPath.startsWith("file:")) {
                        readUri(Uri.parse(extraPath))
                    } else {
                        File(extraPath).readBytes()
                    }
                }
                else -> null
            }
        } catch (e: Exception) {
            null
        }
    }

    private fun readUri(uri: Uri): ByteArray? {
        if (uri.scheme == "file") return File(uri.path!!).readBytes()
        contentResolver.query(uri, null, null, null, null)?.use { c ->
            val idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && c.moveToFirst()) romName = c.getString(idx) ?: romName
        }
        return contentResolver.openInputStream(uri)?.use { it.readBytes() }
    }

    private fun loadSysromBytes(): ByteArray? {
        // Imported BIOS (validated, app-internal) wins; ROM-folder detection is the fallback.
        val imported = File(filesDir, "sysrom.bin")
        if (imported.exists()) {
            return try { imported.readBytes() } catch (e: Exception) { null }
        }
        val uriStr = prefs.getString("sysromUri", null) ?: return null
        return try {
            readUri(Uri.parse(uriStr))
        } catch (e: Exception) {
            null
        }
    }

    // ---------------- input ----------------

    private fun pushInput() {
        val jx = if (touchJoyX != 0 || touchJoyY != 0) touchJoyX else mapper.joyX
        val jy = if (touchJoyX != 0 || touchJoyY != 0) touchJoyY else mapper.joyY
        NativeCore.nativeSetInput(jx, jy, touchButtons or mapper.buttons())
    }

    override fun onTouchInput(joyX: Int, joyY: Int, buttons: Int) {
        touchJoyX = joyX
        touchJoyY = joyY
        touchButtons = buttons
        pushInput()
    }

    override fun onMenuRequested() {
        if (!layoutEditing) showMenu()
    }

    override fun onEditSelectionChanged(count: Int, scale: Float) {
        suppressScaleWatcher = true
        scaleBox?.setText(String.format("%.1f", scale))
        suppressScaleWatcher = false
    }

    override fun onEditLayoutDirty() {}

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (layoutEditing) {
            if (event.keyCode == KeyEvent.KEYCODE_BACK && event.action == KeyEvent.ACTION_UP) {
                confirmDiscardEdit()
            }
            return true  // no game inputs while editing the layout
        }
        captureAction?.let { action ->
            if (event.keyCode == KeyEvent.KEYCODE_BACK) return super.dispatchKeyEvent(event)
            if (event.action == KeyEvent.ACTION_DOWN) {
                mapper.bind(event.keyCode, action)
                Toast.makeText(this, "${action.label} → ${KeyEvent.keyCodeToString(event.keyCode).removePrefix("KEYCODE_")}", Toast.LENGTH_SHORT).show()
                if (wizardActive) advanceWizard() else captureAction = null
            }
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_UP) showMenu()
            return true
        }
        if (mapper.onKey(event, this)) {
            pushInput()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (layoutEditing) return true
        if (mapper.onMotion(event, this)) {
            pushInput()
            return true
        }
        return super.dispatchGenericMotionEvent(event)
    }

    override fun onHotkey(action: Action, pressed: Boolean) {
        when (action) {
            Action.FAST_FORWARD -> NativeCore.nativeSetFastForward(pressed)
            Action.REWIND -> NativeCore.nativeSetRewind(pressed)
            Action.SAVE_STATE -> if (pressed) saveState(lastSlot())
            Action.LOAD_STATE -> if (pressed) loadState(lastSlot())
            Action.MENU -> if (pressed) showMenu()
            else -> {}
        }
    }

    // ---------------- save states ----------------

    private fun stateFile(slot: Int): File {
        val dir = File(getExternalFilesDir(null), "states").apply { mkdirs() }
        return File(dir, "$romName.slot$slot.dss")
    }

    private fun thumbFile(slot: Int): File {
        val dir = File(getExternalFilesDir(null), "states").apply { mkdirs() }
        return File(dir, "$romName.slot$slot.png")
    }

    /** Grabs the currently-displayed frame as a small thumbnail bitmap. */
    private fun captureThumbnail(): android.graphics.Bitmap? {
        return try {
            val buf = java.nio.ByteBuffer.allocateDirect(320 * 240 * 2)
                .order(java.nio.ByteOrder.nativeOrder())
            if (NativeCore.nativeGetFrame(buf) < 0) return null
            buf.position(0)
            val full = android.graphics.Bitmap.createBitmap(320, 240, android.graphics.Bitmap.Config.RGB_565)
            full.copyPixelsFromBuffer(buf)
            val thumb = android.graphics.Bitmap.createScaledBitmap(full, 160, 120, true)
            full.recycle()
            thumb
        } catch (e: Exception) {
            null
        }
    }

    private fun saveState(slot: Int) {
        val data = NativeCore.nativeSaveState()
        if (data != null) {
            stateFile(slot).writeBytes(data)
            captureThumbnail()?.let { t ->
                try {
                    thumbFile(slot).outputStream().use {
                        t.compress(android.graphics.Bitmap.CompressFormat.PNG, 90, it)
                    }
                } catch (e: Exception) { /* thumbnail is best-effort */ }
                t.recycle()
            }
            prefs.edit().putInt("lastSlot_$romName", slot).apply()
            Toast.makeText(this, "State saved (slot ${slot + 1})", Toast.LENGTH_SHORT).show()
        }
    }

    private fun loadState(slot: Int) {
        val f = stateFile(slot)
        if (!f.exists()) {
            Toast.makeText(this, "No state in slot ${slot + 1}", Toast.LENGTH_SHORT).show()
            return
        }
        if (NativeCore.nativeLoadState(f.readBytes())) {
            prefs.edit().putInt("lastSlot_$romName", slot).apply()
            Toast.makeText(this, "State loaded (slot ${slot + 1})", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "State is for a different game/version", Toast.LENGTH_LONG).show()
        }
    }

    private fun lastSlot(): Int = prefs.getInt("lastSlot_$romName", 0)

    // ---------------- menu ----------------

    @SuppressLint("SetTextI18n")
    private fun showMenu() {
        if (menuOpen || !initialized) return
        menuOpen = true
        releaseAllInputs()
        NativeCore.nativeSetPaused(true)
        val items = arrayOf(
            "Resume",
            "Save state…",
            "Load state…",
            if (ffOn) "Fast forward: ON" else "Fast forward: OFF",
            "Fast forward speed…",
            "Controls layout…",
            "Edit controls layout",
            "Controller theme: ${if (overlay.pinkTheme) "Pink" else "Classic"}",
            "Joystick colors: ${if (overlay.joySwap) "Swapped" else "Normal"}",
            if (overlay.controlsVisible) "Hide touch controls" else "Show touch controls",
            "Controls opacity…",
            "Shader…",
            "CRT options…",
            "Aspect ratio…",
            "Render mode: ${if (accurateRender) "Accurate" else "Fast"}",
            "Background…",
            "Bezel…",
            "Map controller buttons…",
            "Trigger sensitivity…",
            "Reset game",
            "Quit"
        )
        val version = try {
            packageManager.getPackageInfo(packageName, 0).versionName
        } catch (e: Exception) { "?" }
        AlertDialog.Builder(this)
            .setTitle("${romName.substringBeforeLast('.')}  —  D.Smile v$version")
            .setItems(items) { _, which ->
                when (which) {
                    1 -> pickSlot("Save to slot", isSave = true) { saveState(it) }
                    2 -> pickSlot("Load from slot", isSave = false) { loadState(it) }
                    3 -> {
                        ffOn = !ffOn
                        NativeCore.nativeSetFastForward(ffOn)
                    }
                    4 -> {
                        val speeds = listOf(2f, 3f, 4f, 8f, 0f)
                        val labels = listOf("2x", "3x", "4x", "8x", "Uncapped")
                        val cur = speeds.indexOf(prefs.getFloat("ffSpeed", 3f)).coerceAtLeast(0)
                        pickChoice("Fast forward speed", labels, cur) {
                            prefs.edit().putFloat("ffSpeed", speeds[it]).apply()
                            NativeCore.nativeSetFastForwardSpeed(speeds[it])
                        }
                    }
                    5 -> showLayoutsManager()
                    6 -> enterLayoutEdit()
                    7 -> {
                        overlay.pinkTheme = !overlay.pinkTheme
                        prefs.edit().putBoolean("pinkTheme", overlay.pinkTheme).apply()
                    }
                    8 -> {
                        overlay.joySwap = !overlay.joySwap
                        prefs.edit().putBoolean("joySwap", overlay.joySwap).apply()
                    }
                    9 -> {
                        overlay.controlsVisible = !overlay.controlsVisible
                        prefs.edit().putBoolean("touchControls", overlay.controlsVisible).apply()
                    }
                    10 -> showOpacityDialog()
                    11 -> pickChoice("Shader", ShaderMode.entries.map { it.name }, renderer.shaderMode.ordinal) {
                        renderer.shaderMode = ShaderMode.entries[it]
                        prefs.edit().putString("shader", renderer.shaderMode.name).apply()
                    }
                    12 -> showCrtOptions()
                    13 -> pickChoice("Aspect ratio", AspectMode.entries.map { it.name }, renderer.aspectMode.ordinal) {
                        renderer.aspectMode = AspectMode.entries[it]
                        prefs.edit().putString("aspect", renderer.aspectMode.name).apply()
                    }
                    14 -> pickChoice(
                        "Render mode",
                        listOf("Fast", "Accurate"),
                        if (accurateRender) 1 else 0
                    ) {
                        accurateRender = it == 1
                        NativeCore.nativeSetAccurate(accurateRender)
                        prefs.edit().putBoolean("accurate", accurateRender).apply()
                    }
                    15 -> pickChoice(
                        "Background",
                        listOf("Black", "V.Smile Blue", "V.Smile Purple"),
                        renderer.backgroundMode.ordinal
                    ) {
                        renderer.backgroundMode = BackgroundMode.entries[it]
                        prefs.edit().putString("background", renderer.backgroundMode.name).apply()
                    }
                    16 -> pickChoice(
                        "Bezel",
                        listOf("None", "Silver", "Black"),
                        renderer.bezelMode.ordinal
                    ) {
                        renderer.bezelMode = BezelMode.entries[it]
                        prefs.edit().putString("bezel", renderer.bezelMode.name).apply()
                    }
                    17 -> startBindingWizard()
                    18 -> showTriggerDialog()
                    19 -> NativeCore.nativeReset()
                    20 -> confirmQuit()
                }
            }
            .setOnDismissListener {
                menuOpen = false
                if (initialized) NativeCore.nativeSetPaused(false)
                hideSystemUi()
            }
            .show()
    }

    /** Radio-list picker that shows the current selection. */
    private fun pickChoice(title: String, items: List<String>, current: Int, f: (Int) -> Unit) {
        AlertDialog.Builder(this)
            .setTitle(title)
            .setSingleChoiceItems(items.toTypedArray(), current) { d, i ->
                f(i)
                d.dismiss()
            }
            .setNegativeButton("Cancel", null)
            .setOnDismissListener { if (!menuOpen && !wizardActive && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    // Slot picker showing each slot's saved-frame thumbnail and time. On Load,
    // empty slots are dimmed and ignored.
    private fun pickSlot(title: String, isSave: Boolean, f: (Int) -> Unit) {
        val d = resources.displayMetrics.density
        val white = android.graphics.Color.WHITE
        val adapter = object : android.widget.BaseAdapter() {
            override fun getCount() = 3
            override fun getItem(p: Int): Any = p
            override fun getItemId(p: Int) = p.toLong()
            override fun getView(pos: Int, convertView: android.view.View?, parent: android.view.ViewGroup): android.view.View {
                val row = android.widget.LinearLayout(this@EmuActivity).apply {
                    orientation = android.widget.LinearLayout.HORIZONTAL
                    gravity = android.view.Gravity.CENTER_VERTICAL
                    setPadding((14 * d).toInt(), (10 * d).toInt(), (14 * d).toInt(), (10 * d).toInt())
                }
                val img = android.widget.ImageView(this@EmuActivity).apply {
                    layoutParams = android.widget.LinearLayout.LayoutParams((80 * d).toInt(), (60 * d).toInt())
                        .apply { rightMargin = (14 * d).toInt() }
                    scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
                }
                val exists = stateFile(pos).exists()
                val thumb = thumbFile(pos)
                if (thumb.exists()) {
                    img.setImageBitmap(android.graphics.BitmapFactory.decodeFile(thumb.absolutePath))
                } else {
                    img.setBackgroundColor(android.graphics.Color.argb(40, 255, 255, 255))
                }
                val col = android.widget.LinearLayout(this@EmuActivity).apply {
                    orientation = android.widget.LinearLayout.VERTICAL
                }
                col.addView(android.widget.TextView(this@EmuActivity).apply {
                    text = "Slot ${pos + 1}"; textSize = 17f; setTextColor(white)
                })
                val sub = if (exists)
                    android.text.format.DateUtils.getRelativeTimeSpanString(stateFile(pos).lastModified()).toString()
                else "Empty"
                col.addView(android.widget.TextView(this@EmuActivity).apply {
                    text = sub; textSize = 12f; alpha = 0.7f; setTextColor(white)
                })
                row.addView(img); row.addView(col)
                row.alpha = if (!isSave && !exists) 0.4f else 1f
                return row
            }
        }
        AlertDialog.Builder(this)
            .setTitle(title)
            .setAdapter(adapter) { _, which ->
                if (isSave || stateFile(which).exists()) f(which)
                else Toast.makeText(this, "Slot ${which + 1} is empty", Toast.LENGTH_SHORT).show()
            }
            .setOnDismissListener { if (!menuOpen && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    private fun showOpacityDialog() {
        val touchOverlay = overlay  // View.getOverlay() shadows the field inside apply{}
        val seek = SeekBar(this).apply {
            max = 100
            progress = (touchOverlay.controlOpacity * 100).toInt()
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, u: Boolean) {
                    touchOverlay.controlOpacity = p / 100f
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}
                override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }
        AlertDialog.Builder(this)
            .setTitle("Controls opacity")
            .setView(seek)
            .setPositiveButton("OK") { _, _ ->
                prefs.edit().putFloat("opacity", overlay.controlOpacity).apply()
            }
            .setOnDismissListener { if (!menuOpen && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    // Sequential binding wizard: walks every action; press a button to bind
    // (stealing it from any other action), Skip to keep, Unassign to clear.
    private var wizardActive = false
    private var wizardIndex = 0
    private var wizardDialog: AlertDialog? = null

    private fun startBindingWizard() {
        wizardActive = true
        wizardIndex = 0
        showWizardStep()
    }

    // ---------------- controls layout editor ----------------

    private fun loadLayouts(): MutableMap<String, MutableMap<String, TouchOverlayView.Placement>> {
        val result = LinkedHashMap<String, MutableMap<String, TouchOverlayView.Placement>>()
        val raw = prefs.getString("layouts", null) ?: return result
        try {
            val root = org.json.JSONObject(raw)
            for (name in root.keys()) {
                val ctls = root.getJSONObject(name)
                val map = HashMap<String, TouchOverlayView.Placement>()
                for (id in ctls.keys()) {
                    val arr = ctls.getJSONArray(id)
                    map[id] = TouchOverlayView.Placement(
                        arr.getDouble(0).toFloat(), arr.getDouble(1).toFloat(), arr.getDouble(2).toFloat()
                    )
                }
                result[name] = map
            }
        } catch (e: Exception) { /* corrupted prefs: start fresh */ }
        return result
    }

    private fun saveLayouts(all: Map<String, Map<String, TouchOverlayView.Placement>>) {
        val root = org.json.JSONObject()
        for ((name, map) in all) {
            val ctls = org.json.JSONObject()
            for ((id, p) in map) {
                ctls.put(id, org.json.JSONArray(listOf(p.cxF.toDouble(), p.cyF.toDouble(), p.scale.toDouble())))
            }
            root.put(name, ctls)
        }
        prefs.edit().putString("layouts", root.toString()).apply()
    }

    private fun applyLayoutByName(name: String) {
        if (name == "Default") {
            overlay.applyLayout(emptyMap())
        } else {
            overlay.applyLayout(loadLayouts()[name] ?: emptyMap())
        }
    }

    private fun enterLayoutEdit() {
        if (layoutEditing) return
        layoutEditing = true
        val active = prefs.getString("activeLayout", "Default")!!
        editingBaseName = if (active == "Default") null else active
        releaseAllInputs()
        NativeCore.nativeSetPaused(true)
        overlay.editMode = true
        showEditPanel()
        Toast.makeText(
            this,
            "Drag buttons to move. Drag empty space to box-select. Game inputs are off.",
            Toast.LENGTH_LONG
        ).show()
    }

    private fun exitLayoutEdit(reapplyActive: Boolean) {
        layoutEditing = false
        overlay.editMode = false
        editPanel?.let { rootLayout.removeView(it) }
        editPanel = null
        scaleBox = null
        if (reapplyActive) applyLayoutByName(prefs.getString("activeLayout", "Default")!!)
        if (initialized && !menuOpen) NativeCore.nativeSetPaused(false)
    }

    private fun showEditPanel() {
        val d = resources.displayMetrics.density
        val panel = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
            setBackgroundColor(android.graphics.Color.argb(210, 30, 30, 40))
            setPadding((8 * d).toInt(), (4 * d).toInt(), (8 * d).toInt(), (4 * d).toInt())
            gravity = android.view.Gravity.CENTER_VERTICAL
        }
        fun btn(label: String, onClick: () -> Unit) = android.widget.Button(this).apply {
            text = label
            isAllCaps = false
            setOnClickListener { onClick() }
        }
        panel.addView(btn("All") { overlay.selectAll() })
        panel.addView(btn("◀") { stepScale(-0.1f) })
        scaleBox = android.widget.EditText(this).apply {
            inputType = android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
            setText("1.0")
            minWidth = (56 * d).toInt()
            gravity = android.view.Gravity.CENTER
            imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { _, _, _ ->
                text.toString().toFloatOrNull()?.let { s -> this@EmuActivity.overlay.setSelectionScale(s) }
                true
            }
        }
        panel.addView(scaleBox)
        panel.addView(btn("▶") { stepScale(+0.1f) })
        panel.addView(btn("Reset") { overlay.resetSelection() })
        panel.addView(btn("Save") { saveLayoutFlow() })
        panel.addView(btn("Close") { confirmDiscardEdit() })
        val lp = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            gravity = android.view.Gravity.TOP or android.view.Gravity.CENTER_HORIZONTAL
            topMargin = (8 * d).toInt()
        }
        rootLayout.addView(panel, lp)
        editPanel = panel
    }

    private fun stepScale(delta: Float) {
        val cur = scaleBox?.text?.toString()?.toFloatOrNull() ?: overlay.selectionScale()
        val next = ((cur + delta) * 10f).toInt() / 10f
        overlay.setSelectionScale(next)
        suppressScaleWatcher = true
        scaleBox?.setText(String.format("%.1f", next.coerceIn(0.4f, 3f)))
        suppressScaleWatcher = false
    }

    private fun saveLayoutFlow() {
        val base = editingBaseName
        if (base == null) {
            promptSaveAs()
        } else {
            AlertDialog.Builder(this)
                .setTitle("Save layout")
                .setItems(arrayOf("Save to \"$base\"", "Save as new layout…")) { _, i ->
                    if (i == 0) persistLayout(base) else promptSaveAs()
                }
                .show()
        }
    }

    private fun promptSaveAs() {
        val input = android.widget.EditText(this).apply {
            hint = "Layout name"
            setText(editingBaseName ?: "")
        }
        AlertDialog.Builder(this)
            .setTitle("Save layout as")
            .setView(input)
            .setPositiveButton("Save") { _, _ ->
                val name = input.text.toString().trim()
                when {
                    name.isEmpty() -> Toast.makeText(this, "Name required", Toast.LENGTH_SHORT).show()
                    name.equals("Default", true) -> Toast.makeText(this, "\"Default\" is protected — pick another name", Toast.LENGTH_LONG).show()
                    else -> persistLayout(name)
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun persistLayout(name: String) {
        val all = loadLayouts()
        all[name] = HashMap(overlay.currentLayout())
        saveLayouts(all)
        prefs.edit().putString("activeLayout", name).apply()
        Toast.makeText(this, "Layout \"$name\" saved & active", Toast.LENGTH_SHORT).show()
        exitLayoutEdit(reapplyActive = false)
    }

    private fun confirmDiscardEdit() {
        AlertDialog.Builder(this)
            .setMessage("Close the layout editor? Unsaved changes are discarded.")
            .setPositiveButton("Close") { _, _ -> exitLayoutEdit(reapplyActive = true) }
            .setNegativeButton("Keep editing", null)
            .show()
    }

    private fun showLayoutsManager() {
        val all = loadLayouts()
        val names = listOf("Default") + all.keys.sorted()
        val active = prefs.getString("activeLayout", "Default")
        val labels = names.map { (if (it == active) "●  " else "    ") + it }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Controls layouts")
            .setItems(labels) { _, i -> showLayoutActions(names[i]) }
            .setOnDismissListener { if (!menuOpen && !layoutEditing && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    private fun showLayoutActions(name: String) {
        val isDefault = name == "Default"
        val actions = if (isDefault) arrayOf("Use", "Edit a copy…")
        else arrayOf("Use", "Edit…", "Rename…", "Delete")
        AlertDialog.Builder(this)
            .setTitle(name)
            .setItems(actions) { _, i ->
                when {
                    i == 0 -> {
                        prefs.edit().putString("activeLayout", name).apply()
                        applyLayoutByName(name)
                        Toast.makeText(this, "Using \"$name\"", Toast.LENGTH_SHORT).show()
                        if (!layoutEditing && initialized && !menuOpen) NativeCore.nativeSetPaused(false)
                    }
                    i == 1 -> {
                        applyLayoutByName(name)
                        prefs.edit().putString("activeLayout", name).apply()
                        enterLayoutEdit()
                    }
                    i == 2 && !isDefault -> promptRename(name)
                    i == 3 && !isDefault -> {
                        val all = loadLayouts()
                        all.remove(name)
                        saveLayouts(all)
                        if (prefs.getString("activeLayout", "Default") == name) {
                            prefs.edit().putString("activeLayout", "Default").apply()
                            applyLayoutByName("Default")
                        }
                        Toast.makeText(this, "Deleted \"$name\"", Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setOnDismissListener { if (!menuOpen && !layoutEditing && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    private fun promptRename(name: String) {
        val input = android.widget.EditText(this).apply { setText(name) }
        AlertDialog.Builder(this)
            .setTitle("Rename layout")
            .setView(input)
            .setPositiveButton("Rename") { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isEmpty() || newName.equals("Default", true)) {
                    Toast.makeText(this, "Invalid name", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                val all = loadLayouts()
                all[name]?.let { all[newName] = it }
                all.remove(name)
                saveLayouts(all)
                if (prefs.getString("activeLayout", "Default") == name) {
                    prefs.edit().putString("activeLayout", newName).apply()
                }
                Toast.makeText(this, "Renamed to \"$newName\"", Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // Per-effect intensity sliders; changes apply live so the paused frame previews them.
    private fun showCrtOptions() {
        data class Fx(val name: String, val key: String, val get: () -> Float, val set: (Float) -> Unit)
        val fx = listOf(
            Fx("Curvature", "crtCurveV", { renderer.crtCurve }, { renderer.crtCurve = it }),
            Fx("Glow", "crtGlowV", { renderer.crtGlow }, { renderer.crtGlow = it }),
            Fx("Scanlines", "crtScanV", { renderer.crtScan }, { renderer.crtScan = it }),
            Fx("Aperture mask", "crtMaskV", { renderer.crtMask }, { renderer.crtMask = it }),
            Fx("Vignette", "crtVigV", { renderer.crtVignette }, { renderer.crtVignette = it }),
        )
        val pad = (resources.displayMetrics.density * 20).toInt()
        val col = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(pad, pad / 2, pad, 0)
        }
        for (e in fx) {
            val label = android.widget.TextView(this)
            fun labelText(v: Int) { label.text = "${e.name}: $v%" }
            labelText((e.get() * 100).toInt())
            val seek = SeekBar(this).apply {
                max = 100
                progress = (e.get() * 100).toInt()
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(sb: SeekBar?, p: Int, u: Boolean) {
                        e.set(p / 100f)
                        labelText(p)
                        prefs.edit().putFloat(e.key, p / 100f).apply()
                    }
                    override fun onStartTrackingTouch(sb: SeekBar?) {}
                    override fun onStopTrackingTouch(sb: SeekBar?) {}
                })
            }
            col.addView(label)
            col.addView(seek)
        }
        val scroll = android.widget.ScrollView(this).apply { addView(col) }
        AlertDialog.Builder(this)
            .setTitle("CRT effects")
            .setView(scroll)
            .setPositiveButton("OK", null)
            .setOnDismissListener { if (!menuOpen && !wizardActive && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    private fun confirmQuit() {
        AlertDialog.Builder(this)
            .setMessage("Quit ${romName.substringBeforeLast('.')}?")
            .setPositiveButton("Quit") { _, _ -> finish() }
            .setNegativeButton("Cancel", null)
            .setOnDismissListener { if (!menuOpen && !wizardActive && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    private fun showTriggerDialog() {
        val seek = SeekBar(this).apply {
            max = 90
            progress = ((mapper.triggerThreshold * 100).toInt() - 5).coerceIn(0, 90)
        }
        AlertDialog.Builder(this)
            .setTitle("Trigger sensitivity (pull depth to register)")
            .setView(seek)
            .setPositiveButton("OK") { _, _ ->
                mapper.triggerThreshold = (seek.progress + 5) / 100f
                Toast.makeText(this, "Trigger threshold: ${seek.progress + 5}%", Toast.LENGTH_SHORT).show()
            }
            .setOnDismissListener { if (!menuOpen && !wizardActive && initialized) NativeCore.nativeSetPaused(false) }
            .show()
    }

    private fun showWizardStep() {
        wizardDialog?.setOnDismissListener(null)
        wizardDialog?.dismiss()
        val actions = Action.entries
        if (wizardIndex >= actions.size) {
            endWizard()
            return
        }
        val action = actions[wizardIndex]
        val key = mapper.bindingFor(action)
        val current = if (key != null) KeyEvent.keyCodeToString(key).removePrefix("KEYCODE_") else "unbound"
        wizardDialog = AlertDialog.Builder(this)
            .setTitle("${wizardIndex + 1}/${actions.size}: ${action.label}")
            .setMessage("Press a controller button to bind\n(currently: $current)")
            .setNeutralButton("Skip") { _, _ -> advanceWizard() }
            .setNegativeButton("Unassign") { _, _ ->
                mapper.unbind(action)
                advanceWizard()
            }
            .setPositiveButton("Done") { _, _ -> endWizard() }
            .setCancelable(false)
            .show()
        // The dialog window receives input before the activity does, so capture
        // happens here. Everything except Back is bindable (incl. d-pad keys);
        // use the touchscreen for Skip/Unassign/Done.
        wizardDialog?.setOnKeyListener { _, keyCode, event ->
            if (keyCode == KeyEvent.KEYCODE_BACK) return@setOnKeyListener false
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                wizardBind(keyCode, action)
            }
            true
        }
        // Triggers/d-pad on many pads (incl. Xbox) are analog axes, not keys.
        wizardDialog?.window?.decorView?.setOnGenericMotionListener { _, ev ->
            if (ev.source and android.view.InputDevice.SOURCE_JOYSTICK ==
                android.view.InputDevice.SOURCE_JOYSTICK) {
                val code = mapper.axisKeyOf(ev)
                if (code != 0 && wizardActive) wizardBind(code, action)
                true
            } else false
        }
    }

    private fun wizardBind(keyCode: Int, action: com.dsmile.emulator.input.Action) {
        mapper.bind(keyCode, action)
        Toast.makeText(
            this,
            "${action.label} → ${KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_")}",
            Toast.LENGTH_SHORT
        ).show()
        advanceWizard()
    }

    private fun advanceWizard() {
        wizardIndex++
        showWizardStep()
    }

    private fun endWizard() {
        captureAction = null
        wizardActive = false
        wizardDialog?.setOnDismissListener(null)
        wizardDialog?.dismiss()
        wizardDialog = null
        Toast.makeText(this, "Bindings saved", Toast.LENGTH_SHORT).show()
        if (initialized) NativeCore.nativeSetPaused(false)
    }

    // ---------------- lifecycle ----------------

    private fun hideSystemUi() {
        val c = WindowInsetsControllerCompat(window, window.decorView)
        c.hide(WindowInsetsCompat.Type.systemBars())
        c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    // Missed release events (dialog/menu/focus steals them) would leave holds
    // like fast forward or rewind stuck on; drop everything on any transition.
    private fun releaseAllInputs() {
        if (!initialized) return
        mapper.releaseAll(this)
        touchButtons = 0
        touchJoyX = 0
        touchJoyY = 0
        NativeCore.nativeSetRewind(false)
        if (!ffOn) NativeCore.nativeSetFastForward(false)
        pushInput()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi() else releaseAllInputs()
    }

    override fun onPause() {
        super.onPause()
        releaseAllInputs()
        if (initialized) NativeCore.nativeSetPaused(true)
        glView.onPause()
    }

    override fun onResume() {
        super.onResume()
        glView.onResume()
        if (initialized && !menuOpen) NativeCore.nativeSetPaused(false)
        hideSystemUi()
    }

    override fun onDestroy() {
        super.onDestroy()
        if (initialized) {
            initialized = false
            NativeCore.nativeStop()
            NativeCore.nativeDestroy()
        }
    }
}
