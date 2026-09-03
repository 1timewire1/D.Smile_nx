#include "switch_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>

#include <switch.h>

#include "input/switch_input.h"
#include "render/switch_chrome.h"
#include "render/switch_ui.h"
#include "switch_dirbrowse.h"
#include "switch_frameskip.h"
#include "switch_library.h"
#include "switch_present.h"
#include "switch_settings.h"

namespace {

// Matches main.cpp's fixed viewport (see its own kViewportW/kViewportH) -
// the whole project targets one fixed handheld-native resolution, so this
// is redeclared locally rather than threaded through every call, same as
// switch_render.cpp does.
constexpr int kViewportW = 1280, kViewportH = 720;

constexpr const char* kBiosDir = "sdmc:/switch/dsmile/bios";

enum class Screen {
  GameGrid,
  SettingsRoot,
  Launcher,
  LibraryStorage,
  GameFolders,
  BiosPicker,
  Graphics,
  Controller,
  InGamePause,
  StateSlots,
};
Screen g_screen = Screen::GameGrid;

// Where SettingsRoot's B/+ should go back to - the game grid (opened via Y
// there) or the in-game pause menu (opened via its own Settings row). Set
// whenever something transitions *into* SettingsRoot.
Screen g_settings_return_to = Screen::GameGrid;

// The currently-loaded cart's path, purely for the Save/Load State slot
// picker to know which files to look for - see switch_menu_set_current_game().
std::string g_current_rom_path;

bool g_state_slots_is_save = true;
int g_state_slots_sel = 0, g_state_slots_scroll = 0;

int g_grid_sel = 0;
int g_list_scroll = 0;  // list view mode's scroll offset (grid mode pages instead)
int g_settings_root_sel = 0, g_settings_root_scroll = 0;
int g_launcher_sel = 0, g_launcher_scroll = 0;
int g_library_storage_sel = 0, g_library_storage_scroll = 0;
int g_pause_sel = 0, g_pause_scroll = 0;
int g_folders_sel = 0, g_folders_scroll = 0;
int g_bios_picker_sel = 0, g_bios_picker_scroll = 0;
int g_graphics_sel = 0, g_graphics_scroll = 0;
int g_controller_sel = 0, g_controller_scroll = 0;

// Sliding highlight fill position, shared across whichever single screen is
// current (reset to -1 - snap next frame - on every screen transition).
float g_highlight_y = -1.0f;

constexpr const char* kThemeKeys[] = {"vsmile", "animated", "xmb", "classic", "oled", "homebrew"};
constexpr const char* kThemeNames[] = {"V.Smile", "Glow", "XMB (PS3)", "Classic", "OLED black", "Bubbles"};
constexpr int kThemeCount = 6;

int ThemeIndex(const std::string& key) {
  for (int i = 0; i < kThemeCount; i++) {
    if (key == kThemeKeys[i]) return i;
  }
  return 0;
}

// Graphics settings option lists - keys match switch_render.cpp's own
// string comparisons (switch_settings.h documents the key sets), names are
// what's shown on screen. Ported 1:1 from Android's ShaderMode/AspectMode/
// BackgroundMode/BezelMode enums (GameRenderer.kt) and the main README's
// "Look" feature list.
constexpr const char* kShaderKeys[] = {"pixel", "sharp", "crt"};
constexpr const char* kShaderNames[] = {"Pixel", "Sharp", "CRT"};
constexpr const char* kAspectKeys[] = {"four_three", "stretch", "integer"};
constexpr const char* kAspectNames[] = {"4:3", "Stretch", "Integer"};
constexpr const char* kBackgroundKeys[] = {"black", "blue", "purple"};
constexpr const char* kBackgroundNames[] = {"Black", "Wavy Blue", "V.Smile Purple"};
constexpr const char* kBezelKeys[] = {"none", "silver", "black"};
constexpr const char* kBezelNames[] = {"None", "Silver", "Black"};
constexpr int kOptionCount3 = 3;

int OptionIndex(const char* const* keys, int count, const std::string& key) {
  for (int i = 0; i < count; i++) {
    if (key == keys[i]) return i;
  }
  return 0;
}

bool IsGridView() { return g_settings.view_mode == "grid"; }

const char* SortName(SortMode m) {
  switch (m) {
    case SortMode::RecentlyPlayed: return "Recently played";
    case SortMode::RecentlyAdded: return "Recently added";
    case SortMode::Alpha:
    default: return "A-Z";
  }
}

// .bin files directly inside sdmc:/switch/dsmile/bios/ - sorted
// alphabetically so the default pick (below) and the picker's listing agree
// on what "first" means. No subfolder recursion, same as the games folders.
std::vector<std::string> ListBiosFiles() {
  std::vector<std::string> files;
  DIR* dir = opendir(kBiosDir);
  if (!dir) return files;
  while (struct dirent* ent = readdir(dir)) {
    if (ent->d_name[0] == '.') continue;
    std::string name = ent->d_name;
    if (name.size() < 4) continue;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext != ".bin") continue;
    files.push_back(std::move(name));
  }
  closedir(dir);
  std::sort(files.begin(), files.end());
  return files;
}

// Which of `files` is actually in effect: g_settings.bios_file if it's
// still present, otherwise the first one alphabetically (so a single
// sysrom.bin - or anything else - just works with no configuration, same
// as before this setting existed), or "" if the folder has nothing in it.
std::string ActiveBiosFile(const std::vector<std::string>& files) {
  if (files.empty()) return "";
  if (!g_settings.bios_file.empty() &&
      std::find(files.begin(), files.end(), g_settings.bios_file) != files.end()) {
    return g_settings.bios_file;
  }
  return files.front();
}

// Standard zlib/PNG CRC-32 (reflected, poly 0xEDB88320) of a whole file -
// just enough to tell a handful of specific known BIOS dumps apart by
// content rather than by filename (which the user can rename). Table is
// built once and kept for the process lifetime; 2MB is a few ms at most,
// and this only ever runs from a settings screen, never gameplay.
uint32_t Crc32File(const std::string& path) {
  static uint32_t table[256];
  static bool table_ready = false;
  if (!table_ready) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    table_ready = true;
  }
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return 0;
  uint32_t crc = 0xFFFFFFFFu;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
  }
  fclose(f);
  return crc ^ 0xFFFFFFFFu;
}

// CRC-32 of the currently-active BIOS file, cached by filename so it's only
// actually recomputed when the active BIOS changes, not every frame this
// screen is open.
std::string g_bios_crc_cache_file;
uint32_t g_bios_crc_cache = 0;
uint32_t ActiveBiosCrc32(const std::string& active_filename) {
  if (active_filename.empty()) return 0;
  if (active_filename != g_bios_crc_cache_file) {
    g_bios_crc_cache_file = active_filename;
    g_bios_crc_cache = Crc32File(std::string(kBiosDir) + "/" + active_filename);
  }
  return g_bios_crc_cache;
}

