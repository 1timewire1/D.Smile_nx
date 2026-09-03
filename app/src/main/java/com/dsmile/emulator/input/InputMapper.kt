package com.dsmile.emulator.input

import android.content.SharedPreferences
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import com.dsmile.emulator.emu.NativeCore
import kotlin.math.abs
import kotlin.math.roundToInt

enum class Action(val label: String, val buttonBit: Int = 0) {
    ENTER("Enter / OK", NativeCore.BTN_ENTER),
    BACK("Exit", NativeCore.BTN_BACK),
    HELP("Help", NativeCore.BTN_HELP),
    ABC("Learning Zone (ABC)", NativeCore.BTN_ABC),
    RED("Red", NativeCore.BTN_RED),
    YELLOW("Yellow", NativeCore.BTN_YELLOW),
    BLUE("Blue", NativeCore.BTN_BLUE),
    GREEN("Green", NativeCore.BTN_GREEN),
    SAVE_STATE("Save State"),
    LOAD_STATE("Load State"),
    FAST_FORWARD("Fast Forward (hold)"),
    REWIND("Rewind (hold)"),
    MENU("Menu"),
}

interface HotkeyListener {
    fun onHotkey(action: Action, pressed: Boolean)
}

// Maps physical gamepad/keyboard input to emulator actions. Remappable, persisted.
class InputMapper(private val prefs: SharedPreferences) {
    private var keyMap = HashMap<Int, Action>()
    /** Axis value a trigger must exceed to count as pressed (0.05-0.95). */
    var triggerThreshold = prefs.getFloat("trigThresh", 0.5f)
        set(v) { field = v; prefs.edit().putFloat("trigThresh", v).apply() }
    private var pressedButtons = 0
    var joyX = 0
        private set
    var joyY = 0
        private set

    init { load() }

    fun buttons(): Int = pressedButtons

    private fun defaults(): HashMap<Int, Action> = hashMapOf(
        KeyEvent.KEYCODE_BUTTON_A to Action.ENTER,
        KeyEvent.KEYCODE_BUTTON_B to Action.BACK,
        KeyEvent.KEYCODE_BUTTON_Y to Action.HELP,
        KeyEvent.KEYCODE_BUTTON_X to Action.ABC,
        KeyEvent.KEYCODE_BUTTON_L1 to Action.GREEN,
        KeyEvent.KEYCODE_BUTTON_R1 to Action.RED,
        KeyEvent.KEYCODE_BUTTON_L2 to Action.YELLOW,
        KeyEvent.KEYCODE_BUTTON_R2 to Action.BLUE,
        KeyEvent.KEYCODE_BUTTON_START to Action.MENU,
        KeyEvent.KEYCODE_BUTTON_THUMBR to Action.FAST_FORWARD,
        KeyEvent.KEYCODE_BUTTON_THUMBL to Action.REWIND,
    )

    fun load() {
        keyMap = defaults()
        prefs.getStringSet("keymap", null)?.let { set ->
            keyMap = HashMap()
            for (entry in set) {
                val parts = entry.split(":")
                if (parts.size == 2) {
                    val code = parts[0].toIntOrNull() ?: continue
                    val action = runCatching { Action.valueOf(parts[1]) }.getOrNull() ?: continue
                    keyMap[code] = action
                }
            }
        }
    }

    fun save() {
        prefs.edit().putStringSet("keymap", keyMap.map { "${it.key}:${it.value.name}" }.toSet()).apply()
    }

    /** Strict 1:1: assigning a key to an action removes the key from any other
     *  action AND removes the action's previous key. */
    fun bind(keyCode: Int, action: Action) {
        keyMap.entries.removeAll { it.value == action }
        keyMap[keyCode] = action
        save()
    }

    fun unbind(action: Action) {
        keyMap.entries.removeAll { it.value == action }
        save()
    }

    fun bindingFor(action: Action): Int? = keyMap.entries.firstOrNull { it.value == action }?.key

    fun resetBindings() {
        keyMap = defaults()
        save()
    }

    /** Returns true if the event was consumed. */
    fun onKey(event: KeyEvent, hotkeys: HotkeyListener): Boolean =
        handleKey(event.keyCode, event.action == KeyEvent.ACTION_DOWN, event.repeatCount, hotkeys)

