#include "switch_chrome.h"

#include <algorithm>

#include <glad/glad.h>

#include "switch_ui.h"

#include "core/switch_settings.h"

// DraStic launcher's 5 built-in theme palettes (see
// reference/DrasticDS_nx-main/launcher/source/main.cpp's applyLauncherAppearance()),
// converted from 0-255 SDL_Color to the 0..1 floats switch_ui expects.
// COL_HI/COL_CARD aren't used here yet (nothing needs a distinct hover-highlight
// text color or card-art tint beyond what switch_menu/dirbrowse already
// draw with g_col_sel/g_col_panel), kept out until something needs them
// rather than carried as dead values. COL_BG *is* used (switch_chrome_clear_background)
// - it was missed in the first pass, which is why every theme's background
// stayed the "animated" theme's navy instead of following the theme.
ChromeColor g_col_bg, g_col_txt, g_col_dim, g_col_val, g_col_sel, g_col_panel, g_col_focus;

void switch_chrome_set_theme(const std::string& theme) {
  if (theme == "vsmile") {
    // Default theme - the console/controller's own purple-and-orange color
    // scheme (orange is #F7941D, the exact value the launcher icon's
    // background and the V.Smile mascot's cheeks/mouth already use -
    // app/src/main/res/values/colors.xml's ic_launcher_bg).
    g_col_bg = {0.145f, 0.098f, 0.243f, 1.0f};
    g_col_txt = {0.976f, 0.965f, 0.988f, 1.0f};
    g_col_dim = {0.706f, 0.663f, 0.827f, 1.0f};
    g_col_val = {0.969f, 0.580f, 0.114f, 1.0f};
    g_col_sel = {1.0f, 0.612f, 0.157f, 1.0f};
    g_col_panel = {0.106f, 0.071f, 0.176f, 0.75f};
    g_col_focus = {0.341f, 0.196f, 0.129f, 0.85f};
  } else if (theme == "xmb") {
    g_col_bg = {0.008f, 0.137f, 0.361f, 1.0f};
    g_col_txt = {0.965f, 0.980f, 1.0f, 1.0f};
    g_col_dim = {0.690f, 0.812f, 0.914f, 1.0f};
    g_col_val = {1.0f, 1.0f, 1.0f, 1.0f};
    g_col_sel = {0.455f, 0.855f, 1.0f, 1.0f};
    g_col_panel = {0.016f, 0.110f, 0.286f, 0.643f};
    g_col_focus = {0.078f, 0.357f, 0.580f, 0.839f};
  } else if (theme == "classic") {
    g_col_bg = {0.086f, 0.094f, 0.118f, 1.0f};
    g_col_txt = {0.894f, 0.902f, 0.922f, 1.0f};
    g_col_dim = {0.588f, 0.608f, 0.647f, 1.0f};
    g_col_val = {1.0f, 0.824f, 0.392f, 1.0f};
    g_col_sel = {1.0f, 0.667f, 0.0f, 1.0f};
    g_col_panel = {0.110f, 0.122f, 0.157f, 1.0f};
    g_col_focus = {0.259f, 0.220f, 0.118f, 0.922f};
  } else if (theme == "oled") {
    g_col_bg = {0.0f, 0.0f, 0.0f, 1.0f};
    g_col_txt = {0.961f, 0.969f, 0.976f, 1.0f};
    g_col_dim = {0.569f, 0.592f, 0.620f, 1.0f};
    g_col_val = {1.0f, 1.0f, 1.0f, 1.0f};
    g_col_sel = {0.0f, 0.824f, 0.745f, 1.0f};
    g_col_panel = {0.016f, 0.016f, 0.020f, 0.973f};
    g_col_focus = {0.0f, 0.227f, 0.208f, 0.961f};
  } else if (theme == "homebrew") {
    g_col_bg = {0.0f, 0.031f, 0.063f, 1.0f};
    g_col_txt = {0.922f, 0.973f, 1.0f, 1.0f};
    g_col_dim = {0.561f, 0.753f, 0.847f, 1.0f};
    g_col_val = {0.761f, 0.937f, 1.0f, 1.0f};
    g_col_sel = {0.239f, 0.718f, 0.922f, 1.0f};
    g_col_panel = {0.016f, 0.122f, 0.196f, 0.745f};
    g_col_focus = {0.047f, 0.298f, 0.424f, 0.863f};
  } else {  // "animated" (DraStic's "Glow") - also the fallback for any
            // unrecognized theme string; "vsmile" above is the actual
            // default for a fresh install, see switch_settings.h
    g_col_bg = {0.031f, 0.047f, 0.094f, 1.0f};
    g_col_txt = {0.922f, 0.937f, 0.969f, 1.0f};
    g_col_dim = {0.592f, 0.639f, 0.722f, 1.0f};
    g_col_val = {1.0f, 0.843f, 0.471f, 1.0f};
    g_col_sel = {0.455f, 0.784f, 1.0f, 1.0f};
    g_col_panel = {0.063f, 0.090f, 0.153f, 0.72f};
    g_col_focus = {0.110f, 0.271f, 0.361f, 0.82f};
  }
}

