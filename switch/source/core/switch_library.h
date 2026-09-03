#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The game list's data model: scanning every configured folder
// (switch_settings_game_folders()) for *.bin files, resolving cover art
// (a same-basename *.png next to the .bin - see switch/README.md's cover
// art convention), and sorting per g_settings.sort_mode. switch_menu.cpp
// only renders and navigates this; it doesn't touch the filesystem itself.
struct LibraryGame {
  std::string full_path;     // sdmc: path to the .bin, used to launch it
  std::string display_name;  // filename without the .bin extension
  long long mtime = 0;       // file mtime, for SortMode::RecentlyAdded
  uint32_t cover_tex = 0;    // 0 if no cover art found/decoded
  int cover_w = 0, cover_h = 0;
};

// Frees any cover textures from a previous scan, then re-walks every
// configured folder and re-resolves + re-uploads cover art. Safe to call
// again later (folder list changed, SD card changed).
void switch_library_rescan();

const std::vector<LibraryGame>& switch_library_games();

// Re-sorts the already-scanned list in place per g_settings.sort_mode.
// Doesn't touch cover textures or re-scan the filesystem - cheap enough to
// call every time the sort mode is cycled.
void switch_library_resort();

void switch_library_shutdown();  // frees cover textures
