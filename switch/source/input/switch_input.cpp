#include "switch_input.h"

#include <switch.h>

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

// Folds the stick pseudo-buttons libnx already synthesizes (from the stick
// crossing libnx's own internal deadzone) into the D-Pad bits, so a single
// check below covers both - same trick as the Yokoi reference port.
uint64_t merge_bit(uint64_t bits, uint64_t from, uint64_t to) {
  return (bits & from) ? (bits | to) : bits;
}

SwitchInputState PollPad(PadState& pad) {
  padUpdate(&pad);
  uint64_t held = padGetButtons(&pad);
  uint64_t down = padGetButtonsDown(&pad);
  uint64_t up = padGetButtonsUp(&pad);

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

  if (held & HidNpadButton_Up) s.joy_y = 5;
  else if (held & HidNpadButton_Down) s.joy_y = -5;
  if (held & HidNpadButton_Left) s.joy_x = -5;
  else if (held & HidNpadButton_Right) s.joy_x = 5;

  // Settings > Controller-configurable, unlike the joystick above - see
  // switch_settings.h's GameAction/action_binding doc comments. Shared by
  // both players - see switch_settings.h's player_count comment for why.
  uint32_t buttons = 0;
  for (int i = 0; i < 8; i++) {
    const uint64_t bit = switch_settings_button_bit(g_settings.action_binding[(int)kVsmileActions[i]]);
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
}

SwitchInputState switch_input_poll() { return PollPad(s_pad); }
// Updates s_pad2 (padUpdate) - the only place that happens, so callers
// should poll this once per frame and use switch_input_player2_ready()
// afterward rather than each doing their own padUpdate(); calling it twice
// a frame doesn't corrupt held-button reads, but does eat single-frame
// down/up edges on the second call (the first call's padUpdate already
// moved them into "old"), so it's worth getting right even though nothing
// currently reads player 2's down/up.
SwitchInputState switch_input_poll_p2() { return PollPad(s_pad2); }

bool switch_input_player2_ready() { return padIsConnected(&s_pad2); }
