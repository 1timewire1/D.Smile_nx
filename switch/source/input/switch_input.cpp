#include "switch_input.h"

#include <switch.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/switch_settings.h"

namespace {

constexpr uint32_t BTN_ENTER = 1;
constexpr uint32_t BTN_BACK = 2;
constexpr uint32_t BTN_HELP = 4;
constexpr uint32_t BTN_ABC = 8;
constexpr uint32_t BTN_RED = 16;
constexpr uint32_t BTN_YELLOW = 32;
constexpr uint32_t BTN_BLUE = 64;
constexpr uint32_t BTN_GREEN = 128;

// Index-aligned with GameAction's first 8 values (the actual V.Smile
// controller buttons - the 5 hotkeys after them aren't wired to anything
// yet, see switch_settings.h's GameAction doc comment).
constexpr GameAction kVsmileActions[] = {GameAction::Enter, GameAction::Back,  GameAction::Help,
                                          GameAction::Abc,   GameAction::Red,   GameAction::Yellow,
                                          GameAction::Blue,  GameAction::Green};
constexpr uint32_t kVsmileBits[] = {BTN_ENTER, BTN_BACK, BTN_HELP, BTN_ABC,
                                     BTN_RED,   BTN_YELLOW, BTN_BLUE, BTN_GREEN};

PadState s_pad;
PadState s_pad2;

// USB HID key -> Smart Keyboard key code, by physical position. Codes are
// from the vtech.pulkomandy.tk wiki's key table (captured from a French
// keyboard) and MAME's vsmile keyboard.cpp matrix (US/FR/DE), which agree.
struct KeyMap {
  HidKeyboardKey usb;
  uint8_t code;
};
constexpr uint8_t kKbShift = 0xA9;
constexpr KeyMap kKeyMap[] = {
    // Row 1: digits, the key right of 0, Backspace
    {HidKeyboardKey_D1, 0x33}, {HidKeyboardKey_D2, 0x34}, {HidKeyboardKey_D3, 0x35},
    {HidKeyboardKey_D4, 0x37}, {HidKeyboardKey_D5, 0x36}, {HidKeyboardKey_D6, 0x30},
    {HidKeyboardKey_D7, 0x31}, {HidKeyboardKey_D8, 0x3E}, {HidKeyboardKey_D9, 0x3F},
    {HidKeyboardKey_D0, 0x38}, {HidKeyboardKey_Minus, 0x29}, {HidKeyboardKey_Backspace, 0x39},
    // Row 2: Typing Time, QWERTYUIOP, the key right of P, Erase
    {HidKeyboardKey_Tab, 0x22}, {HidKeyboardKey_Q, 0x23}, {HidKeyboardKey_W, 0x24},
    {HidKeyboardKey_E, 0x25}, {HidKeyboardKey_R, 0x27}, {HidKeyboardKey_T, 0x26},
    {HidKeyboardKey_Y, 0x20}, {HidKeyboardKey_U, 0x21}, {HidKeyboardKey_I, 0x3A},
    {HidKeyboardKey_O, 0x3B}, {HidKeyboardKey_P, 0x3C}, {HidKeyboardKey_OpenBracket, 0x2A},
    {HidKeyboardKey_Slash, 0x2A},  // the US model's / ? key sits right of P
    {HidKeyboardKey_CloseBracket, 0x3D}, {HidKeyboardKey_Delete, 0x3D},
    // Row 3: Caps, ASDFGHJKL, the key right of L
    {HidKeyboardKey_CapsLock, 0x1A}, {HidKeyboardKey_A, 0x1B}, {HidKeyboardKey_S, 0x1C},
    {HidKeyboardKey_D, 0x1D}, {HidKeyboardKey_F, 0x1F}, {HidKeyboardKey_G, 0x1E},
    {HidKeyboardKey_H, 0x18}, {HidKeyboardKey_J, 0x19}, {HidKeyboardKey_K, 0x0A},
    {HidKeyboardKey_L, 0x0B}, {HidKeyboardKey_Semicolon, 0x01},
    // Row 4: Shift, ZXCVBNM and the next two keys, Up
    {HidKeyboardKey_LeftShift, kKbShift}, {HidKeyboardKey_RightShift, kKbShift},
    {HidKeyboardKey_Z, 0x13}, {HidKeyboardKey_X, 0x14}, {HidKeyboardKey_C, 0x15},
    {HidKeyboardKey_V, 0x17}, {HidKeyboardKey_B, 0x16}, {HidKeyboardKey_N, 0x08},
    {HidKeyboardKey_M, 0x11}, {HidKeyboardKey_Comma, 0x0C}, {HidKeyboardKey_Period, 0x2F},
    {HidKeyboardKey_UpArrow, 0x12},
    // Row 5: Player 1, Symbol, Space, Player 2, Left/Down/Right
    {HidKeyboardKey_F2, 0x04}, {HidKeyboardKey_NumPad1, 0x04},
    {HidKeyboardKey_F4, 0x2C}, {HidKeyboardKey_NumPadAdd, 0x2C},
    {HidKeyboardKey_Space, 0x05},
    {HidKeyboardKey_F3, 0x0E}, {HidKeyboardKey_NumPad2, 0x0E},
    {HidKeyboardKey_LeftArrow, 0x06}, {HidKeyboardKey_DownArrow, 0x0F},
    {HidKeyboardKey_RightArrow, 0x0D},
};
bool s_kb_prev[256] = {};  // indexed by Smart Keyboard code

// Six-axis sensors for player 1, one per way the controller can be held
// (same set as libnx's hid/sixaxis example): handheld, Pro Controller, and
// the two Joy-Cons of an attached/dual pair.
HidSixAxisSensorHandle s_sixaxis[4];
HidSixAxisSensorHandle s_sixaxis2[3];  // player 2: Pro Controller, Joy-Con pair (left, right)

int AccelToByte(float g) {
  const int v = 0x80 + (int)(g * 64.0f);
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

// Holds the reported motion steady while the controller is still, and passes
// it through untouched while it moves. A real V.Smile Motion controller held
// still reports constant values, and games rely on that: Shrek Forever
// After's Bowling Bonanza throws on any small back-and-forth or drift (a
// change of 1 that returns after a quarter second, or +1 every second),
// which a hand-held Switch controller - tremor plus slow wander of a few
// degrees - produces constantly. But other games need every bit of a real
// movement: Spider-Man & Friends' Safari Showdown reacts to shakes, not to
// held tilts, and ignores a +-32 shake while a +-48 one registers, so any
// smoothing or deadband on a moving controller eats the gesture (the first
// version of this filter - a 7-degree deadband on smoothed values - made it
// need extreme motions on hardware).
//
// "Moving" is decided from the gyro, which reads rotation directly and is far
// less noisy than the accelerometer at rest, plus a quick-change check on the
// accelerometer for sideways shakes that barely rotate. The gyro is smoothed
// first so hand tremor (a fast, tiny back-and-forth) averages out while a
// deliberate turn doesn't. Movement keeps the output live for a few frames
// after it stops so the settled position gets reported. While still, a
// slow tilt still gets through once it's kMotionDeadband away.
struct MotionFilter {
  bool primed = false;
  float fast[3] = {};  // lightly smoothed accelerometer, for the quick-change check
  float slow[3] = {};  // heavily smoothed accelerometer
  float rot[3] = {};   // smoothed gyro, revolutions per second
  int live = 0;        // frames left to pass readings through
  int out[3] = {};
};
constexpr float kMotionFastAlpha = 0.5f;
constexpr float kMotionSlowAlpha = 0.1f;
constexpr float kMotionRotAlpha = 0.1f;            // 10 Hz tremor is cut to about a tenth
constexpr float kMotionRotMoving = 20.0f / 360.0f;  // 20 degrees per second
constexpr float kMotionJerkMoving = 6.0f;           // accelerometer units, 0x40 per g
constexpr int kMotionLiveFrames = 8;
constexpr float kMotionDeadband = 8.0f;  // ~7 degrees, for slow tilts while "still"
MotionFilter s_motion_filter[2];         // players 1 and 2

void FilterMotion(MotionFilter& f, const int raw[3], const HidVector& gyro) {
  const float g[3] = {gyro.x, gyro.y, gyro.z};
  if (!f.primed) {
    for (int i = 0; i < 3; i++) {
      f.fast[i] = f.slow[i] = (float)raw[i];
      f.out[i] = raw[i];
    }
    f.primed = true;
    return;
  }
  float rot2 = 0.0f;
  bool jerk = false;
  for (int i = 0; i < 3; i++) {
    f.fast[i] += kMotionFastAlpha * ((float)raw[i] - f.fast[i]);
    f.slow[i] += kMotionSlowAlpha * ((float)raw[i] - f.slow[i]);
    f.rot[i] += kMotionRotAlpha * (g[i] - f.rot[i]);
    rot2 += f.rot[i] * f.rot[i];
    if (std::fabs(f.fast[i] - f.slow[i]) > kMotionJerkMoving) jerk = true;
  }
  if (jerk || rot2 > kMotionRotMoving * kMotionRotMoving) f.live = kMotionLiveFrames;
  if (f.live > 0) {
    f.live--;
    for (int i = 0; i < 3; i++) f.out[i] = raw[i];
    return;
  }
  for (int i = 0; i < 3; i++) {
    if (std::fabs(f.slow[i] - (float)f.out[i]) >= kMotionDeadband) f.out[i] = (int)std::lround(f.slow[i]);
  }
}

bool UsbKeyDown(const HidKeyboardState& st, int key) {
  return (st.keys[key / 64] >> (key % 64)) & 1;
}

// Folds the stick pseudo-buttons libnx already synthesizes (from the stick
// crossing libnx's own internal deadzone) into the D-Pad bits, so a single
// check below covers both - same trick as the Yokoi reference port.
uint64_t merge_bit(uint64_t bits, uint64_t from, uint64_t to) {
  return (bits & from) ? (bits | to) : bits;
}

// One stick axis (-32767..32767) as a V.Smile joystick level, -5..5: nothing
// inside a 20% dead zone, full tilt from 85% out (so a diagonal on the
// Switch's round gate still reaches level 4), evenly spaced in between.
int StickLevel(int v) {
  constexpr int kDead = JOYSTICK_MAX * 20 / 100, kFull = JOYSTICK_MAX * 85 / 100;
  const int a = v < 0 ? -v : v;
  if (a < kDead) return 0;
  const int level = std::min(5, 1 + (a - kDead) * 5 / (kFull - kDead));
  return v < 0 ? -level : level;
}

// `bindings` holds a button name per V.Smile button, indexed like
// kVsmileActions: g_settings.action_binding for player 1 (whose first 8
// entries are exactly those actions), g_settings.p2_binding for player 2.
SwitchInputState PollPad(PadState& pad, const std::string* bindings) {
  padUpdate(&pad);
  uint64_t held = padGetButtons(&pad);
  uint64_t down = padGetButtonsDown(&pad);
  uint64_t up = padGetButtonsUp(&pad);
  const uint64_t dpad = held & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right);

  held = merge_bit(held, HidNpadButton_StickLUp, HidNpadButton_Up);
  held = merge_bit(held, HidNpadButton_StickLDown, HidNpadButton_Down);
  held = merge_bit(held, HidNpadButton_StickLLeft, HidNpadButton_Left);
  held = merge_bit(held, HidNpadButton_StickLRight, HidNpadButton_Right);
  // Also fold into `down`/`up` (not just `held`) so the ROM browser's menu
  // navigation - which reacts to a fresh press, not a held level - responds
  // to the left stick the same way it does to the physical D-Pad.
  down = merge_bit(down, HidNpadButton_StickLUp, HidNpadButton_Up);
  down = merge_bit(down, HidNpadButton_StickLDown, HidNpadButton_Down);
  down = merge_bit(down, HidNpadButton_StickLLeft, HidNpadButton_Left);
  down = merge_bit(down, HidNpadButton_StickLRight, HidNpadButton_Right);
  up = merge_bit(up, HidNpadButton_StickLUp, HidNpadButton_Up);
  up = merge_bit(up, HidNpadButton_StickLDown, HidNpadButton_Down);
  up = merge_bit(up, HidNpadButton_StickLLeft, HidNpadButton_Left);
  up = merge_bit(up, HidNpadButton_StickLRight, HidNpadButton_Right);

  SwitchInputState s;
  s.held = held;
  s.down = down;
  s.up = up;

  // The V.Smile joystick reports 5 levels per direction (findings.md §3), so
  // the left stick maps its deflection onto them; the D-Pad is always full
  // tilt. The BIOS test screen shows the result (X/Y read 3..7 for levels 1..5).
  const HidAnalogStickState stick = padGetStickPos(&pad, 0);
  if (dpad & HidNpadButton_Up) s.joy_y = 5;
  else if (dpad & HidNpadButton_Down) s.joy_y = -5;
  else s.joy_y = StickLevel(stick.y);
  if (dpad & HidNpadButton_Left) s.joy_x = -5;
  else if (dpad & HidNpadButton_Right) s.joy_x = 5;
  else s.joy_x = StickLevel(stick.x);

  // Settings > Controller-configurable, unlike the joystick above - see
  // switch_settings.h's GameAction/action_binding/p2_binding doc comments.
  uint32_t buttons = 0;
  for (int i = 0; i < kVsmileButtonCount; i++) {
    const uint64_t bit = switch_settings_button_bit(bindings[(int)kVsmileActions[i]]);
    if (bit != 0 && (held & bit)) buttons |= kVsmileBits[i];
  }
  s.buttons = buttons;

  return s;
}

}  // namespace

