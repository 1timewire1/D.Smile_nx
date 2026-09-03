#pragma once
#include <functional>
#include <string>

// Shared visual chrome for every menu-ish screen (game grid, settings,
// folder list, directory browser): header band, footer hint bar, a
// selectable row inside a "glass" panel, and the current theme's palette.
// Factored out of switch_menu.cpp once a second screen (the directory
// browser) needed the exact same look.

struct ChromeColor { float r, g, b, a; };

// Current theme's palette (default "vsmile" until switch_chrome_set_theme
// picks another - see switch/README.md's theme list). Read directly by
// callers that need a color switch_chrome's own helpers don't cover.
extern ChromeColor g_col_bg, g_col_txt, g_col_dim, g_col_val, g_col_sel, g_col_panel, g_col_focus;

// Sets the palette from a theme key: "vsmile" (default - the console/
// controller's own purple-and-orange), "animated" ("Glow"), "xmb",
// "classic", "oled", "homebrew" ("Bubbles") - the last 4 (plus "animated")
// match DraStic's own Wrapper/Theme values, so switch/settings.ini stays
// consistent with what the Android/DraStic side would use for the same
// concept. Unknown keys fall back to "animated".
void switch_chrome_set_theme(const std::string& theme);

constexpr float kChromeListLeft = 96.0f;
constexpr float kChromeListTop = 116.0f;
constexpr float kChromeRowHeight = 48.0f;
constexpr int kChromeHeaderH = 92;
constexpr int kChromeFooterH = 60;

float switch_chrome_list_width(int viewport_w);

// glViewport + glClearColor(g_col_bg) + glClear, so every screen's
// background actually follows the current theme - this was missing before
// (screens hardcoded a color instead), which is why the OLED theme's
// background didn't turn black even though its panels correctly did.
void switch_chrome_clear_background(int viewport_w, int viewport_h);

// ctx, if non-empty, is drawn right-aligned in the same header band (e.g.
// "12 / 47 - Page 1/4 - Sort: A-Z" on the game grid).
void switch_chrome_draw_header(int viewport_w, const std::string& title,
                                const std::string& ctx = "");
void switch_chrome_draw_footer(int viewport_w, int viewport_h, const std::string& hint);

// The focus fill + accent stripe for one row, drawn separately from its
// text (switch_chrome_draw_row below) so the fill can be positioned at an
// animated y (switch_chrome_animate_to's *state) while the row's own text
// stays snapped to its real grid position - the same "sliding highlight
// tray, static list" split DraStic's own g_hy does.
void switch_chrome_draw_highlight(float x, float y, float w, float h);

// A row's text inside a glass panel: a left-aligned label and a
// right-aligned value (value may be empty). `selected` only affects text
// color and how an overlong label is handled here - draw
// switch_chrome_draw_highlight separately for the fill. A label too wide
// for the row is ellipsized when unselected, or slides via
// switch_ui_draw_text_marquee when selected (so the row you're actually
// looking at is the one that becomes fully readable) - viewport_h is needed
// for the marquee's clip rect.
void switch_chrome_draw_row(float x, float w, float y, float h, bool selected,
                             const std::string& label, const std::string& value, int viewport_h);

// Lerps *state toward target (DraStic's sliding highlight), or snaps
// instantly when Settings > Launcher > UI animations is off. Each screen
// owns its own state float (reset to -1 on entering the screen so the very
// first frame snaps rather than sliding in from wherever a previous screen
// left off).
void switch_chrome_animate_to(float* state, float target);

// The standard scrollable row list every settings-style screen wants:
// draws the glass panel sized to whatever fits the available height, keeps
// `selected` scrolled into view (only moving the window when the selection
// would otherwise go off-screen, not every frame), and draws the sliding
// highlight + each visible row's text. Existing screens used to each
// hand-roll their own panel+highlight+loop, and several of them forgot the
// "keep selected in view" part entirely once they grew past what fits on
// one screen (e.g. Settings > Controller's 14 rows) - route every row list
// through this one audited implementation instead of repeating that.
//
// `scroll` and `highlight_y` are the caller's own persistent per-screen
// state - a plain int and a float. Reset both (scroll to 0, highlight_y to
// -1) when entering the screen or whenever its row count changes, same as
// every screen already resets highlight_y on a screen transition.
//
// `row_text(index, label, value)` fills in one row's text; value may be
// left empty. Called only for rows actually on screen this frame, not the
// full list, so it's fine for it to do real formatting work.
void switch_chrome_draw_row_list(
    int viewport_w, int viewport_h, int row_count, int selected, int* scroll, float* highlight_y,
    const std::function<void(int index, std::string& label, std::string& value)>& row_text);
