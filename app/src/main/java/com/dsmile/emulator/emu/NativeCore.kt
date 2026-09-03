package com.dsmile.emulator.emu

object NativeCore {
    init {
        System.loadLibrary("dsmile")
    }

    external fun nativeInit(cart: ByteArray, sysrom: ByteArray?, pal: Boolean, playIntro: Boolean): Boolean
    external fun nativeStart()
    external fun nativeStop()
    external fun nativeDestroy()
    external fun nativeSetPaused(paused: Boolean)
    external fun nativeSetFastForward(ff: Boolean)
    external fun nativeSetFastForwardSpeed(speed: Float)  // 0 = uncapped
    external fun nativeSetAccurate(accurate: Boolean)
    external fun nativeSetRewind(rewind: Boolean)
    external fun nativeSetInput(joyX: Int, joyY: Int, buttons: Int)
    external fun nativeReset()
    external fun nativeGetFrame(buf: java.nio.ByteBuffer): Int
    external fun nativeGetLeds(): Int
    external fun nativeSaveState(): ByteArray?
    external fun nativeLoadState(data: ByteArray): Boolean

    // Button bit assignments shared with the core (VSmileJoy::UpdateInput)
    const val BTN_ENTER = 1
    const val BTN_BACK = 2
    const val BTN_HELP = 4
    const val BTN_ABC = 8
    const val BTN_RED = 16
    const val BTN_YELLOW = 32
    const val BTN_BLUE = 64
    const val BTN_GREEN = 128
}
