#include "switch_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>

#include <switch.h>

namespace {
constexpr const char* kPath = "sdmc:/switch/dsmile/settings.ini";
constexpr const char* kDefaultGamesDir = "sdmc:/switch/dsmile/games";

struct ActionMeta {
  const char* key;              // settings.ini key suffix: bind_<key>
  const char* label;            // shown in the Controller screen
  const char* default_button;   // "" = unbound by default (matches Android)
};
// Index-aligned with GameAction's declaration order.
constexpr ActionMeta kActionMeta[kGameActionCount] = {
    {"enter", "Enter / OK", "A"},
    {"back", "Exit", "B"},
    {"help", "Help", "Y"},
    {"abc", "Learning Zone (ABC)", "X"},
    {"red", "Red", "R"},
    {"yellow", "Yellow", "ZL"},
    {"blue", "Blue", "ZR"},
    {"green", "Green", "L"},
    {"save_state", "Save State", ""},
    {"load_state", "Load State", "StickR"},
    {"rewind", "Rewind (hold)", "StickL"},
    {"menu", "Menu", "Plus"},
};

struct ButtonMeta {
  const char* name;
  uint64_t bit;
};
// The fixed set of physical buttons the Controller screen's capture prompt
// will accept - D-Pad is excluded (always the joystick, see GameAction's
// doc comment); B is excluded too, since the capture prompt itself uses B
// to cancel (see switch_menu.cpp) and so can never be captured as a target.
constexpr ButtonMeta kButtonMeta[] = {
    {"A", HidNpadButton_A},           {"B", HidNpadButton_B},
    {"X", HidNpadButton_X},           {"Y", HidNpadButton_Y},
    {"L", HidNpadButton_L},           {"R", HidNpadButton_R},
    {"ZL", HidNpadButton_ZL},         {"ZR", HidNpadButton_ZR},
    {"StickL", HidNpadButton_StickL}, {"StickR", HidNpadButton_StickR},
    {"Plus", HidNpadButton_Plus},     {"Minus", HidNpadButton_Minus},
};
constexpr int kButtonMetaCount = sizeof(kButtonMeta) / sizeof(kButtonMeta[0]);

// Ordered (not hashed - our scale is dozens of entries, not thousands) so
// re-saving preserves a stable, human-readable file order.
std::vector<std::pair<std::string, std::string>> g_kv;

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

const std::string* Find(const std::string& key) {
  for (auto& kv : g_kv) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

std::string GetStr(const std::string& key, const std::string& def) {
  const std::string* v = Find(key);
  return v ? *v : def;
}

int GetInt(const std::string& key, int def) {
  const std::string* v = Find(key);
  return v ? atoi(v->c_str()) : def;
}

float GetFloat(const std::string& key, float def) {
  const std::string* v = Find(key);
  return v ? (float)atof(v->c_str()) : def;
}

bool GetBool(const std::string& key, bool def) {
  const std::string* v = Find(key);
  return v ? (*v == "true") : def;
}

void Set(const std::string& key, const std::string& value) {
  for (auto& kv : g_kv) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  g_kv.emplace_back(key, value);
}

void SetInt(const std::string& key, int value) { Set(key, std::to_string(value)); }
void SetBool(const std::string& key, bool value) { Set(key, value ? "true" : "false"); }
void SetFloat(const std::string& key, float value) { Set(key, std::to_string(value)); }

void RemovePrefix(const std::string& prefix) {
  g_kv.erase(std::remove_if(g_kv.begin(), g_kv.end(),
                            [&](const std::pair<std::string, std::string>& kv) {
                              return kv.first.compare(0, prefix.size(), prefix) == 0;
                            }),
             g_kv.end());
}

SortMode SortModeFromInt(int v) {
  if (v < 0 || v >= (int)SortMode::Count) return SortMode::Alpha;
  return (SortMode)v;
}

}  // namespace

SwitchSettings g_settings;

void switch_settings_load() {
  g_kv.clear();
  FILE* f = fopen(kPath, "rb");
  if (f) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
      std::string s = Trim(line);
      if (s.empty() || s[0] == '#') continue;
      const size_t eq = s.find('=');
      if (eq == std::string::npos) continue;
      g_kv.emplace_back(Trim(s.substr(0, eq)), Trim(s.substr(eq + 1)));
    }
    fclose(f);
  }

  g_settings.ui_animations = GetBool("ui_animations", true);
  g_settings.theme = GetStr("theme", "vsmile");
  g_settings.view_mode = GetStr("view_mode", "list");
  g_settings.grid_columns = std::max(3, std::min(8, GetInt("grid_columns", 6)));
  g_settings.grid_rows = std::max(1, std::min(3, GetInt("grid_rows", 2)));
  g_settings.show_game_titles = GetBool("show_game_titles", true);
  g_settings.sort_mode = SortModeFromInt(GetInt("sort_mode", 0));

  g_settings.shader_mode = GetStr("shader_mode", "sharp");
  g_settings.aspect_mode = GetStr("aspect_mode", "four_three");
  g_settings.background_mode = GetStr("background_mode", "black");
  g_settings.bezel_mode = GetStr("bezel_mode", "none");
  g_settings.crt_curve = std::max(0.0f, std::min(1.0f, GetFloat("crt_curve", 1.0f)));
  g_settings.crt_glow = std::max(0.0f, std::min(1.0f, GetFloat("crt_glow", 1.0f)));
  g_settings.crt_scan = std::max(0.0f, std::min(1.0f, GetFloat("crt_scan", 1.0f)));
  g_settings.crt_mask = std::max(0.0f, std::min(1.0f, GetFloat("crt_mask", 1.0f)));
  g_settings.crt_vignette = std::max(0.0f, std::min(1.0f, GetFloat("crt_vignette", 1.0f)));
  g_settings.accurate_renderer = GetBool("accurate_renderer", false);
  g_settings.frame_skip_mode = GetStr("frame_skip_mode", "off");
  g_settings.frame_skip_manual = std::max(0, std::min(10, GetInt("frame_skip_manual", 0)));
  g_settings.bios_file = GetStr("bios_file", "");
  g_settings.region = std::max(0, std::min(15, GetInt("region", 0xF)));

  for (int i = 0; i < kGameActionCount; i++) {
    g_settings.action_binding[i] =
        GetStr(std::string("bind_") + kActionMeta[i].key, kActionMeta[i].default_button);
  }
  g_settings.trigger_threshold = std::max(0.05f, std::min(0.95f, GetFloat("trigger_threshold", 0.5f)));
}