// These two CRCs are the V.Smile *Motion* system's known BIOS dumps
// (MAME's vsmilem CRC(60fa5426)/CRC(427087ea)) - the Motion system's region
// nibble maps to a different set of languages than the standard V.Smile's
// (see RegionLabel() below), so which table applies depends on which BIOS
// is actually loaded, not just on the nibble value itself.
constexpr uint32_t kMotionBiosCrc[] = {0x60fa5426u, 0x427087eau};

// Region/language label for VSmile::SetRegion()'s raw nibble - sourced
// verbatim from MAME's src/mame/vtech/vsmile.cpp INPUT_PORTS_START(vsmile)
// and (vsmilem) PORT_CONFNAME("Language")/PORT_CONFSETTING blocks for the
// "REGION" port, which is the exact hardware register this core's
// VSmile::GpioIn() Port C read mirrors (see switch_settings.h's own comment
// on SwitchSettings::region for how this was discovered). Codes not listed
// here are genuinely undocumented in MAME's own driver, not an omission on
// this port's part - shown as "(undocumented)" rather than guessed at.
std::string RegionLabel(int code, bool is_motion) {
  static const std::pair<int, const char*> kStandard[] = {
      {0x02, "Italian"},       {0x07, "Chinese"},        {0x08, "Portuguese"},
      {0x09, "Dutch"},         {0x0b, "German"},         {0x0c, "Spanish"},
      {0x0d, "French"},        {0x0e, "English (UK)"},   {0x0f, "English (US)"},
  };
  // MAME's own comments note some of these are uncertain/renumbered between
  // firmware revisions (e.g. Italy was previously 0x0a) - carried over as-is.
  static const std::pair<int, const char*> kMotion[] = {
      {0x02, "Italian"},          {0x05, "English (1)"},  {0x06, "English (2)"},
      {0x07, "Chinese"},          {0x08, "Mexican Spanish"}, {0x09, "Dutch (?)"},
      {0x0b, "German"},           {0x0c, "Spanish"},       {0x0d, "French"},
      {0x0f, "English (3)"},
  };
  const auto* table = is_motion ? kMotion : kStandard;
  const int count = is_motion ? (int)(sizeof(kMotion) / sizeof(kMotion[0]))
                               : (int)(sizeof(kStandard) / sizeof(kStandard[0]));
  for (int i = 0; i < count; i++) {
    if (table[i].first == code) return table[i].second;
  }
  return "(undocumented)";
}

std::string FreeSpaceText() {
  struct statvfs st{};
  if (statvfs("sdmc:/", &st) != 0) return "unknown";
  const double free_gb = (double)st.f_bsize * (double)st.f_bavail / (1024.0 * 1024.0 * 1024.0);
  const double total_gb = (double)st.f_bsize * (double)st.f_blocks / (1024.0 * 1024.0 * 1024.0);
  char buf[64];
  snprintf(buf, sizeof(buf), "%.1f GB free of %.1f GB", free_gb, total_gb);
  return buf;
}

// Ported from reference/DrasticDS_nx-main's gridNav()/gridPage() - 2D grid
// navigation over a flat, page-major index. dy never crosses a page
// boundary (only Left/Right and the L/R page-shoulder keys do); see that
// source for the reasoning already spelled out there.
int GridNav(int sel, int dx, int dy, int cols, int rows, int n) {
  if (n <= 0) return 0;
  const int per = cols * rows;
  const int page = sel / per, pos = sel % per, cr = pos / cols, cc = pos % cols;
  auto clamp = [&](int i) { return i >= n ? n - 1 : (i < 0 ? 0 : i); };
  if (dx > 0) {
    if (cc < cols - 1 && page * per + cr * cols + cc + 1 < n) return page * per + cr * cols + cc + 1;
    if ((page + 1) * per < n) return clamp((page + 1) * per + cr * cols);
    return sel;
  }
  if (dx < 0) {
    if (cc > 0) return sel - 1;
    if (page > 0) return clamp((page - 1) * per + cr * cols + (cols - 1));
    return sel;
  }
  if (dy > 0) {
    if (cr < rows - 1 && page * per + (cr + 1) * cols + cc < n) return page * per + (cr + 1) * cols + cc;
    return sel;
  }
  if (dy < 0) {
    if (cr > 0) return sel - cols;
    return sel;
  }
  return sel;
}

int GridPage(int sel, int dir, int cols, int rows, int n) {
  if (n <= 0) return 0;
  const int per = cols * rows, pos = sel % per, maxpage = (n - 1) / per;
  int np = sel / per + dir;
  if (np < 0) np = 0;
  if (np > maxpage) np = maxpage;
  const int i = np * per + pos;
  return i >= n ? n - 1 : i;
}

// Draws game.display_name centered under/inside a box of width box_w,
// ellipsized when unselected or slid via marquee when selected - shared by
// the grid's placeholder tile text and its below-cover title caption.
void DrawTileLabel(float box_x, float box_y, float box_w, float box_h, float pixel_size,
                    bool selected, const ChromeColor& color, const std::string& text,
                    int viewport_h) {
  // Text that already fits is always centered, whether selected or not -
  // switch_ui_draw_text_marquee's own "fits" case draws left-aligned
  // (correct for the left-aligned settings rows it's also used for via
  // switch_chrome_draw_row), which would otherwise make a tile's name jump
  // from centered to left-aligned the instant it's selected. Only once text
  // actually needs shortening does selected/unselected diverge (marquee vs
  // ellipsis), and neither of those starts from a centered position anyway.
  const float tw = switch_ui_text_width(text, pixel_size);
  if (tw <= box_w) {
    switch_ui_draw_text(box_x + (box_w - tw) * 0.5f, box_y, pixel_size, color.r, color.g, color.b,
                         1.0f, text);
    return;
  }
  if (selected) {
    switch_ui_draw_text_marquee(box_x, box_y, box_w, box_h, pixel_size, color.r, color.g, color.b,
                                 1.0f, text, viewport_h);
    return;
  }
  const std::string shown = switch_ui_ellipsize(text, pixel_size, box_w);
  const float sw = switch_ui_text_width(shown, pixel_size);
  switch_ui_draw_text(box_x + (box_w - sw) * 0.5f, box_y, pixel_size, color.r, color.g, color.b,
                       1.0f, shown);
}

void RenderGameListView(int viewport_w, int viewport_h, const std::vector<LibraryGame>& games) {
  const int n = (int)games.size();
  char ctx[64];
  snprintf(ctx, sizeof(ctx), "%d / %d    Sort: %s", g_grid_sel + 1, n, SortName(g_settings.sort_mode));
  switch_chrome_draw_header(viewport_w, "D.Smile", ctx);

  switch_chrome_draw_row_list(viewport_w, viewport_h, n, g_grid_sel, &g_list_scroll, &g_highlight_y,
                               [&](int i, std::string& label, std::string&) {
                                 label = games[(size_t)i].display_name;
                               });

  switch_chrome_draw_footer(viewport_w, viewport_h, "A  Play   Y  Settings   X  Sort   +  Quit");
}

