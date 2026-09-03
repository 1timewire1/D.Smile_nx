package com.dsmile.emulator.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PathMeasure
import android.graphics.RectF
import android.view.MotionEvent
import android.view.View
import com.dsmile.emulator.emu.NativeCore
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * On-screen V.Smile controller: joystick, Enter dial, 4 color buttons,
 * Help/Exit/ABC. Toggleable, adjustable opacity, multi-touch, themeable
 * (classic orange / pink console), and fully layout-editable.
 */
class TouchOverlayView(context: Context) : View(context) {

    interface Listener {
        fun onTouchInput(joyX: Int, joyY: Int, buttons: Int)
        fun onMenuRequested()
        fun onEditSelectionChanged(count: Int, scale: Float)
        fun onEditLayoutDirty()
    }

    // Normalized placement of one control: center as fraction of view size,
    // plus a size multiplier relative to the default radius.
    data class Placement(var cxF: Float, var cyF: Float, var scale: Float)

    var listener: Listener? = null
    var controlOpacity = 0.55f
        set(v) { field = v; invalidate() }
    var controlsVisible = true
        set(v) { field = v; clearTouchState(); pushState(); invalidate() }
    var leds = 0
        set(v) { if (field != v) { field = v; invalidate() } }
    var pinkTheme = false
        set(v) { field = v; invalidate() }
    var joySwap = false
        set(v) { field = v; invalidate() }
    var editMode = false
        set(v) {
            field = v
            clearTouchState()
            selection.clear()
            bandRect = null
            pushState()
            notifySelection()
            invalidate()
        }

    private class Ctl(val id: String, val bit: Int) {
        var cx = 0f; var cy = 0f; var r = 0f
        var defCx = 0f; var defCy = 0f; var defR = 0f
    }

    private val editable = listOf(
        Ctl("joystick", 0),
        Ctl("red", NativeCore.BTN_RED),
        Ctl("yellow", NativeCore.BTN_YELLOW),
        Ctl("blue", NativeCore.BTN_BLUE),
        Ctl("green", NativeCore.BTN_GREEN),
        Ctl("enter", NativeCore.BTN_ENTER),
        Ctl("help", NativeCore.BTN_HELP),
        Ctl("exit", NativeCore.BTN_BACK),
        Ctl("abc", NativeCore.BTN_ABC),
    )
    private val menuCtl = Ctl("menu", 0)
    private fun ctl(id: String) = editable.first { it.id == id }

    // Active layout overrides (empty = pure default layout).
    private var overrides = HashMap<String, Placement>()

    fun applyLayout(layout: Map<String, Placement>) {
        overrides = HashMap(layout.mapValues { Placement(it.value.cxF, it.value.cyF, it.value.scale) })
        relayout()
        invalidate()
    }

    fun currentLayout(): Map<String, Placement> =
        overrides.mapValues { Placement(it.value.cxF, it.value.cyF, it.value.scale) }

