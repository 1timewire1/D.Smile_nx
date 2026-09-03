#pragma once
#include <cstdint>
#include <string>

// Game grid + a small settings area, modeled visually on
// reference/DrasticDS_nx-main's SDL launcher (header band, translucent
// "glass" list panels, accent-striped sliding highlight, footer button
// hints, themes) - reimplemented on our own GL+FreeType switch_ui.cpp /
// switch_chrome.cpp toolkit rather than pulling in SDL2/SDL_ttf. Ported
// scope is deliberately narrower than DraStic's own (see switch/README.md):
// local cover art only (no SteamGridDB - there's no V.Smile game database
// to fetch from), no per-game custom settings, no SMB/USB storage.

void switch_menu_init();  // loads settings, applies the saved theme, scans the library
void switch_menu_shutdown();

enum class MenuAction {
  None,
  LaunchGame,   // out_path is set
  Quit,         // + pressed at the game grid (top level): quit to hbmenu
  ResumeGame,   // in-game pause menu: close it, keep playing
  ResetGame,    // in-game pause menu: reset the running game, keep playing
  QuitToGrid,   // in-game pause menu: unload the running game, return to the grid
  SaveState,    // out_slot is set (0..2); main.cpp does the actual VSmile::SaveState()
  LoadState,    // out_slot is set (0..2); main.cpp does the actual VSmile::LoadState()
};

// Called once per frame while the menu owns the screen. k_down/k_held are
// this frame's raw HidNpadButton_* bits from switch_input_poll(); the menu
// handles all of its own navigation internally, including moving between
// the game grid and the settings screens. out_slot is only meaningful for
// SaveState/LoadState.
MenuAction switch_menu_update(uint64_t k_down, uint64_t k_held, std::string& out_path, int& out_slot);

void switch_menu_render(int viewport_w, int viewport_h);

// Switches the menu into its in-game pause screen (Resume/Save/Load/Reset/
// Quit/Settings) - called by main.cpp when the Menu action's bound button
// is pressed during gameplay. Settings opened from here returns here (not
// to the game grid) when backed out of.
void switch_menu_enter_pause();

// Tells the menu which cart is currently loaded, purely so the Save/Load
// State slot picker can show which of its 3 slots are already occupied
// (via switch_settings_state_file_path()) - the menu otherwise has no
// notion of "the running game" at all, main.cpp owns that. Called right
// after a successful LaunchGame; irrelevant (and harmless if stale) while
// no game is loaded.
void switch_menu_set_current_game(const std::string& rom_path);

// The active BIOS file's full sdmc: path - whichever .bin the user picked in
// Library & Storage > BIOS (g_settings.bios_file), or the first .bin found
// alphabetically in sdmc:/switch/dsmile/bios/ if nothing's been picked yet
// (or the picked one no longer exists). Presence is optional either way -
// nothing requires it (see main README's BIOS note). Returns empty if the
// bios folder has no .bin files at all.
std::string switch_menu_bios_path();
