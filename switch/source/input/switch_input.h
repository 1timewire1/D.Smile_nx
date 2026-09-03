#pragma once
#include <cstdint>

// Polls the Switch pad and translates it into V.Smile joystick input: an
// x/y pair in -5..5 (VSmileJoy::UpdateInput's convention) and an 8-bit
// button mask matching NativeCore.BTN_* / VSmileJoy::UpdateInput's bit
// layout (bit0 enter, bit1 back, bit2 help, bit3 abc, bit4 red, bit5
// yellow, bit6 blue, bit7 green). Mirrors the Android app's default
// gamepad mapping (see InputMapper.kt's defaults()) so muscle memory
// carries over: A=Enter, B=Back, Y=Help, X=ABC, L=Green, R=Red, ZL=Yellow,
// ZR=Blue, D-Pad/left stick=joystick.
struct SwitchInputState {
  int16_t joy_x = 0;
  int16_t joy_y = 0;
  uint32_t buttons = 0;
  uint64_t held = 0;   // raw HidNpadButton_* bits, for chord checks (e.g. quit combo)
  uint64_t down = 0;   // raw HidNpadButton_* bits pressed this frame
  uint64_t up = 0;     // raw HidNpadButton_* bits released this frame - Fast Forward's
                       // hold-hotkey needs the release edge specifically (see main.cpp),
                       // to match Android's onHotkey(pressed) press/release semantics
                       // exactly rather than just reflecting the current held level.
};

void switch_input_init();
SwitchInputState switch_input_poll();

// Player 2 - fully automatic, no setting (a second controller being
// connected *is* two-player mode, checked live every frame - see
// switch/README.md's "Two-player controller support" section). Same input
// shape/mapping as player 1 (both physical V.Smile pads are identical, so
// both players share one action_binding table), read from a second,
// separately-initialized PadState (HidNpadIdType_No2, no Handheld merge -
// handheld mode is inherently single-controller). Safe to call even with
// nothing connected to that slot: libnx just reports no buttons held, so
// player 2's VSmileJoy port naturally behaves like an empty, unprobed
// controller port - same as real hardware with no second pad plugged in,
// no special-casing needed here.
//
// This is the only place s_pad2 gets padUpdate()'d - call it once per
// frame and use switch_input_player2_ready() afterward, not the other way
// around (that function doesn't call padUpdate() itself, to avoid eating
// s_pad2's down/up edges by updating it twice a frame).
SwitchInputState switch_input_poll_p2();

// Whether a second controller is currently connected - reads whatever
// switch_input_poll_p2() last saw, so call that first each frame. Doesn't
// call padUpdate() itself (see that function's own comment).
//
// Deliberately doesn't try to *summon* a second controller itself -
// in-app use of the system controller-pairing applet
// (hidLaShowControllerSupport()) was tried first and reliably failed on
// hardware (LibnxError_LibAppletBadExit, most likely because the applet
// won't run from handheld mode unless a flag this port didn't set is
// passed - see switch/README.md's "Two-player controller support" note on
// this). reference/NetherSX2_nx-main takes the same approach this settled
// on instead: just poll HidNpadIdType_No2 directly and let the player pair
// a second controller however they like (the system's own HOME-menu
// overlay, most simply) - no in-app applet call needed at all.
bool switch_input_player2_ready();
