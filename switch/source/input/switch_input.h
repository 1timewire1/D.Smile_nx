#pragma once
#include <cstdint>
#include <utility>
#include <vector>

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
// shape as player 1, but mapped through player 2's own p2_binding table
// (Settings > Controller > Player 2), read from a second,
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

// A USB keyboard as the V.Smile Smart Keyboard (Settings > Controller >
// Controller Type). Keys map by physical position (USB reports positions,
// not characters), so an AZERTY/QWERTZ keyboard lines up with the French/
// German Smart Keyboard the same way a QWERTY one does with the US model.
// Extra keys: Enter = OK, Esc = Quit, F1 = Help, F2/F3 = Player 1/2,
// F4 = Symbol, F5-F8 = Red/Yellow/Blue/Green, F9 = ABC, Delete = Erase,
// Tab = Typing Time.
struct SwitchKeyboardInput {
  // Smart Keyboard key code changes since the last call, in send order
  // (Shift presses first, Shift releases last): {code, pressed}.
  std::vector<std::pair<uint8_t, bool>> events;
  // Held OK/Quit/Help/ABC/colours, same bit layout as SwitchInputState::buttons.
  uint32_t buttons = 0;
};
// Call once per frame while the keyboard is in use; it diffs against the
// previous call.
SwitchKeyboardInput switch_input_poll_keyboard();

// Player 1's controller accelerometer as V.Smile Motion controller readings
// (Settings > Controller > Controller Type = Motion Controller): one byte per
// axis, 0x80 = 0g, 0x40 per g (see VSmileJoy::UpdateMotion). x = tilt
// left/right (roll), y = tilt forward/back, z = up/down; level and still
// reads about 0x80, 0x80, 0xC0. Works in handheld mode, with a Pro
// Controller, or with attached Joy-Cons (the left one's sensor).
struct SwitchMotionInput {
  int x = 0x80, y = 0x80, z = 0xC0;
};
SwitchMotionInput switch_input_poll_motion();
// The same for player 2's controller (a Pro Controller or Joy-Con pair on
// controller slot 2). Call after switch_input_poll_p2() in the same frame.
SwitchMotionInput switch_input_poll_motion_p2();
