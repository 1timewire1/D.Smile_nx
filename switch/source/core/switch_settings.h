#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Persisted launcher preferences: a small generic key=value store (like
// DraStic's own Store/KV, at a scale that doesn't need its unordered_map
// index - a V.Smile library and its play history are tiny), plus typed
// wrappers for the settings the menu actually knows about. Plain text at
// sdmc:/switch/dsmile/settings.ini (same idea as Yokoi's
// yokoi_settings.txt).

enum class SortMode { Alpha, RecentlyPlayed, RecentlyAdded, Count };

// Every gamepad action Android's InputMapper.kt exposes (Action enum), minus
// FastForward - disabled in this build (see switch/README.md: skipping
// presentation can't out-run VSmile::RunFrame()'s own cost at stock clock,
// so it bought next to nothing and wasn't worth the confusion of a hotkey
// that barely does anything). The 8 V.Smile controller buttons plus the
// remaining 4 hotkeys can all be remapped via Settings > Controller, same as
// Android's binding wizard. D-Pad is deliberately not one of these - it's
// always the V.Smile joystick here, same as it's never one of InputMapper's
// bindable Actions on Android either.
enum class GameAction {
  Enter,
  Back,
  Help,
  Abc,
  Red,
  Yellow,
  Blue,
  Green,
  SaveState,
  LoadState,
  Rewind,
  Menu,
};
constexpr int kGameActionCount = 12;

struct SwitchSettings {
  bool ui_animations = true;
  std::string theme = "vsmile";       // vsmile|animated|xmb|classic|oled|homebrew
  std::string view_mode = "list";    // list|grid - list is the default
  int grid_columns = 6;              // 3..8, matches DraStic's own clamp
  int grid_rows = 2;                 // 1..3
  bool show_game_titles = true;
  SortMode sort_mode = SortMode::Alpha;

  // Graphics - mirrors Android GameRenderer.kt's options (see the main
  // README's "Look" feature list) and VSmile::SetAccurate().
  std::string shader_mode = "sharp";        // pixel|sharp|crt
  std::string aspect_mode = "four_three";   // four_three|stretch|integer
  std::string background_mode = "black";    // black|blue|purple
  std::string bezel_mode = "none";          // none|silver|black
  float crt_curve = 1.0f, crt_glow = 1.0f, crt_scan = 1.0f, crt_mask = 1.0f, crt_vignette = 1.0f;
  bool accurate_renderer = false;

  // Frame skip (see switch_frameskip.h) - MAME-style: off|auto|manual. In
  // "manual", frame_skip_manual (0-10, matching MAME's own frameskip range)
  // is how many out of every (N+1) frames go unrendered. In "auto", that
  // count is instead adjusted live by switch_frameskip.cpp based on measured
  // VSmile::RunFrame() cost, so frame_skip_manual is unused (but its value
  // is preserved, in case the user switches back to manual later).
  std::string frame_skip_mode = "off";  // off|auto|manual
  int frame_skip_manual = 0;            // 0..10

  // Selected BIOS filename (not a full path - just the name of a .bin file
  // inside sdmc:/switch/dsmile/bios/), set via Library & Storage > BIOS.
  // "" means "no explicit choice yet" - switch_menu_bios_path() then falls
  // back to whichever .bin is first alphabetically in that folder, so
  // dropping in a single sysrom.bin still works with zero configuration.
  std::string bios_file = "";

  // Raw 4-bit region/language jumper (VSmile::SetRegion(), read by the BIOS
  // off the emulated Port C pins - see vsmile.cpp's GpioIn()). Real V.Smile
  // hardware apparently shares one BIOS datamask across regions and picks
  // the displayed language from these pins rather than from which physical
  // chip is fitted, which is also why swapping BIOS *files* alone doesn't
  // change the language: this code was never set anywhere in the app (not
  // here, not Android) before now, so it was always stuck at the core's own
  // hardcoded default. The code-to-language mapping isn't in this codebase
  // itself, but it is in MAME's vsmile.cpp driver (src/mame/vtech/vsmile.cpp,
  // INPUT_PORTS_START(vsmile)/(vsmilem)'s "REGION" port) - switch_menu.cpp's
  // RegionLabel() carries that table over verbatim for the Library & Storage
  // Region row's display, separately for the standard V.Smile vs. V.Smile
  // Motion systems (they differ) - see switch/README.md for the full table
  // and how it was found.
  int region = 0xF;  // matches VSmile's own hardcoded default - US English