void switch_chrome_clear_background(int viewport_w, int viewport_h) {
  glViewport(0, 0, viewport_w, viewport_h);
  glClearColor(g_col_bg.r, g_col_bg.g, g_col_bg.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

float switch_chrome_list_width(int viewport_w) { return (float)viewport_w - kChromeListLeft * 2.0f; }

void switch_chrome_draw_header(int viewport_w, const std::string& title, const std::string& ctx) {
  switch_ui_draw_rect(0.0f, 0.0f, (float)viewport_w, (float)kChromeHeaderH, g_col_panel.r,
                       g_col_panel.g, g_col_panel.b, g_col_panel.a);
  switch_ui_draw_rect(0.0f, (float)kChromeHeaderH - 2.0f, (float)viewport_w, 2.0f, g_col_sel.r,
                       g_col_sel.g, g_col_sel.b, 1.0f);
  switch_ui_draw_text(kChromeListLeft, 26.0f, 38.0f, g_col_val.r, g_col_val.g, g_col_val.b, 1.0f,
                       title);
  if (!ctx.empty()) {
    const float w = switch_ui_text_width(ctx, 20.0f);
    switch_ui_draw_text((float)viewport_w - kChromeListLeft - w, 40.0f, 20.0f, g_col_dim.r,
                         g_col_dim.g, g_col_dim.b, 1.0f, ctx);
  }
}

void switch_chrome_draw_footer(int viewport_w, int viewport_h, const std::string& hint) {
  const float y = (float)(viewport_h - kChromeFooterH);
  switch_ui_draw_rect(0.0f, y, (float)viewport_w, (float)kChromeFooterH, g_col_panel.r,
                       g_col_panel.g, g_col_panel.b, g_col_panel.a);
  const float text_w = switch_ui_text_width(hint, 20.0f);
  switch_ui_draw_text(((float)viewport_w - text_w) * 0.5f, y + 18.0f, 20.0f, g_col_dim.r,
                       g_col_dim.g, g_col_dim.b, 1.0f, hint);
}

void switch_chrome_draw_highlight(float x, float y, float w, float h) {
  switch_ui_draw_rect(x, y, w, h, g_col_focus.r, g_col_focus.g, g_col_focus.b, g_col_focus.a);
  switch_ui_draw_rect(x, y, 5.0f, h, g_col_sel.r, g_col_sel.g, g_col_sel.b, 1.0f);
}

void switch_chrome_draw_row(float x, float w, float y, float h, bool selected,
                             const std::string& label, const std::string& value, int viewport_h) {
  const ChromeColor& lc = selected ? g_col_sel : g_col_txt;
  const float value_reserve = value.empty() ? 0.0f : switch_ui_text_width(value, 20.0f) + 16.0f;
  const float label_max_w = w - 48.0f - value_reserve;
  const float label_y = y + h * 0.5f - 14.0f;
  if (selected) {
    switch_ui_draw_text_marquee(x + 24.0f, label_y, label_max_w, 28.0f, 24.0f, lc.r, lc.g, lc.b,
                                 1.0f, label, viewport_h);
  } else {
    switch_ui_draw_text(x + 24.0f, label_y, 24.0f, lc.r, lc.g, lc.b, 1.0f,
                         switch_ui_ellipsize(label, 24.0f, label_max_w));
  }
  if (!value.empty()) {
    const float vw = switch_ui_text_width(value, 20.0f);
    const ChromeColor& vc = selected ? g_col_sel : g_col_dim;
    switch_ui_draw_text(x + w - 24.0f - vw, y + h * 0.5f - 12.0f, 20.0f, vc.r, vc.g, vc.b, 1.0f,
                         value);
  }
}

void switch_chrome_animate_to(float* state, float target) {
  if (!g_settings.ui_animations || *state < 0.0f) {
    *state = target;
  } else {
    *state += (target - *state) * 0.3f;
  }
}

void switch_chrome_draw_row_list(
    int viewport_w, int viewport_h, int row_count, int selected, int* scroll, float* highlight_y,
    const std::function<void(int index, std::string& label, std::string& value)>& row_text) {
  if (row_count <= 0) return;
  // Defensive: every current caller already keeps its own selection index
  // clamped after anything that can change row_count, but a future one
  // might not - an out-of-range `selected` would otherwise animate the
  // highlight to a y outside the panel entirely (never matching any row
  // actually drawn below) instead of just looking briefly wrong.
  selected = std::max(0, std::min(selected, row_count - 1));

  const float avail_h = (float)(viewport_h - kChromeFooterH) - kChromeListTop - 8.0f;
  const int max_visible = std::max(1, (int)(avail_h / kChromeRowHeight));
  const int visible = std::min(row_count, max_visible);

  // Keep `selected` scrolled into view - only moves *scroll when the
  // selection would otherwise land off-screen, not every frame, so the
  // window doesn't shift more than necessary. Also re-clamps against the
  // current row_count/visible in case either shrank since the last frame
  // (e.g. switching a setting that hides rows) and left a stale *scroll
  // pointing past the new end of the list.
  if (selected < *scroll) *scroll = selected;
  else if (selected >= *scroll + max_visible) *scroll = selected - max_visible + 1;
  *scroll = std::max(0, std::min(*scroll, row_count - visible));

  const float list_w = switch_chrome_list_width(viewport_w);
  switch_ui_draw_rect(kChromeListLeft, kChromeListTop, list_w, (float)visible * kChromeRowHeight,
                       g_col_panel.r, g_col_panel.g, g_col_panel.b, g_col_panel.a);

  switch_chrome_animate_to(highlight_y,
                            kChromeListTop + (float)(selected - *scroll) * kChromeRowHeight);
  switch_chrome_draw_highlight(kChromeListLeft, *highlight_y, list_w, kChromeRowHeight);

  const int last = *scroll + visible;
  for (int i = *scroll; i < last; i++) {
    const float y = kChromeListTop + (float)(i - *scroll) * kChromeRowHeight;
    std::string label, value;
    row_text(i, label, value);
    switch_chrome_draw_row(kChromeListLeft, list_w, y, kChromeRowHeight, i == selected, label,
                            value, viewport_h);
  }
}