void RenderGameGridView(int viewport_w, int viewport_h, const std::vector<LibraryGame>& games) {
  const int n = (int)games.size();
  const int cols = g_settings.grid_columns, rows = g_settings.grid_rows;
  const int per_page = cols * rows;
  const int page = g_grid_sel / per_page;
  const int pages = (n - 1) / per_page + 1;

  char ctx[96];
  snprintf(ctx, sizeof(ctx), "%d / %d    Page %d/%d    Sort: %s", g_grid_sel + 1, n, page + 1, pages,
           SortName(g_settings.sort_mode));
  switch_chrome_draw_header(viewport_w, "D.Smile", ctx);

  const float grid_left = kChromeListLeft;
  const float grid_top = kChromeListTop;
  const float grid_w = switch_chrome_list_width(viewport_w);
  const float grid_h = (float)(viewport_h - kChromeFooterH - 8) - grid_top;
  constexpr float kGutter = 16.0f;
  const float tile_w = (grid_w - kGutter * (float)(cols - 1)) / (float)cols;
  const float title_h = g_settings.show_game_titles ? 28.0f : 0.0f;
  const float tile_h = (grid_h - kGutter * (float)(rows - 1)) / (float)rows;
  const float cover_h = tile_h - title_h;

  const int page_start = page * per_page;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const int idx = page_start + r * cols + c;
      if (idx >= n) continue;
      const float x = grid_left + (float)c * (tile_w + kGutter);
      const float y = grid_top + (float)r * (tile_h + kGutter);
      const bool selected = (idx == g_grid_sel);
      const LibraryGame& game = games[(size_t)idx];

      if (selected) {
        constexpr float kBorder = 4.0f;
        switch_ui_draw_rect(x - kBorder, y - kBorder, tile_w + kBorder * 2.0f,
                             cover_h + kBorder * 2.0f, g_col_sel.r, g_col_sel.g, g_col_sel.b, 1.0f);
      }

      if (game.cover_tex != 0 && game.cover_w > 0 && game.cover_h > 0) {
        const float img_aspect = (float)game.cover_w / (float)game.cover_h;
        const float area_aspect = tile_w / cover_h;
        float draw_w, draw_h;
        if (img_aspect > area_aspect) {
          draw_w = tile_w;
          draw_h = tile_w / img_aspect;
        } else {
          draw_h = cover_h;
          draw_w = cover_h * img_aspect;
        }
        switch_ui_draw_rect(x, y, tile_w, cover_h, g_col_panel.r, g_col_panel.g, g_col_panel.b, 1.0f);
        switch_ui_draw_texture(game.cover_tex, x + (tile_w - draw_w) * 0.5f,
                                y + (cover_h - draw_h) * 0.5f, draw_w, draw_h);
      } else {
        // No cover art found for this game: a plain tinted tile with the
        // name on it, no attempt to fetch one (see README's cover art note).
        switch_ui_draw_rect(x, y, tile_w, cover_h, g_col_focus.r, g_col_focus.g, g_col_focus.b, 1.0f);
        const float fit = std::min(18.0f, tile_w / 6.0f);
        constexpr float kPad = 8.0f;
        DrawTileLabel(x + kPad, y + cover_h * 0.5f - fit * 0.5f, tile_w - kPad * 2.0f, fit + 4.0f,
                      fit, selected, g_col_txt, game.display_name, viewport_h);
      }

      if (g_settings.show_game_titles) {
        const ChromeColor& c = selected ? g_col_sel : g_col_dim;
        DrawTileLabel(x, y + cover_h + 4.0f, tile_w, 20.0f, 16.0f, selected, c, game.display_name,
                      viewport_h);
      }
    }
  }

  switch_chrome_draw_footer(viewport_w, viewport_h,
                             "A  Play   Y  Settings   X  Sort   L/R  Page   +  Quit");
}

void RenderGameGrid(int viewport_w, int viewport_h) {
  const auto& games = switch_library_games();
  if (games.empty()) {
    switch_chrome_draw_header(viewport_w, "D.Smile");
    const float list_w = switch_chrome_list_width(viewport_w);
    switch_ui_draw_rect(kChromeListLeft, kChromeListTop, list_w, kChromeRowHeight * 3.0f,
                         g_col_panel.r, g_col_panel.g, g_col_panel.b, g_col_panel.a);
    switch_ui_draw_text(kChromeListLeft + 24.0f, kChromeListTop + 24.0f, 22.0f, g_col_txt.r,
                         g_col_txt.g, g_col_txt.b, 1.0f, "No games found.");
    switch_ui_draw_text(kChromeListLeft + 24.0f, kChromeListTop + 56.0f, 18.0f, g_col_dim.r,
                         g_col_dim.g, g_col_dim.b, 1.0f,
                         "Add a folder with .bin cart dumps in");
    switch_ui_draw_text(kChromeListLeft + 24.0f, kChromeListTop + 82.0f, 18.0f, g_col_sel.r,
                         g_col_sel.g, g_col_sel.b, 1.0f, "Settings > Library & Storage.");
    switch_chrome_draw_footer(viewport_w, viewport_h, "Y  Settings      +  Quit");
    return;
  }
  if (IsGridView()) RenderGameGridView(viewport_w, viewport_h, games);
  else RenderGameListView(viewport_w, viewport_h, games);
}

void RenderSettingsRoot(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Settings");
  static constexpr const char* kLabels[] = {"Launcher", "Library & Storage", "Graphics",
                                             "Controller"};
  switch_chrome_draw_row_list(viewport_w, viewport_h, 4, g_settings_root_sel, &g_settings_root_scroll,
                               &g_highlight_y, [&](int i, std::string& label, std::string&) {
                                 label = kLabels[i];
                               });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A  Open      B  Back");
}

// Row order matches Android's own pause menu (EmuActivity.kt's showMenu()
// item list) wherever there's a Switch equivalent: Resume, Save/Load State,
// then Reset/Quit/Settings (Settings stands in for the tail of Android's
// list - Shader/CRT/Aspect/Render mode/Background/Bezel/Map buttons/Trigger
// sensitivity - all of which live in Settings > Graphics/Controller here
// instead of directly in this menu). No Fast Forward row - disabled in this
// build, see switch/README.md.
enum {
  kPauseRowResume,
  kPauseRowSaveState,
  kPauseRowLoadState,
  kPauseRowResetGame,
  kPauseRowQuit,
  kPauseRowSettings,
  kPauseRowCount,
};

void RenderInGamePause(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Paused");
  static constexpr const char* kLabels[] = {"Resume",     "Save State", "Load State",
                                             "Reset Game", "Quit",       "Settings"};
  switch_chrome_draw_row_list(
      viewport_w, viewport_h, kPauseRowCount, g_pause_sel, &g_pause_scroll, &g_highlight_y,
      [&](int i, std::string& label, std::string&) { label = kLabels[i]; });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A  Select      B  Resume");
}