  // Controller - see GameAction above. action_binding[i] is a button name
  // ("A","B","X","Y","L","R","ZL","ZR","StickL","StickR","Plus","Minus") or
  // "" if unbound; switch_settings_button_bit() converts that to a
  // HidNpadButton_ bit. trigger_threshold mirrors Android's analog-trigger
  // pull-depth setting (InputMapper.kt's triggerThreshold, 0.05..0.95) -
  // stored for parity but not read anywhere yet, since Switch controllers'
  // L2/R2 equivalents (ZL/ZR) are digital, not analog pulls.
  std::string action_binding[kGameActionCount];
  float trigger_threshold = 0.5f;
};

extern SwitchSettings g_settings;

void switch_settings_load();
void switch_settings_save();

// Configured game-source folders. Defaults to a single entry
// (sdmc:/switch/dsmile/games) the first time this runs, so upgrading from
// the single-folder version of the browser doesn't lose anything.
std::vector<std::string> switch_settings_game_folders();
void switch_settings_set_game_folders(const std::vector<std::string>& folders);

// Per-game last-played tracking (unix time), keyed by the game's full
// sdmc: path - used by SortMode::RecentlyPlayed. 0 if never played.
void switch_settings_mark_played(const std::string& full_path);
long long switch_settings_last_played(const std::string& full_path);

// Per-game "last used save-state slot" (0-2), keyed by the game's full
// sdmc: path - mirrors Android's lastSlot_$romName preference. Used by
// main.cpp's direct Save/Load State hotkeys (GameAction::SaveState/
// LoadState - quick-save/load without opening the pause menu's slot
// picker, same as Android's onHotkey() calling saveState(lastSlot())/
// loadState(lastSlot()) directly). 0 (Slot 1) if never saved/loaded.
int switch_settings_last_slot(const std::string& rom_path);
void switch_settings_set_last_slot(const std::string& rom_path, int slot);

// Controller / GameAction support.
const char* switch_settings_action_label(GameAction action);  // e.g. "Enter / OK"

// Binds `action` to `button_name` (see action_binding's doc comment above
// for the valid names), enforcing strict 1:1 the same way Android's
// InputMapper.bind() does: clears button_name from whichever other action
// currently holds it, and replaces action's own previous binding. Saves
// immediately. button_name = "" unbinds action with no other side effect.
void switch_settings_bind(GameAction action, const std::string& button_name);

// A single HidNpadButton_ bit for a binding's stored name (0 if "" or
// unrecognized), and the reverse lookup (used by the Controller screen's
// "press a button to bind" capture prompt). Only covers the fixed set of
// buttons available to bind (see action_binding's doc comment) - not every
// HidNpadButton_ value has a name here.
uint64_t switch_settings_button_bit(const std::string& name);
std::string switch_settings_button_name(uint64_t single_bit);

// Save-state file path for `rom_path`'s cart, slot 0-2 - matches Android's
// own naming (EmuActivity.kt's stateFile(): "<rom base name>.slotN.dss")
// under sdmc:/switch/dsmile/states/ instead of Android's app-private
// external files dir. Shared by main.cpp (the actual save/load) and
// switch_menu.cpp (the slot picker's Empty/saved-at display) so both agree
// on where a given cart's states live without duplicating the naming
// logic. Pure path building - doesn't touch the filesystem itself (callers
// that write need to create the directory first; see main.cpp).
std::string switch_settings_state_file_path(const std::string& rom_path, int slot);
