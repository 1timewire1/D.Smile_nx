#include "switch_dirbrowse.h"

#include <algorithm>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <vector>

#include <switch.h>

#include <glad/glad.h>

#include "input/switch_input.h"
#include "render/switch_chrome.h"
#include "render/switch_ui.h"
#include "switch_present.h"

namespace {

bool CanGoUp(const std::string& path) { return path != "sdmc:/" && path != "sdmc:"; }

std::string ParentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "sdmc:/";
  const std::string parent = path.substr(0, slash);
  return parent == "sdmc:" ? "sdmc:/" : parent;
}

std::vector<std::string> ListSubdirs(const std::string& path) {
  std::vector<std::string> dirs;
  DIR* dir = opendir(path.c_str());
  if (!dir) return dirs;
  while (struct dirent* ent = readdir(dir)) {
    if (ent->d_name[0] == '.') continue;
    const std::string full = path + "/" + ent->d_name;
    struct stat st{};
    if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) dirs.push_back(ent->d_name);
  }
  closedir(dir);
  std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  return dirs;
}

}  // namespace

std::string switch_dirbrowse_pick(const std::string& start_path, int viewport_w, int viewport_h) {
  std::string current = start_path;
  std::vector<std::string> subdirs = ListSubdirs(current);
  int sel = 0, scroll = 0;
  float highlight_y = -1.0f;

  for (;;) {
    if (!appletMainLoop()) return "";

    const bool up_row = CanGoUp(current);
    const int row_count = (int)subdirs.size() + (up_row ? 1 : 0);
    const SwitchInputState in = switch_input_poll();

    if (in.down & HidNpadButton_B) return "";
    if (in.down & HidNpadButton_Y) return current;  // confirm the *current* folder

    if (row_count > 0) {
      if (in.down & HidNpadButton_Up) sel = (sel - 1 + row_count) % row_count;
      else if (in.down & HidNpadButton_Down) sel = (sel + 1) % row_count;
      // scroll itself is kept in view by switch_chrome_draw_row_list below.

      if (in.down & HidNpadButton_A) {
        const bool picked_up = up_row && sel == 0;
        current = picked_up ? ParentOf(current) : current + "/" + subdirs[(size_t)(sel - (up_row ? 1 : 0))];
        subdirs = ListSubdirs(current);
        sel = 0;
        scroll = 0;
        highlight_y = -1.0f;
      }
    }

    switch_chrome_clear_background(viewport_w, viewport_h);

    switch_ui_begin_frame(viewport_w, viewport_h);
    switch_chrome_draw_header(viewport_w, "Select Folder", current);

    if (row_count == 0) {
      const float list_w = switch_chrome_list_width(viewport_w);
      switch_ui_draw_rect(kChromeListLeft, kChromeListTop, list_w, kChromeRowHeight * 2.0f,
                           g_col_panel.r, g_col_panel.g, g_col_panel.b, g_col_panel.a);
      switch_ui_draw_text(kChromeListLeft + 24.0f, kChromeListTop + 24.0f, 20.0f, g_col_dim.r,
                           g_col_dim.g, g_col_dim.b, 1.0f, "No subfolders here.");
    } else {
      switch_chrome_draw_row_list(viewport_w, viewport_h, row_count, sel, &scroll, &highlight_y,
                                   [&](int i, std::string& label, std::string&) {
                                     const bool is_up = up_row && i == 0;
                                     label = is_up ? "[ .. ]" : subdirs[(size_t)(i - (up_row ? 1 : 0))];
                                   });
    }

    switch_chrome_draw_footer(viewport_w, viewport_h, "A  Open      Y  Use this folder      B  Cancel");
    switch_present();
  }
}