// "Empty" or the slot's save time, e.g. "2026-08-28 14:03" - simpler than
// Android's relative-time-span formatting ("2 minutes ago"), same idea.
std::string StateSlotValue(int slot) {
  struct stat st{};
  if (stat(switch_settings_state_file_path(g_current_rom_path, slot).c_str(), &st) != 0) {
    return "Empty";
  }
  char buf[32];
  const time_t t = st.st_mtime;
  struct tm tm_buf{};
  localtime_r(&t, &tm_buf);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
  return buf;
}

void RenderStateSlots(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, g_state_slots_is_save ? "Save State" : "Load State");
  switch_chrome_draw_row_list(viewport_w, viewport_h, 3, g_state_slots_sel, &g_state_slots_scroll,
                               &g_highlight_y, [&](int i, std::string& label, std::string& value) {
                                 label = "Slot " + std::to_string(i + 1);
                                 value = StateSlotValue(i);
                               });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A  Select      B  Cancel");
}

// Launcher settings rows: View and Theme are always shown; the grid-only
// rows (Games per row / Rows per page / Show game titles) only apply -  and
// only appear - when View is set to Grid. UI animations is always last.
enum LauncherRow { LR_VIEW, LR_THEME, LR_GRID_COLUMNS, LR_GRID_ROWS, LR_SHOW_TITLES, LR_UI_ANIM };

std::vector<LauncherRow> VisibleLauncherRows() {
  std::vector<LauncherRow> rows = {LR_VIEW, LR_THEME};
  if (IsGridView()) {
    rows.push_back(LR_GRID_COLUMNS);
    rows.push_back(LR_GRID_ROWS);
    rows.push_back(LR_SHOW_TITLES);
  }
  rows.push_back(LR_UI_ANIM);
  return rows;
}

std::string LauncherRowLabel(LauncherRow r) {
  switch (r) {
    case LR_VIEW: return "View";
    case LR_THEME: return "Theme";
    case LR_GRID_COLUMNS: return "Games per row";
    case LR_GRID_ROWS: return "Rows per page";
    case LR_SHOW_TITLES: return "Show game titles";
    case LR_UI_ANIM: return "UI animations";
  }
  return "";
}

std::string LauncherRowValue(LauncherRow r) {
  switch (r) {
    case LR_VIEW: return IsGridView() ? "Grid (cover art)" : "List";
    case LR_THEME: return kThemeNames[ThemeIndex(g_settings.theme)];
    case LR_GRID_COLUMNS: return std::to_string(g_settings.grid_columns);
    case LR_GRID_ROWS: return std::to_string(g_settings.grid_rows);
    case LR_SHOW_TITLES: return g_settings.show_game_titles ? "On" : "Off";
    case LR_UI_ANIM: return g_settings.ui_animations ? "On" : "Off";
  }
  return "";
}

void RenderLauncherSettings(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Launcher");
  const auto rows = VisibleLauncherRows();
  switch_chrome_draw_row_list(viewport_w, viewport_h, (int)rows.size(), g_launcher_sel,
                               &g_launcher_scroll, &g_highlight_y,
                               [&](int i, std::string& label, std::string& value) {
                                 label = LauncherRowLabel(rows[(size_t)i]);
                                 value = LauncherRowValue(rows[(size_t)i]);
                               });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A / <-  ->   Change      B  Back");
}

// Graphics settings rows: Renderer/Shader/Aspect/Background/Bezel are
// always shown; the 5 CRT intensity sliders only apply - and only appear -
// when Shader is set to CRT (mirrors VisibleLauncherRows' grid-only-rows
// pattern above).
enum GraphicsRow {
  GX_RENDERER,
  GX_FRAMESKIP_MODE,
  GX_FRAMESKIP_AMOUNT,
  GX_SHADER,
  GX_ASPECT,
  GX_BACKGROUND,
  GX_BEZEL,
  GX_CRT_CURVE,
  GX_CRT_GLOW,
  GX_CRT_SCAN,
  GX_CRT_MASK,
  GX_CRT_VIGNETTE,
};

bool IsCrtShader() { return g_settings.shader_mode == "crt"; }

constexpr const char* kFrameSkipModeKeys[] = {"off", "auto", "manual"};
constexpr const char* kFrameSkipModeNames[] = {"Off", "Auto", "Manual"};

std::vector<GraphicsRow> VisibleGraphicsRows() {
  std::vector<GraphicsRow> rows = {GX_RENDERER, GX_FRAMESKIP_MODE};
  if (g_settings.frame_skip_mode == "manual") rows.push_back(GX_FRAMESKIP_AMOUNT);
  rows.push_back(GX_SHADER);
  rows.push_back(GX_ASPECT);
  rows.push_back(GX_BACKGROUND);
  rows.push_back(GX_BEZEL);
  if (IsCrtShader()) {
    rows.push_back(GX_CRT_CURVE);
    rows.push_back(GX_CRT_GLOW);
    rows.push_back(GX_CRT_SCAN);
    rows.push_back(GX_CRT_MASK);
    rows.push_back(GX_CRT_VIGNETTE);
  }
  return rows;
}

std::string GraphicsRowLabel(GraphicsRow r) {
  switch (r) {
    case GX_RENDERER: return "Renderer";
    case GX_FRAMESKIP_MODE: return "Frame Skip";
    case GX_FRAMESKIP_AMOUNT: return "Skip Amount";
    case GX_SHADER: return "Shader";
    case GX_ASPECT: return "Aspect Ratio";
    case GX_BACKGROUND: return "Background";
    case GX_BEZEL: return "TV Bezel";
    case GX_CRT_CURVE: return "CRT Curvature";
    case GX_CRT_GLOW: return "CRT Glow";
    case GX_CRT_SCAN: return "CRT Scanlines";
    case GX_CRT_MASK: return "CRT Aperture Grille";
    case GX_CRT_VIGNETTE: return "CRT Vignette";
  }
  return "";
}

std::string Pct(float v) { return std::to_string((int)(v * 100.0f + 0.5f)) + "%"; }