    // Explicit bindings win; unbound d-pad keys fall back to the joystick.
    private fun handleKey(keyCode: Int, pressed: Boolean, repeat: Int, hotkeys: HotkeyListener): Boolean {
        val action = keyMap[keyCode]
        if (action != null) {
            if (repeat > 0) return true
            if (action.buttonBit != 0) {
                pressedButtons = if (pressed) pressedButtons or action.buttonBit
                else pressedButtons and action.buttonBit.inv()
            } else {
                hotkeys.onHotkey(action, pressed)
            }
            return true
        }
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP -> { joyY = if (pressed) 5 else 0; return true }
            KeyEvent.KEYCODE_DPAD_DOWN -> { joyY = if (pressed) -5 else 0; return true }
            KeyEvent.KEYCODE_DPAD_LEFT -> { joyX = if (pressed) -5 else 0; return true }
            KeyEvent.KEYCODE_DPAD_RIGHT -> { joyX = if (pressed) 5 else 0; return true }
        }
        return false
    }

    // Triggers and hats are analog axes on most gamepads; synthesize key codes
    // so they are bindable and fire bindings like real buttons.
    private val axisKeyState = HashMap<Int, Boolean>()

    private fun synthKey(keyCode: Int, pressed: Boolean, hotkeys: HotkeyListener) {
        if (axisKeyState[keyCode] == pressed) return
        axisKeyState[keyCode] = pressed
        handleKey(keyCode, pressed, 0, hotkeys)
    }

    // Hysteresis: press above the threshold, release only below 60% of it.
    // Prevents a trigger hovering at the threshold from flutter-holding.
    private fun axisHeld(keyCode: Int, value: Float): Boolean {
        val was = axisKeyState[keyCode] == true
        return if (was) value > triggerThreshold * 0.6f else value > triggerThreshold
    }

    /** Force-release everything (menu opened, app paused, focus lost). Missed
     *  release events otherwise leave hold-actions like fast forward stuck. */
    fun releaseAll(hotkeys: HotkeyListener) {
        pressedButtons = 0
        joyX = 0
        joyY = 0
        for ((code, pressed) in axisKeyState) {
            if (pressed) {
                axisKeyState[code] = false
                keyMap[code]?.let { if (it.buttonBit == 0) hotkeys.onHotkey(it, false) }
            }
        }
    }

    /** Extracts the key code an axis gesture represents, or 0. (Used by the binding wizard.) */
    fun axisKeyOf(event: MotionEvent): Int = when {
        event.getAxisValue(MotionEvent.AXIS_LTRIGGER) > triggerThreshold ||
            event.getAxisValue(MotionEvent.AXIS_BRAKE) > triggerThreshold -> KeyEvent.KEYCODE_BUTTON_L2
        event.getAxisValue(MotionEvent.AXIS_RTRIGGER) > triggerThreshold ||
            event.getAxisValue(MotionEvent.AXIS_GAS) > triggerThreshold -> KeyEvent.KEYCODE_BUTTON_R2
        event.getAxisValue(MotionEvent.AXIS_HAT_X) < -0.5f -> KeyEvent.KEYCODE_DPAD_LEFT
        event.getAxisValue(MotionEvent.AXIS_HAT_X) > 0.5f -> KeyEvent.KEYCODE_DPAD_RIGHT
        event.getAxisValue(MotionEvent.AXIS_HAT_Y) < -0.5f -> KeyEvent.KEYCODE_DPAD_UP
        event.getAxisValue(MotionEvent.AXIS_HAT_Y) > 0.5f -> KeyEvent.KEYCODE_DPAD_DOWN
        else -> 0
    }

    // Spring-back suppression: a released stick physically overshoots past
    // center, briefly reading as the opposite direction beyond any sane
    // deadzone. Track the last strong deflection; an opposing reading within
    // the suppression window is spring-back and reads as center. A genuine
    // reversal persists past the window and comes through ~1 frame late.
    private var strongDirX = 0f
    private var strongDirY = 0f
    private var strongTime = 0L

    /** Handles joystick axis motion. Returns true if consumed. */
    fun onMotion(event: MotionEvent, hotkeys: HotkeyListener): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return false
        val x = event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_Y)
        // Radial deadzone + remap: absorbs small drift near center.
        val mag = kotlin.math.hypot(x, y)
        val dz = 0.24f
        if (mag > dz) {
            val now = android.os.SystemClock.uptimeMillis()
            val nx = x / mag
            val ny = y / mag
            val opposesRecent = (now - strongTime) < 90 &&
                (nx * strongDirX + ny * strongDirY) < -0.4f
            if (opposesRecent) {
                joyX = 0
                joyY = 0
            } else {
                val scaled = ((mag - dz) / (1f - dz)).coerceIn(0f, 1f)
                joyX = (x / mag * scaled * 5f).roundToInt().coerceIn(-5, 5)
                joyY = (-y / mag * scaled * 5f).roundToInt().coerceIn(-5, 5)
                if (mag > 0.6f) {
                    strongDirX = nx
                    strongDirY = ny
                    strongTime = now
                }
            }
        } else {
            joyX = 0
            joyY = 0
        }

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        synthKey(KeyEvent.KEYCODE_DPAD_LEFT, hatX < -0.5f, hotkeys)
        synthKey(KeyEvent.KEYCODE_DPAD_RIGHT, hatX > 0.5f, hotkeys)
        synthKey(KeyEvent.KEYCODE_DPAD_UP, hatY < -0.5f, hotkeys)
        synthKey(KeyEvent.KEYCODE_DPAD_DOWN, hatY > 0.5f, hotkeys)
        val lt = maxOf(
            event.getAxisValue(MotionEvent.AXIS_LTRIGGER),
            event.getAxisValue(MotionEvent.AXIS_BRAKE)
        )
        val rt = maxOf(
            event.getAxisValue(MotionEvent.AXIS_RTRIGGER),
            event.getAxisValue(MotionEvent.AXIS_GAS)
        )
        synthKey(KeyEvent.KEYCODE_BUTTON_L2, axisHeld(KeyEvent.KEYCODE_BUTTON_L2, lt), hotkeys)
        synthKey(KeyEvent.KEYCODE_BUTTON_R2, axisHeld(KeyEvent.KEYCODE_BUTTON_R2, rt), hotkeys)
        return true
    }
}