    // ---------------- layout ----------------

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val u = min(w, h) / 100f
        fun def(id: String, cx: Float, cy: Float, r: Float) {
            ctl(id).apply { defCx = cx; defCy = cy; defR = r }
        }
        def("joystick", 22f * u, h - 24f * u, 16f * u)
        val bx = w - 21f * u
        val by = h - 60f * u
        // 6.3u buttons at 11u spread: silver bezels (1.16r) clear each other
        def("red", bx, by - 11f * u, 6.3f * u)
        def("yellow", bx + 11f * u, by, 6.3f * u)
        def("green", bx, by + 11f * u, 6.3f * u)
        def("blue", bx - 11f * u, by, 6.3f * u)
        def("enter", bx, h - 24f * u, 15.3f * u)
        def("help", 12f * u, h - 80f * u, 5f * u)
        def("abc", 12f * u, h - 66f * u, 5f * u)
        def("exit", 12f * u, h - 52f * u, 5f * u)
        menuCtl.apply { cx = w - 8f * u; cy = 6f * u; r = 5f * u }
        relayout()
    }

    private fun relayout() {
        if (width == 0 || height == 0) return
        for (c in editable) {
            val o = overrides[c.id]
            if (o != null) {
                c.cx = o.cxF * width
                c.cy = o.cyF * height
                c.r = c.defR * o.scale
            } else {
                c.cx = c.defCx
                c.cy = c.defCy
                c.r = c.defR
            }
        }
    }

    // ---------------- edit mode API ----------------

    val selection = LinkedHashSet<String>()

    fun selectAll() {
        selection.clear()
        selection.addAll(editable.map { it.id })
        notifySelection()
        invalidate()
    }

    fun selectionScale(): Float {
        val first = selection.firstOrNull() ?: return 1f
        return overrides[first]?.scale ?: 1f
    }

    fun setSelectionScale(scale: Float) {
        val s = scale.coerceIn(0.4f, 3f)
        for (id in selection) ensureOverride(id).scale = s
        relayout()
        listener?.onEditLayoutDirty()
        invalidate()
    }

    fun resetSelection() {
        for (id in selection) overrides.remove(id)
        relayout()
        listener?.onEditLayoutDirty()
        notifySelection()
        invalidate()
    }

    private fun ensureOverride(id: String): Placement {
        return overrides.getOrPut(id) {
            val c = ctl(id)
            Placement(c.cx / width, c.cy / height, c.r / c.defR)
        }
    }

    private fun notifySelection() {
        listener?.onEditSelectionChanged(selection.size, selectionScale())
    }

    // ---------------- theme ----------------

    private fun bodyPurple() = if (pinkTheme) Color.rgb(150, 84, 190) else Color.rgb(106, 79, 163)
    private fun dialColor() = if (pinkTheme) Color.rgb(246, 146, 182) else Color.rgb(247, 148, 29)
    private fun dialText() = if (pinkTheme) Color.rgb(148, 44, 130) else Color.rgb(255, 224, 102)
    private fun dishColor() = if (pinkTheme) Color.rgb(246, 146, 182) else Color.rgb(247, 148, 29)
    private fun dishInner() = if (pinkTheme) Color.rgb(228, 116, 158) else Color.rgb(200, 110, 20)
    private fun ballColor() = if (pinkTheme) Color.rgb(156, 79, 192) else Color.rgb(123, 111, 208)

    // ---------------- drawing ----------------

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val selPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.WHITE
        pathEffect = DashPathEffect(floatArrayOf(12f, 8f), 0f)
    }

    override fun onDraw(canvas: Canvas) {
        val a = if (editMode) 235 else (controlOpacity * 255).toInt()
        if (controlsVisible || editMode) {
            drawJoystick(canvas, a)
            for (c in editable) {
                if (c.id == "joystick") continue
                drawControl(canvas, c, a)
            }
        }
        if (!editMode) {
            paint.color = Color.rgb(60, 60, 60)
            paint.alpha = 120
            canvas.drawCircle(menuCtl.cx, menuCtl.cy, menuCtl.r, paint)
            textPaint.textSize = menuCtl.r
            textPaint.color = Color.WHITE
            textPaint.alpha = 255
            canvas.drawText("☰", menuCtl.cx, menuCtl.cy + menuCtl.r * 0.35f, textPaint)
        } else {
            for (id in selection) {
                val c = ctl(id)
                selPaint.strokeWidth = 4f
                canvas.drawCircle(c.cx, c.cy, c.r * 1.18f + 6f, selPaint)
            }
            bandRect?.let {
                paint.color = Color.argb(40, 255, 255, 255)
                canvas.drawRect(it, paint)
                selPaint.strokeWidth = 2f
                canvas.drawRect(it, selPaint)
            }
        }
    }

    // Brushed-silver vertical gradient matching the on-screen TV bezel shading:
    // bright top with a specular band, falling to a darker base, cool tint.
    private fun silverShader(cx: Float, cy: Float, r: Float) = android.graphics.LinearGradient(
        cx, cy - r, cx, cy + r,
        intArrayOf(0xFFEDF0F5.toInt(), 0xFFF7F9FD.toInt(), 0xFFBFC5CF.toInt(), 0xFF858B96.toInt()),
        floatArrayOf(0f, 0.20f, 0.55f, 1f),
        android.graphics.Shader.TileMode.CLAMP
    )

    private fun darken(color: Int): Int = Color.rgb(
        (Color.red(color) * 0.80f).toInt(), (Color.green(color) * 0.80f).toInt(),
        (Color.blue(color) * 0.80f).toInt()
    )

    private fun lighten(color: Int, frac: Float): Int = Color.rgb(
        (Color.red(color) + (255 - Color.red(color)) * frac).toInt(),
        (Color.green(color) + (255 - Color.green(color)) * frac).toInt(),
        (Color.blue(color) + (255 - Color.blue(color)) * frac).toInt()
    )

    private fun drawJoystick(canvas: Canvas, a: Int) {
        val j = ctl("joystick")
        val dish = if (joySwap) ballColor() else dishColor()
        val ball = if (joySwap) dishColor() else ballColor()
        paint.color = dish; paint.alpha = a
        canvas.drawCircle(j.cx, j.cy, j.r, paint)
        paint.color = darken(dish); paint.alpha = (a * 0.6f).toInt()
        canvas.drawCircle(j.cx, j.cy, j.r * 0.62f, paint)
        paint.color = ball; paint.alpha = a
        canvas.drawCircle(j.cx + joyDx * j.r * 0.55f, j.cy + joyDy * j.r * 0.55f, j.r * 0.42f, paint)
    }

    private fun drawControl(canvas: Canvas, c: Ctl, a: Int) {
        val held = buttonPointers.values.any { it === c }
        val alpha = if (held) 255 else a
        when (c.id) {
            "red", "yellow", "blue", "green" -> {
                val base = when (c.id) {
                    "red" -> Color.rgb(224, 58, 47)
                    "yellow" -> Color.rgb(245, 197, 24)
                    "blue" -> Color.rgb(46, 95, 208)
                    else -> Color.rgb(63, 165, 60)
                }
                val ledBit = when (c.id) {
                    "green" -> 1; "blue" -> 2; "yellow" -> 4; else -> 8
                }
                val lit = (leds and ledBit) != 0
                // Soft coloured glow halo when the game lights this button.
                if (lit) {
                    val gr = lighten(base, 0.18f)  // glow stays in the button's hue
                    paint.shader = android.graphics.RadialGradient(
                        c.cx, c.cy, c.r * 2.05f,
                        intArrayOf(
                            Color.argb(210, Color.red(gr), Color.green(gr), Color.blue(gr)),
                            Color.argb(110, Color.red(base), Color.green(base), Color.blue(base)),
                            Color.argb(0, Color.red(base), Color.green(base), Color.blue(base))
                        ),
                        floatArrayOf(0.26f, 0.58f, 1f),
                        android.graphics.Shader.TileMode.CLAMP
                    )
                    paint.alpha = 255
                    canvas.drawCircle(c.cx, c.cy, c.r * 2.05f, paint)
                    paint.shader = null
                }
                // silver bezel ring (same shading as the screen bezel)
                paint.shader = silverShader(c.cx, c.cy, c.r * 1.16f)
                paint.alpha = alpha
                canvas.drawCircle(c.cx, c.cy, c.r * 1.16f, paint)
                paint.shader = null
                // button face — glows in its own colour when lit (not toward white)
                paint.color = if (lit) lighten(base, 0.22f) else base
                paint.alpha = if (lit) 255 else alpha
                canvas.drawCircle(c.cx, c.cy, c.r, paint)
                if (lit) {  // brighter inner core, still clearly the button colour
                    paint.color = lighten(base, 0.38f); paint.alpha = 190
                    canvas.drawCircle(c.cx, c.cy, c.r * 0.58f, paint)
                }
                // glossy dome highlight
                paint.color = Color.WHITE; paint.alpha = (alpha * 0.35f).toInt()
                canvas.drawCircle(c.cx - c.r * 0.3f, c.cy - c.r * 0.35f, c.r * 0.3f, paint)
            }
            "enter" -> drawEnter(canvas, c, alpha)
            "help" -> {
                paint.color = bodyPurple(); paint.alpha = alpha
                canvas.drawCircle(c.cx, c.cy, c.r, paint)
                textPaint.textSize = c.r * 1.0f
                textPaint.color = Color.WHITE; textPaint.alpha = 255
                textPaint.isFakeBoldText = true
                canvas.drawText("?", c.cx, c.cy + c.r * 0.36f, textPaint)
                textPaint.isFakeBoldText = false
            }
            "exit" -> drawExit(canvas, c, alpha)
            "abc" -> {
                // dense pill with a thick outline, like the real ABC key
                val pill = RectF(c.cx - c.r * 1.12f, c.cy - c.r * 0.60f, c.cx + c.r * 1.12f, c.cy + c.r * 0.60f)
                paint.color = bodyPurple(); paint.alpha = alpha
                canvas.drawRoundRect(pill, c.r * 0.60f, c.r * 0.60f, paint)
                stroke.color = Color.WHITE; stroke.alpha = alpha
                stroke.strokeWidth = c.r * 0.22f
                canvas.drawRoundRect(pill, c.r * 0.60f, c.r * 0.60f, stroke)
                textPaint.textSize = c.r * 0.80f
                textPaint.color = Color.WHITE; textPaint.alpha = 255
                textPaint.typeface = android.graphics.Typeface.DEFAULT_BOLD
                textPaint.isFakeBoldText = true
                canvas.drawText("ABC", c.cx, c.cy + c.r * 0.28f, textPaint)
                textPaint.isFakeBoldText = false
                textPaint.typeface = android.graphics.Typeface.DEFAULT
            }
        }
    }

    private fun drawEnter(canvas: Canvas, c: Ctl, alpha: Int) {
        // silver ring (screen-bezel shading), colored dial, check + curved ENTER
        paint.shader = silverShader(c.cx, c.cy, c.r)
        paint.alpha = alpha
        canvas.drawCircle(c.cx, c.cy, c.r, paint)
        paint.shader = null
        paint.color = dialColor(); paint.alpha = alpha
        canvas.drawCircle(c.cx, c.cy, c.r * 0.86f, paint)

        val ink = dialText()
        // Sweeping check, user-approved geometry (see test/out/check_preview.ps1):
        // tilted chisel cut, both legs bowing with the sweep, long stroke
        // tapering to a curved point, optically centered.
        fun X(v: Float) = c.cx + c.r * v
        fun Y(v: Float) = c.cy + c.r * v
        val check = Path().apply {
            moveTo(X(-0.196f), Y(-0.035f))                        // chisel inner corner
            quadTo(X(-0.129f), Y(0.004f), X(-0.089f), Y(0.066f))  // short leg inner edge
            quadTo(X(0.137f), Y(-0.144f), X(0.356f), Y(-0.211f))  // long upper edge to tip
            quadTo(X(0.135f), Y(-0.098f), X(-0.097f), Y(0.211f))  // long lower edge to vee
            quadTo(X(-0.162f), Y(0.109f), X(-0.256f), Y(0.024f))  // short leg outer edge
            close()
        }
        paint.color = ink; paint.alpha = 255
        canvas.drawPath(check, paint)
        // "ENTER" along the bottom rim: each glyph positioned and rotated by
        // plain trigonometry - no path APIs, so spacing is exact everywhere.
        val arcR = c.r * 0.70f
        textPaint.textSize = c.r * 0.26f
        textPaint.color = ink; textPaint.alpha = 255
        textPaint.isFakeBoldText = true
        val word = "ENTER"
        val startA = 160f
        val sweep = -140f
        for (i in word.indices) {
            val deg = startA + sweep * (i + 1) / (word.length + 1)
            val rad = Math.toRadians(deg.toDouble())
            val px = c.cx + arcR * kotlin.math.cos(rad).toFloat()
            val py = c.cy + arcR * kotlin.math.sin(rad).toFloat()
            canvas.save()
            canvas.rotate(deg - 90f, px, py)
            canvas.drawText(word[i].toString(), px, py, textPaint)
            canvas.restore()
        }
        textPaint.isFakeBoldText = false
    }

    private fun drawExit(canvas: Canvas, c: Ctl, alpha: Int) {
        paint.color = bodyPurple(); paint.alpha = alpha
        canvas.drawCircle(c.cx, c.cy, c.r, paint)
        // doorway (outline) with the door leaf swung open to the RIGHT,
        // overlapping the opening; its right edge sags down, leaving slivers
        // of the doorway visible along the top and right
        val fL = c.cx - c.r * 0.34f
        val fR = c.cx + c.r * 0.34f
        val fT = c.cy - c.r * 0.46f
        val fB = c.cy + c.r * 0.46f
        stroke.color = Color.WHITE; stroke.alpha = 255
        stroke.strokeWidth = c.r * 0.10f
        canvas.drawRoundRect(RectF(fL, fT, fR, fB), c.r * 0.05f, c.r * 0.05f, stroke)
        val leaf = Path().apply {
            moveTo(fL, fT + c.r * 0.04f)               // hinge top (left jamb)
            lineTo(fR - c.r * 0.20f, fT + c.r * 0.16f) // right edge, dropped
            lineTo(fR - c.r * 0.20f, fB + c.r * 0.20f) // bottom-right, well below frame
            lineTo(fL, fB)                             // hinge bottom
            close()
        }
        paint.color = Color.WHITE; paint.alpha = 240
        canvas.drawPath(leaf, paint)
        paint.color = bodyPurple(); paint.alpha = 255
        canvas.drawCircle(fR - c.r * 0.32f, c.cy + c.r * 0.08f, c.r * 0.065f, paint)
    }

    // ---------------- touch ----------------

    private var joyPointer = -1
    private var joyDx = 0f
    private var joyDy = 0f
    private val buttonPointers = HashMap<Int, Ctl>()
    private var heldBits = 0

    // edit gestures
    private var bandRect: RectF? = null
    private var bandStartX = 0f
    private var bandStartY = 0f
    private var dragIds: List<String> = emptyList()
    private var lastX = 0f
    private var lastY = 0f
    private var editDragging = false

    private fun clearTouchState() {
        joyPointer = -1; joyDx = 0f; joyDy = 0f
        buttonPointers.clear(); heldBits = 0
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (editMode) return editTouch(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val idx = event.actionIndex
                val id = event.getPointerId(idx)
                val x = event.getX(idx)
                val y = event.getY(idx)
                if (hypot(x - menuCtl.cx, y - menuCtl.cy) <= menuCtl.r * 1.6f) {
                    listener?.onMenuRequested()
                    return true
                }
                if (!controlsVisible) return false
                val joy = ctl("joystick")
                if (hypot(x - joy.cx, y - joy.cy) <= joy.r * 1.4f && joyPointer < 0) {
                    joyPointer = id
                    updateJoy(x, y)
                    return true
                }
                val hit = editable.firstOrNull {
                    it.id != "joystick" && hypot(x - it.cx, y - it.cy) <= it.r * 1.35f
                }
                if (hit != null) {
                    buttonPointers[id] = hit
                    heldBits = heldBits or hit.bit
                    pushState()
                    invalidate()
                    return true
                }
                return false
            }
            MotionEvent.ACTION_MOVE -> {
                if (!controlsVisible) return false
                var handled = false
                for (i in 0 until event.pointerCount) {
                    if (event.getPointerId(i) == joyPointer) {
                        updateJoy(event.getX(i), event.getY(i))
                        handled = true
                    }
                }
                return handled || buttonPointers.isNotEmpty()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                val id = event.getPointerId(event.actionIndex)
                var handled = false
                if (id == joyPointer) {
                    joyPointer = -1; joyDx = 0f; joyDy = 0f
                    pushState(); invalidate()
                    handled = true
                }
                buttonPointers.remove(id)?.let {
                    heldBits = buttonPointers.values.fold(0) { acc, b -> acc or b.bit }
                    pushState(); invalidate()
                    handled = true
                }
                if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
                    clearTouchState()
                    pushState(); invalidate()
                }
                return handled
            }
        }
        return false
    }

    private fun editTouch(event: MotionEvent): Boolean {
        val x = event.x
        val y = event.y
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                editDragging = false
                lastX = x; lastY = y
                val hit = editable.firstOrNull { hypot(x - it.cx, y - it.cy) <= max(it.r * 1.25f, 40f) }
                if (hit != null) {
                    // dragging a selected control moves the whole selection;
                    // an unselected one becomes the sole selection
                    if (hit.id !in selection) {
                        selection.clear()
                        selection.add(hit.id)
                        notifySelection()
                    }
                    dragIds = selection.toList()
                    bandRect = null
                } else {
                    dragIds = emptyList()
                    bandStartX = x; bandStartY = y
                    bandRect = RectF(x, y, x, y)
                }
                invalidate()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = x - lastX
                val dy = y - lastY
                if (!editDragging && hypot(dx, dy) > 6f) editDragging = true
                if (dragIds.isNotEmpty()) {
                    for (id in dragIds) {
                        val c = ctl(id)
                        val o = ensureOverride(id)
                        o.cxF = ((c.cx + dx).coerceIn(c.r, width - c.r)) / width
                        o.cyF = ((c.cy + dy).coerceIn(c.r, height - c.r)) / height
                    }
                    relayout()
                    listener?.onEditLayoutDirty()
                } else {
                    bandRect?.set(
                        min(bandStartX, x), min(bandStartY, y),
                        max(bandStartX, x), max(bandStartY, y)
                    )
                }
                lastX = x; lastY = y
                invalidate()
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                bandRect?.let { band ->
                    if (editDragging) {
                        selection.clear()
                        for (c in editable) if (band.contains(c.cx, c.cy)) selection.add(c.id)
                    } else {
                        selection.clear()  // plain tap on empty space deselects
                    }
                    notifySelection()
                }
                bandRect = null
                dragIds = emptyList()
                invalidate()
                return true
            }
        }
        return true
    }

    private fun updateJoy(x: Float, y: Float) {
        val j = ctl("joystick")
        var dx = (x - j.cx) / j.r
        var dy = (y - j.cy) / j.r
        val len = hypot(dx, dy)
        if (len > 1f) { dx /= len; dy /= len }
        joyDx = dx; joyDy = dy
        pushState()
        invalidate()
    }

    private fun pushState() {
        if (editMode) {
            listener?.onTouchInput(0, 0, 0)
            return
        }
        // Center deadzone + remap so the neutral zone is clean and there's no
        // jump at its edge (stops jitter and opposite blips as you return).
        val mag = hypot(joyDx, joyDy)
        var jx = 0
        var jy = 0
        val dz = 0.30f
        if (mag > dz) {
            val scaled = ((mag - dz) / (1f - dz)).coerceIn(0f, 1f)
            jx = (joyDx / mag * scaled * 5f).roundToInt().coerceIn(-5, 5)
            jy = (-joyDy / mag * scaled * 5f).roundToInt().coerceIn(-5, 5)
        }
        listener?.onTouchInput(jx, jy, heldBits)
    }
}