std::string GraphicsRowValue(GraphicsRow r) {
  switch (r) {
    case GX_RENDERER: return g_settings.accurate_renderer ? "Accurate" : "Fast";
    case GX_FRAMESKIP_MODE: {
      const int idx = OptionIndex(kFrameSkipModeKeys, 3, g_settings.frame_skip_mode);
      std::string v = kFrameSkipModeNames[idx];
      if (g_settings.frame_skip_mode == "auto") {
        v += " (currently " + std::to_string(switch_frameskip_current_level()) + ")";
      }
      return v;
    }
    case GX_FRAMESKIP_AMOUNT: return std::to_string(g_settings.frame_skip_manual);
    case GX_SHADER: return kShaderNames[OptionIndex(kShaderKeys, kOptionCount3, g_settings.shader_mode)];
    case GX_ASPECT: return kAspectNames[OptionIndex(kAspectKeys, kOptionCount3, g_settings.aspect_mode)];
    case GX_BACKGROUND:
      return kBackgroundNames[OptionIndex(kBackgroundKeys, kOptionCount3, g_settings.background_mode)];
    case GX_BEZEL: return kBezelNames[OptionIndex(kBezelKeys, kOptionCount3, g_settings.bezel_mode)];
    case GX_CRT_CURVE: return Pct(g_settings.crt_curve);
    case GX_CRT_GLOW: return Pct(g_settings.crt_glow);
    case GX_CRT_SCAN: return Pct(g_settings.crt_scan);
    case GX_CRT_MASK: return Pct(g_settings.crt_mask);
    case GX_CRT_VIGNETTE: return Pct(g_settings.crt_vignette);
  }
  return "";
}

void RenderGraphicsSettings(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Graphics");
  const auto rows = VisibleGraphicsRows();
  switch_chrome_draw_row_list(viewport_w, viewport_h, (int)rows.size(), g_graphics_sel,
                               &g_graphics_scroll, &g_highlight_y,
                               [&](int i, std::string& label, std::string& value) {
                                 label = GraphicsRowLabel(rows[(size_t)i]);
                                 value = GraphicsRowValue(rows[(size_t)i]);
                               });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A / <-  ->   Change      B  Back");
}

// The 12 GameActions in Controller-screen row order, plus a 13th row for
// the (currently inert - see switch_settings.h) trigger threshold.
constexpr GameAction kControllerActions[] = {
    GameAction::Enter,   GameAction::Back,  GameAction::Help,        GameAction::Abc,
    GameAction::Red,     GameAction::Yellow, GameAction::Blue,       GameAction::Green,
    GameAction::SaveState, GameAction::LoadState, GameAction::Rewind,
    GameAction::Menu,
};
constexpr int kControllerActionCount = kGameActionCount;
constexpr int kControllerRowCount = kControllerActionCount + 1;  // + trigger threshold

// Blocks until a bindable physical button is pressed (returns its
// switch_settings_button_name(), e.g. "A") or B cancels (returns "").
// Same nested-blocking-loop shape as switch_dirbrowse_pick - this is called
// synchronously from inside switch_menu_update() while binding a row, and
// runs its own input/render/present loop until it resolves.
std::string CaptureButtonPress(const std::string& action_label, int viewport_w, int viewport_h) {
  // B always cancels here (matches the rest of this menu's convention), so
  // it's the one physical button this prompt can never bind to an action -
  // see switch_settings.cpp's kButtonMeta comment.
  static constexpr const char* kCapturable[] = {"A",  "X", "Y",      "L",     "R",
                                                 "ZL", "ZR", "StickL", "StickR", "Plus", "Minus"};
  for (;;) {
    if (!appletMainLoop()) return "";
    const SwitchInputState in = switch_input_poll();
    if (in.down & HidNpadButton_B) return "";
    for (const char* name : kCapturable) {
      if (in.down & switch_settings_button_bit(name)) return name;
    }

    switch_chrome_clear_background(viewport_w, viewport_h);
    switch_ui_begin_frame(viewport_w, viewport_h);
    switch_chrome_draw_header(viewport_w, "Controller");
    const float list_w = switch_chrome_list_width(viewport_w);
    switch_ui_draw_rect(kChromeListLeft, kChromeListTop, list_w, kChromeRowHeight * 3.0f,
                         g_col_panel.r, g_col_panel.g, g_col_panel.b, g_col_panel.a);
    switch_ui_draw_text(kChromeListLeft + 24.0f, kChromeListTop + 20.0f, 22.0f, g_col_dim.r,
                         g_col_dim.g, g_col_dim.b, 1.0f, "Press a button to bind:");
    switch_ui_draw_text(kChromeListLeft + 24.0f, kChromeListTop + 56.0f, 28.0f, g_col_val.r,
                         g_col_val.g, g_col_val.b, 1.0f, action_label);
    switch_chrome_draw_footer(viewport_w, viewport_h, "B  Cancel");
    switch_present();
  }
}

void RenderController(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Controller");
  switch_chrome_draw_row_list(
      viewport_w, viewport_h, kControllerRowCount, g_controller_sel, &g_controller_scroll,
      &g_highlight_y, [&](int i, std::string& label, std::string& value) {
        if (i < kControllerActionCount) {
          label = switch_settings_action_label(kControllerActions[i]);
          const std::string& bound = g_settings.action_binding[(int)kControllerActions[i]];
          value = bound.empty() ? "Unbound" : bound;
        } else {
          label = "Trigger Sensitivity";
          value = Pct(g_settings.trigger_threshold);
        }
      });
  switch_chrome_draw_footer(viewport_w, viewport_h,
                             "A  Bind      X  Unbind      <-  ->  Adjust      B  Back");
}

// BIOS and Region only take effect on the *next* VSmile::LoadGame() - a
// fresh instance's LoadSysrom()/SetRegion() call, at launch - so changing
// either mid-game would silently do nothing to the game actually running,
// which would look like the setting is broken rather than just "takes
// effect next launch". Hidden rather than shown-but-disabled (same
// convention VisibleLauncherRows()/VisibleGraphicsRows() already use for
// rows that don't currently apply) whenever this screen was reached from
// the in-game pause menu instead of the main grid.
enum LibraryStorageRow { LS_GAME_FOLDERS, LS_BIOS, LS_REGION, LS_STORAGE, LS_RESCAN };

std::vector<LibraryStorageRow> VisibleLibraryStorageRows() {
  std::vector<LibraryStorageRow> rows = {LS_GAME_FOLDERS};
  if (g_settings_return_to != Screen::InGamePause) {
    rows.push_back(LS_BIOS);
    rows.push_back(LS_REGION);
  }
  rows.push_back(LS_STORAGE);
  rows.push_back(LS_RESCAN);
  return rows;
}

void RenderLibraryStorage(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Library & Storage");
  const auto bios_files = ListBiosFiles();
  const std::string active_bios = ActiveBiosFile(bios_files);
  const size_t folder_count = switch_settings_game_folders().size();
  const bool is_motion_bios = !active_bios.empty() &&
      std::find(std::begin(kMotionBiosCrc), std::end(kMotionBiosCrc),
                ActiveBiosCrc32(active_bios)) != std::end(kMotionBiosCrc);
  const std::string region_value =
      std::to_string(g_settings.region) + " - " + RegionLabel(g_settings.region, is_motion_bios);
  const auto rows = VisibleLibraryStorageRows();
  g_library_storage_sel = std::min(g_library_storage_sel, (int)rows.size() - 1);
  switch_chrome_draw_row_list(
      viewport_w, viewport_h, (int)rows.size(), g_library_storage_sel, &g_library_storage_scroll,
      &g_highlight_y, [&](int i, std::string& label, std::string& value) {
        switch (rows[(size_t)i]) {
          case LS_GAME_FOLDERS:
            label = "Game folders";
            value = std::to_string(folder_count) + (folder_count == 1 ? " folder" : " folders");
            break;
          case LS_BIOS:
            label = "BIOS";
            value = active_bios.empty() ? "None found (optional)" : active_bios;
            break;
          case LS_REGION:
            label = "Region";
            value = region_value;
            break;
          case LS_STORAGE:
            label = "Storage";
            value = FreeSpaceText();
            break;
          case LS_RESCAN:
            label = "Rescan library";
            value = std::to_string(switch_library_games().size()) + " games loaded";
            break;
        }
      });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A  Open / Rescan      <- ->  Region      B  Back");
}