void switch_input_init() {
  // NpadFullCtrl {FullKey, Handheld, JoyDual}, deliberately *not*
  // NpadStandard (which also adds JoyLeft/JoyRight, a single Joy-Con held
  // sideways as its own complete controller) - combined Joy-Con pairs,
  // Pro Controllers, and comparable USB controllers only. Single/sideways
  // Joy-Con mode isn't meant to be a supported option here at all, so it
  // shouldn't even be offered as one.
  padConfigureInput(2, HidNpadStyleSet_NpadFullCtrl);
  padInitializeDefault(&s_pad);                    // No1 + Handheld, player 1
  padInitialize(&s_pad2, HidNpadIdType_No2);        // No2 only, player 2

  // Explicitly pin whatever's connected to No1/No2 as combined-pair (not
  // single/split) controllers, rather than trusting the system's own
  // default pairing behavior to keep two players cleanly separated on its
  // own - reported on hardware as full crosstalk (either physical
  // controller driving both players) without this. Note this is Dual, not
  // Single: hidSetNpadJoyAssignmentModeSingleByDefault() was tried first
  // and caused a real regression on hardware - it explicitly requests
  // *single* (sideways, split) mode, so every connected Joy-Con pair got
  // split into two independent one-Joy-Con controllers instead of staying
  // combined, exactly the opposite of what's wanted here. Best-effort: a
  // no-op on anything that isn't a bare Joy-Con pair (Pro Controllers etc.
  // have no left/right pairing to begin with), so harmless either way -
  // Result deliberately ignored, same as other best-effort setup calls in
  // this codebase.
  hidSetNpadJoyAssignmentModeDual(HidNpadIdType_No1);
  hidSetNpadJoyAssignmentModeDual(HidNpadIdType_No2);

  hidInitializeKeyboard();

  hidGetSixAxisSensorHandles(&s_sixaxis[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  hidGetSixAxisSensorHandles(&s_sixaxis[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
  hidGetSixAxisSensorHandles(&s_sixaxis[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
  for (const HidSixAxisSensorHandle& h : s_sixaxis) hidStartSixAxisSensor(h);
  hidGetSixAxisSensorHandles(&s_sixaxis2[0], 1, HidNpadIdType_No2, HidNpadStyleTag_NpadFullKey);
  hidGetSixAxisSensorHandles(&s_sixaxis2[1], 2, HidNpadIdType_No2, HidNpadStyleTag_NpadJoyDual);
  for (const HidSixAxisSensorHandle& h : s_sixaxis2) hidStartSixAxisSensor(h);
}

// The Switch reports acceleration in G with x right, y toward the top of
// the controller, z out of its face. The V.Smile Motion controller reads as a
// gravity sensor with x right, y toward the player, z down: tilting right
// raises x (Rapid Race steering), lying level reads +1g on z, and tipping
// the top away from you (tilt forward) lowers y - Spider-Man & Friends'
// tree climb says "tilt forward to climb up", and the unflipped y made it
// work backwards on hardware.
SwitchMotionInput MotionFromState(const HidSixAxisSensorState& st, MotionFilter& filter) {
  const int raw[3] = {AccelToByte(st.acceleration.x), AccelToByte(-st.acceleration.y),
                      AccelToByte(-st.acceleration.z)};
  FilterMotion(filter, raw, st.angular_velocity);
  SwitchMotionInput out;
  out.x = filter.out[0];
  out.y = filter.out[1];
  out.z = filter.out[2];
  return out;
}

SwitchMotionInput switch_input_poll_motion() {
  HidSixAxisSensorState st{};
  const u64 style = padGetStyleSet(&s_pad);
  size_t n = 0;
  if (style & HidNpadStyleTag_NpadHandheld) {
    n = hidGetSixAxisSensorStates(s_sixaxis[0], &st, 1);
  } else if (style & HidNpadStyleTag_NpadFullKey) {
    n = hidGetSixAxisSensorStates(s_sixaxis[1], &st, 1);
  } else if (style & HidNpadStyleTag_NpadJoyDual) {
    const bool left = padGetAttributes(&s_pad) & HidNpadAttribute_IsLeftConnected;
    n = hidGetSixAxisSensorStates(s_sixaxis[left ? 2 : 3], &st, 1);
  }
  // No sensor: report level and still.
  return n == 0 ? SwitchMotionInput{} : MotionFromState(st, s_motion_filter[0]);
}

SwitchMotionInput switch_input_poll_motion_p2() {
  HidSixAxisSensorState st{};
  const u64 style = padGetStyleSet(&s_pad2);
  size_t n = 0;
  if (style & HidNpadStyleTag_NpadFullKey) {
    n = hidGetSixAxisSensorStates(s_sixaxis2[0], &st, 1);
  } else if (style & HidNpadStyleTag_NpadJoyDual) {
    const bool left = padGetAttributes(&s_pad2) & HidNpadAttribute_IsLeftConnected;
    n = hidGetSixAxisSensorStates(s_sixaxis2[left ? 1 : 2], &st, 1);
  }
  return n == 0 ? SwitchMotionInput{} : MotionFromState(st, s_motion_filter[1]);
}

SwitchKeyboardInput switch_input_poll_keyboard() {
  SwitchKeyboardInput out;
  HidKeyboardState st{};
  if (hidGetKeyboardStates(&st, 1) == 0) return out;  // nothing sampled: treat as no keys

  bool held[256] = {};
  for (const KeyMap& m : kKeyMap) {
    if (UsbKeyDown(st, m.usb)) held[m.code] = true;
  }
  if (UsbKeyDown(st, HidKeyboardKey_Return) || UsbKeyDown(st, HidKeyboardKey_NumPadEnter)) {
    out.buttons |= 1;  // OK
  }
  if (UsbKeyDown(st, HidKeyboardKey_Escape)) out.buttons |= 2;  // Quit
  if (UsbKeyDown(st, HidKeyboardKey_F1)) out.buttons |= 4;      // Help
  // The keyboard's colour buttons (left to right red, yellow, blue, green,
  // like the real one) and ABC.
  if (UsbKeyDown(st, HidKeyboardKey_F5)) out.buttons |= 16;   // Red
  if (UsbKeyDown(st, HidKeyboardKey_F6)) out.buttons |= 32;   // Yellow
  if (UsbKeyDown(st, HidKeyboardKey_F7)) out.buttons |= 64;   // Blue
  if (UsbKeyDown(st, HidKeyboardKey_F8)) out.buttons |= 128;  // Green
  if (UsbKeyDown(st, HidKeyboardKey_F9)) out.buttons |= 8;    // ABC

  // Shift goes down before, and up after, any letter changing in the same
  // frame, so a quick Shift+letter still arrives as a capital.
  if (held[kKbShift] && !s_kb_prev[kKbShift]) out.events.push_back({kKbShift, true});
  for (int code = 0; code < 256; code++) {
    if (code == kKbShift || held[code] == s_kb_prev[code]) continue;
    out.events.push_back({(uint8_t)code, held[code]});
  }
  if (!held[kKbShift] && s_kb_prev[kKbShift]) out.events.push_back({kKbShift, false});
  std::copy(std::begin(held), std::end(held), std::begin(s_kb_prev));
  return out;
}

SwitchInputState switch_input_poll() { return PollPad(s_pad, g_settings.action_binding); }
// Updates s_pad2 (padUpdate) - the only place that happens, so callers
// should poll this once per frame and use switch_input_player2_ready()
// afterward rather than each doing their own padUpdate(); calling it twice
// a frame doesn't corrupt held-button reads, but does eat single-frame
// down/up edges on the second call (the first call's padUpdate already
// moved them into "old"), so it's worth getting right even though nothing
// currently reads player 2's down/up.
SwitchInputState switch_input_poll_p2() { return PollPad(s_pad2, g_settings.p2_binding); }

bool switch_input_player2_ready() { return padIsConnected(&s_pad2); }