void switch_settings_save() {
  SetBool("ui_animations", g_settings.ui_animations);
  Set("theme", g_settings.theme);
  Set("view_mode", g_settings.view_mode);
  SetInt("grid_columns", g_settings.grid_columns);
  SetInt("grid_rows", g_settings.grid_rows);
  SetBool("show_game_titles", g_settings.show_game_titles);
  SetInt("sort_mode", (int)g_settings.sort_mode);

  Set("shader_mode", g_settings.shader_mode);
  Set("aspect_mode", g_settings.aspect_mode);
  Set("background_mode", g_settings.background_mode);
  Set("bezel_mode", g_settings.bezel_mode);
  SetFloat("crt_curve", g_settings.crt_curve);
  SetFloat("crt_glow", g_settings.crt_glow);
  SetFloat("crt_scan", g_settings.crt_scan);
  SetFloat("crt_mask", g_settings.crt_mask);
  SetFloat("crt_vignette", g_settings.crt_vignette);
  SetBool("accurate_renderer", g_settings.accurate_renderer);
  Set("frame_skip_mode", g_settings.frame_skip_mode);
  SetInt("frame_skip_manual", g_settings.frame_skip_manual);
  Set("bios_file", g_settings.bios_file);
  SetInt("region", g_settings.region);

  for (int i = 0; i < kGameActionCount; i++) {
    Set(std::string("bind_") + kActionMeta[i].key, g_settings.action_binding[i]);
  }
  SetFloat("trigger_threshold", g_settings.trigger_threshold);

  FILE* f = fopen(kPath, "wb");
  if (!f) return;  // sdmc:/switch/dsmile/ not existing yet is not fatal here
  for (auto& kv : g_kv) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
  fclose(f);
}

std::vector<std::string> switch_settings_game_folders() {
  const int count = GetInt("game_folder_count", -1);
  if (count < 0) return {kDefaultGamesDir};  // never configured: single default folder
  std::vector<std::string> folders;
  for (int i = 0; i < count; i++) {
    folders.push_back(GetStr("game_folder_" + std::to_string(i), ""));
  }
  if (folders.empty()) folders.push_back(kDefaultGamesDir);
  return folders;
}

void switch_settings_set_game_folders(const std::vector<std::string>& folders) {
  RemovePrefix("game_folder_");
  SetInt("game_folder_count", (int)folders.size());
  for (size_t i = 0; i < folders.size(); i++) {
    Set("game_folder_" + std::to_string(i), folders[i]);
  }
  switch_settings_save();
}

void switch_settings_mark_played(const std::string& full_path) {
  Set("played:" + full_path, std::to_string((long long)time(nullptr)));
  switch_settings_save();
}

long long switch_settings_last_played(const std::string& full_path) {
  const std::string* v = Find("played:" + full_path);
  return v ? atoll(v->c_str()) : 0;
}

const char* switch_settings_action_label(GameAction action) { return kActionMeta[(int)action].label; }

void switch_settings_bind(GameAction action, const std::string& button_name) {
  if (!button_name.empty()) {
    // Strict 1:1 (matches Android's InputMapper.bind()): steal button_name
    // away from whichever other action currently holds it.
    for (int i = 0; i < kGameActionCount; i++) {
      if (i != (int)action && g_settings.action_binding[i] == button_name) {
        g_settings.action_binding[i] = "";
      }
    }
  }
  g_settings.action_binding[(int)action] = button_name;
  switch_settings_save();
}

uint64_t switch_settings_button_bit(const std::string& name) {
  if (name.empty()) return 0;
  for (int i = 0; i < kButtonMetaCount; i++) {
    if (name == kButtonMeta[i].name) return kButtonMeta[i].bit;
  }
  return 0;
}

std::string switch_settings_button_name(uint64_t single_bit) {
  for (int i = 0; i < kButtonMetaCount; i++) {
    if (single_bit == kButtonMeta[i].bit) return kButtonMeta[i].name;
  }
  return "";
}

int switch_settings_last_slot(const std::string& rom_path) {
  const std::string* v = Find("lastslot:" + rom_path);
  const int slot = v ? atoi(v->c_str()) : 0;
  return (slot >= 0 && slot < 3) ? slot : 0;
}

void switch_settings_set_last_slot(const std::string& rom_path, int slot) {
  Set("lastslot:" + rom_path, std::to_string(slot));
  switch_settings_save();
}

std::string switch_settings_state_file_path(const std::string& rom_path, int slot) {
  std::string base = rom_path;
  const size_t slash = base.find_last_of('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);
  const size_t dot = base.find_last_of('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  return "sdmc:/switch/dsmile/states/" + base + ".slot" + std::to_string(slot) + ".dss";
}