void RenderGameFolders(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "Game Folders");
  const auto folders = switch_settings_game_folders();
  const int row_count = 1 + (int)folders.size();
  switch_chrome_draw_row_list(viewport_w, viewport_h, row_count, g_folders_sel, &g_folders_scroll,
                               &g_highlight_y, [&](int i, std::string& label, std::string&) {
                                 label = (i == 0) ? "[ Add game folder ]" : folders[(size_t)(i - 1)];
                               });
  switch_chrome_draw_footer(viewport_w, viewport_h,
                             folders.size() > 1 ? "A  Add / Remove      B  Back" : "A  Add      B  Back");
}

// Flat list of *.bin files directly inside sdmc:/switch/dsmile/bios/ - picking
// one sets g_settings.bios_file. No tree navigation needed (unlike game
// folders, which can be anywhere on the card) since the BIOS location is
// fixed, so this is a simple row list rather than switch_dirbrowse_pick's
// own folder-tree screen.
void RenderBiosPicker(int viewport_w, int viewport_h) {
  switch_chrome_draw_header(viewport_w, "BIOS");
  const auto files = ListBiosFiles();
  if (files.empty()) {
    switch_chrome_draw_row_list(viewport_w, viewport_h, 1, 0, &g_bios_picker_scroll, &g_highlight_y,
                                 [&](int, std::string& label, std::string&) {
                                   label = "No .bin files in /switch/dsmile/bios";
                                 });
    switch_chrome_draw_footer(viewport_w, viewport_h, "B  Back");
    return;
  }
  const std::string active = ActiveBiosFile(files);
  switch_chrome_draw_row_list(viewport_w, viewport_h, (int)files.size(), g_bios_picker_sel,
                               &g_bios_picker_scroll, &g_highlight_y,
                               [&](int i, std::string& label, std::string& value) {
                                 label = files[(size_t)i];
                                 value = (files[(size_t)i] == active) ? "Active" : "";
                               });
  switch_chrome_draw_footer(viewport_w, viewport_h, "A  Use this BIOS      B  Back");
}

}  // namespace

void switch_menu_init() {
  switch_settings_load();
  switch_chrome_set_theme(g_settings.theme);
  switch_library_rescan();
}

void switch_menu_shutdown() { switch_library_shutdown(); }

void switch_menu_enter_pause() {
  g_screen = Screen::InGamePause;
  g_pause_sel = 0;
  g_highlight_y = -1.0f;
}

void switch_menu_set_current_game(const std::string& rom_path) { g_current_rom_path = rom_path; }

MenuAction switch_menu_update(uint64_t k_down, uint64_t /*k_held*/, std::string& out_path,
                               int& out_slot) {
  switch (g_screen) {
    case Screen::GameGrid: {
      if (k_down & HidNpadButton_Plus) return MenuAction::Quit;
      if (k_down & HidNpadButton_Y) {
        g_screen = Screen::SettingsRoot;
        g_settings_return_to = Screen::GameGrid;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      const auto& games = switch_library_games();
      const int n = (int)games.size();
      if (n > 0) {
        if (g_grid_sel >= n) g_grid_sel = n - 1;

        if (IsGridView()) {
          const int cols = g_settings.grid_columns, rows = g_settings.grid_rows;
          if (k_down & HidNpadButton_Up) g_grid_sel = GridNav(g_grid_sel, 0, -1, cols, rows, n);
          else if (k_down & HidNpadButton_Down) g_grid_sel = GridNav(g_grid_sel, 0, 1, cols, rows, n);
          else if (k_down & HidNpadButton_Left) g_grid_sel = GridNav(g_grid_sel, -1, 0, cols, rows, n);
          else if (k_down & HidNpadButton_Right) g_grid_sel = GridNav(g_grid_sel, 1, 0, cols, rows, n);
          if (k_down & HidNpadButton_L) g_grid_sel = GridPage(g_grid_sel, -1, cols, rows, n);
          else if (k_down & HidNpadButton_R) g_grid_sel = GridPage(g_grid_sel, 1, cols, rows, n);
        } else {
          if (k_down & HidNpadButton_Up) g_grid_sel = (g_grid_sel - 1 + n) % n;
          else if (k_down & HidNpadButton_Down) g_grid_sel = (g_grid_sel + 1) % n;
          // g_list_scroll itself is kept in view by switch_chrome_draw_row_list
          // on the render side every frame - no need to duplicate that here.
        }

        if (k_down & HidNpadButton_X) {
          const std::string keep = games[(size_t)g_grid_sel].full_path;
          g_settings.sort_mode = (SortMode)(((int)g_settings.sort_mode + 1) % (int)SortMode::Count);
          switch_settings_save();
          switch_library_resort();
          g_grid_sel = 0;
          g_list_scroll = 0;
          const auto& resorted = switch_library_games();
          for (size_t i = 0; i < resorted.size(); i++) {
            if (resorted[i].full_path == keep) {
              g_grid_sel = (int)i;
              break;
            }
          }
        }
        if (k_down & HidNpadButton_A) {
          out_path = games[(size_t)g_grid_sel].full_path;
          return MenuAction::LaunchGame;
        }
      }
      return MenuAction::None;
    }

    case Screen::SettingsRoot: {
      if (k_down & (HidNpadButton_B | HidNpadButton_Plus)) {
        g_screen = g_settings_return_to;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      constexpr int kRows = 4;
      static constexpr Screen kTargets[kRows] = {Screen::Launcher, Screen::LibraryStorage,
                                                  Screen::Graphics, Screen::Controller};
      if (k_down & HidNpadButton_Up) g_settings_root_sel = (g_settings_root_sel - 1 + kRows) % kRows;
      else if (k_down & HidNpadButton_Down) g_settings_root_sel = (g_settings_root_sel + 1) % kRows;
      if (k_down & HidNpadButton_A) {
        g_screen = kTargets[g_settings_root_sel];
        g_highlight_y = -1.0f;
      }
      return MenuAction::None;
    }

    case Screen::Launcher: {
      if (k_down & HidNpadButton_B) {
        g_screen = Screen::SettingsRoot;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      const auto rows = VisibleLauncherRows();
      const int n = (int)rows.size();
      g_launcher_sel = std::min(g_launcher_sel, n - 1);
      if (k_down & HidNpadButton_Up) g_launcher_sel = (g_launcher_sel - 1 + n) % n;
      else if (k_down & HidNpadButton_Down) g_launcher_sel = (g_launcher_sel + 1) % n;

      int dir = 0;
      if (k_down & HidNpadButton_Left) dir = -1;
      else if (k_down & (HidNpadButton_Right | HidNpadButton_A)) dir = 1;
      if (dir != 0) {
        switch (rows[(size_t)g_launcher_sel]) {
          case LR_VIEW:
            g_settings.view_mode = IsGridView() ? "list" : "grid";
            g_grid_sel = 0;
            g_list_scroll = 0;
            break;
          case LR_THEME: {
            const int idx = (ThemeIndex(g_settings.theme) + dir + kThemeCount) % kThemeCount;
            g_settings.theme = kThemeKeys[idx];
            switch_chrome_set_theme(g_settings.theme);
            break;
          }
          case LR_GRID_COLUMNS:
            g_settings.grid_columns = std::max(3, std::min(8, g_settings.grid_columns + dir));
            g_grid_sel = 0;
            break;
          case LR_GRID_ROWS:
            g_settings.grid_rows = std::max(1, std::min(3, g_settings.grid_rows + dir));
            g_grid_sel = 0;
            break;
          case LR_SHOW_TITLES: g_settings.show_game_titles = !g_settings.show_game_titles; break;
          case LR_UI_ANIM: g_settings.ui_animations = !g_settings.ui_animations; break;
        }
        switch_settings_save();
      }
      return MenuAction::None;
    }

    case Screen::Graphics: {
      if (k_down & HidNpadButton_B) {
        g_screen = Screen::SettingsRoot;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      const auto rows = VisibleGraphicsRows();
      const int n = (int)rows.size();
      g_graphics_sel = std::min(g_graphics_sel, n - 1);
      if (k_down & HidNpadButton_Up) g_graphics_sel = (g_graphics_sel - 1 + n) % n;
      else if (k_down & HidNpadButton_Down) g_graphics_sel = (g_graphics_sel + 1) % n;

      int dir = 0;
      if (k_down & HidNpadButton_Left) dir = -1;
      else if (k_down & (HidNpadButton_Right | HidNpadButton_A)) dir = 1;
      if (dir != 0) {
        constexpr float kCrtStep = 0.05f;
        switch (rows[(size_t)g_graphics_sel]) {
          case GX_RENDERER: g_settings.accurate_renderer = !g_settings.accurate_renderer; break;
          case GX_FRAMESKIP_MODE: {
            const int idx =
                (OptionIndex(kFrameSkipModeKeys, 3, g_settings.frame_skip_mode) + dir + 3) % 3;
            g_settings.frame_skip_mode = kFrameSkipModeKeys[idx];
            break;
          }
          case GX_FRAMESKIP_AMOUNT:
            g_settings.frame_skip_manual = std::max(0, std::min(10, g_settings.frame_skip_manual + dir));
            break;
          case GX_SHADER: {
            const int idx =
                (OptionIndex(kShaderKeys, kOptionCount3, g_settings.shader_mode) + dir + kOptionCount3) %
                kOptionCount3;
            g_settings.shader_mode = kShaderKeys[idx];
            break;
          }
          case GX_ASPECT: {
            const int idx =
                (OptionIndex(kAspectKeys, kOptionCount3, g_settings.aspect_mode) + dir + kOptionCount3) %
                kOptionCount3;
            g_settings.aspect_mode = kAspectKeys[idx];
            break;
          }
          case GX_BACKGROUND: {
            const int idx = (OptionIndex(kBackgroundKeys, kOptionCount3, g_settings.background_mode) +
                              dir + kOptionCount3) %
                             kOptionCount3;
            g_settings.background_mode = kBackgroundKeys[idx];
            break;
          }
          case GX_BEZEL: {
            const int idx =
                (OptionIndex(kBezelKeys, kOptionCount3, g_settings.bezel_mode) + dir + kOptionCount3) %
                kOptionCount3;
            g_settings.bezel_mode = kBezelKeys[idx];
            break;
          }
          case GX_CRT_CURVE:
            g_settings.crt_curve = std::max(0.0f, std::min(1.0f, g_settings.crt_curve + dir * kCrtStep));
            break;
          case GX_CRT_GLOW:
            g_settings.crt_glow = std::max(0.0f, std::min(1.0f, g_settings.crt_glow + dir * kCrtStep));
            break;
          case GX_CRT_SCAN:
            g_settings.crt_scan = std::max(0.0f, std::min(1.0f, g_settings.crt_scan + dir * kCrtStep));
            break;
          case GX_CRT_MASK:
            g_settings.crt_mask = std::max(0.0f, std::min(1.0f, g_settings.crt_mask + dir * kCrtStep));
            break;
          case GX_CRT_VIGNETTE:
            g_settings.crt_vignette =
                std::max(0.0f, std::min(1.0f, g_settings.crt_vignette + dir * kCrtStep));
            break;
        }
        switch_settings_save();
      }
      return MenuAction::None;
    }

    case Screen::Controller: {
      if (k_down & HidNpadButton_B) {
        g_screen = Screen::SettingsRoot;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      if (k_down & HidNpadButton_Up) {
        g_controller_sel = (g_controller_sel - 1 + kControllerRowCount) % kControllerRowCount;
      } else if (k_down & HidNpadButton_Down) {
        g_controller_sel = (g_controller_sel + 1) % kControllerRowCount;
      }

      if (g_controller_sel < kControllerActionCount) {
        const GameAction action = kControllerActions[g_controller_sel];
        if (k_down & HidNpadButton_X) {
          switch_settings_bind(action, "");  // unbind
        } else if (k_down & HidNpadButton_A) {
          const std::string picked =
              CaptureButtonPress(switch_settings_action_label(action), kViewportW, kViewportH);
          g_highlight_y = -1.0f;  // resync after the capture prompt's own blocking loop
          if (!picked.empty()) switch_settings_bind(action, picked);
        }
      } else {
        int dir = 0;
        if (k_down & HidNpadButton_Left) dir = -1;
        else if (k_down & (HidNpadButton_Right | HidNpadButton_A)) dir = 1;
        if (dir != 0) {
          g_settings.trigger_threshold =
              std::max(0.05f, std::min(0.95f, g_settings.trigger_threshold + dir * 0.05f));
          switch_settings_save();
        }
      }
      return MenuAction::None;
    }

    case Screen::LibraryStorage: {
      if (k_down & HidNpadButton_B) {
        g_screen = Screen::SettingsRoot;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      const auto rows = VisibleLibraryStorageRows();
      const int n = (int)rows.size();
      g_library_storage_sel = std::min(g_library_storage_sel, n - 1);
      if (k_down & HidNpadButton_Up) g_library_storage_sel = (g_library_storage_sel - 1 + n) % n;
      else if (k_down & HidNpadButton_Down) g_library_storage_sel = (g_library_storage_sel + 1) % n;

      const LibraryStorageRow row = rows[(size_t)g_library_storage_sel];
      if (row == LS_REGION && (k_down & (HidNpadButton_Left | HidNpadButton_Right))) {
        const int dir = (k_down & HidNpadButton_Left) ? -1 : 1;
        g_settings.region = (g_settings.region + dir + 16) % 16;
        switch_settings_save();
      }
      if (k_down & HidNpadButton_A) {
        if (row == LS_GAME_FOLDERS) {
          g_screen = Screen::GameFolders;
          g_folders_sel = 0;
          g_highlight_y = -1.0f;
        } else if (row == LS_BIOS) {
          g_screen = Screen::BiosPicker;
          g_bios_picker_sel = 0;
          g_highlight_y = -1.0f;
        } else if (row == LS_RESCAN) {
          switch_library_rescan();
          g_grid_sel = 0;
          g_list_scroll = 0;
        }
      }
      return MenuAction::None;
    }

    case Screen::BiosPicker: {
      if (k_down & HidNpadButton_B) {
        g_screen = Screen::LibraryStorage;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      const auto files = ListBiosFiles();
      if (!files.empty()) {
        const int row_count = (int)files.size();
        if (k_down & HidNpadButton_Up) g_bios_picker_sel = (g_bios_picker_sel - 1 + row_count) % row_count;
        else if (k_down & HidNpadButton_Down) g_bios_picker_sel = (g_bios_picker_sel + 1) % row_count;
        if (k_down & HidNpadButton_A) {
          g_settings.bios_file = files[(size_t)g_bios_picker_sel];
          switch_settings_save();
        }
      }
      return MenuAction::None;
    }

    case Screen::GameFolders: {
      if (k_down & HidNpadButton_B) {
        g_screen = Screen::LibraryStorage;
        g_highlight_y = -1.0f;
        return MenuAction::None;
      }
      auto folders = switch_settings_game_folders();
      const int row_count = 1 + (int)folders.size();
      if (k_down & HidNpadButton_Up) g_folders_sel = (g_folders_sel - 1 + row_count) % row_count;
      else if (k_down & HidNpadButton_Down) g_folders_sel = (g_folders_sel + 1) % row_count;

      if (k_down & HidNpadButton_A) {
        if (g_folders_sel == 0) {
          if (folders.size() >= 16) return MenuAction::None;  // DraStic's own cap
          const std::string picked = switch_dirbrowse_pick("sdmc:/switch/dsmile", kViewportW, kViewportH);
          g_highlight_y = -1.0f;  // resync after the dirbrowse's own blocking loop
          if (!picked.empty() && std::find(folders.begin(), folders.end(), picked) == folders.end()) {
            folders.push_back(picked);
            switch_settings_set_game_folders(folders);
            switch_library_rescan();
            g_grid_sel = 0;
            g_list_scroll = 0;
          }
        } else if (folders.size() > 1) {
          // No confirmation dialog yet (see README) - removing only drops
          // the folder reference, no files are touched.
          folders.erase(folders.begin() + (g_folders_sel - 1));
          switch_settings_set_game_folders(folders);
          switch_library_rescan();
          g_grid_sel = 0;
          g_list_scroll = 0;
          g_folders_sel = std::max(0, g_folders_sel - 1);
        }
      }
      return MenuAction::None;
    }

    case Screen::InGamePause: {
      if (k_down & HidNpadButton_Up) g_pause_sel = (g_pause_sel - 1 + kPauseRowCount) % kPauseRowCount;
      else if (k_down & HidNpadButton_Down) g_pause_sel = (g_pause_sel + 1) % kPauseRowCount;

      if (k_down & HidNpadButton_B) return MenuAction::ResumeGame;
      if (k_down & HidNpadButton_A) {
        switch (g_pause_sel) {
          case kPauseRowResume: return MenuAction::ResumeGame;
          case kPauseRowSaveState:
          case kPauseRowLoadState:
            g_screen = Screen::StateSlots;
            g_state_slots_is_save = (g_pause_sel == kPauseRowSaveState);
            g_state_slots_sel = 0;
            g_highlight_y = -1.0f;
            break;
          case kPauseRowResetGame: return MenuAction::ResetGame;
          case kPauseRowQuit:
            g_screen = Screen::GameGrid;  // so the grid (not this screen) renders once RomList takes over
            g_highlight_y = -1.0f;
            return MenuAction::QuitToGrid;
          case kPauseRowSettings:
            g_screen = Screen::SettingsRoot;
            g_settings_return_to = Screen::InGamePause;
            g_highlight_y = -1.0f;
            break;
        }
      }
      return MenuAction::None;
    }

    case Screen::StateSlots: {
      // Matches Android's own pickSlot(): picking *any* slot - including an
      // empty one in Load mode, which just no-ops - closes the picker back
      // to gameplay, not back to the pause menu. So does cancelling (B) -
      // Android's version is a cancelable AlertDialog with no separate
      // "back to pause menu" path either.
      if (k_down & HidNpadButton_B) return MenuAction::ResumeGame;
      if (k_down & HidNpadButton_Up) g_state_slots_sel = (g_state_slots_sel + 2) % 3;
      else if (k_down & HidNpadButton_Down) g_state_slots_sel = (g_state_slots_sel + 1) % 3;
      if (k_down & HidNpadButton_A) {
        out_slot = g_state_slots_sel;
        return g_state_slots_is_save ? MenuAction::SaveState : MenuAction::LoadState;
      }
      return MenuAction::None;
    }
  }
  return MenuAction::None;
}

void switch_menu_render(int viewport_w, int viewport_h) {
  switch_ui_begin_frame(viewport_w, viewport_h);
  switch (g_screen) {
    case Screen::GameGrid: RenderGameGrid(viewport_w, viewport_h); break;
    case Screen::SettingsRoot: RenderSettingsRoot(viewport_w, viewport_h); break;
    case Screen::Launcher: RenderLauncherSettings(viewport_w, viewport_h); break;
    case Screen::LibraryStorage: RenderLibraryStorage(viewport_w, viewport_h); break;
    case Screen::GameFolders: RenderGameFolders(viewport_w, viewport_h); break;
    case Screen::BiosPicker: RenderBiosPicker(viewport_w, viewport_h); break;
    case Screen::Graphics: RenderGraphicsSettings(viewport_w, viewport_h); break;
    case Screen::Controller: RenderController(viewport_w, viewport_h); break;
    case Screen::InGamePause: RenderInGamePause(viewport_w, viewport_h); break;
    case Screen::StateSlots: RenderStateSlots(viewport_w, viewport_h); break;
  }
}

std::string switch_menu_bios_path() {
  const std::string active = ActiveBiosFile(ListBiosFiles());
  return active.empty() ? std::string() : std::string(kBiosDir) + "/" + active;
}
