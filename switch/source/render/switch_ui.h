#pragma once
#include <string>

// Small immediate-mode 2D helper for the ROM browser: filled rects and text,
// in absolute pixel space (top-left origin). Text is rendered with FreeType
// against Switch's own shared system font (plGetSharedFontByType) rather
// than a bundled bitmap font, so it renders correctly regardless of what
// characters a filename needs - same approach as the Yokoi reference port's
// switch_ui.cpp, trimmed down to just what a plain list needs (no button
// icons, no texture blitting for thumbnails).
//
// Owns its own GL program/VAO/texture, separate from switch_render.cpp's -
// the in-game renderer is proven working and this keeps the menu from being
// able to disturb its GL state.

bool switch_ui_init();
void switch_ui_shutdown();

// Call once per frame before any switch_ui_draw_* calls.
void switch_ui_begin_frame(int viewport_w, int viewport_h);

void switch_ui_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a);

// Blits an already-uploaded GL texture (e.g. a decoded cover) into a
// pixel-space rect at full opacity, no cropping - the whole texture stretched
// to fill (x,y,w,h).
void switch_ui_draw_texture(uint32_t gl_tex, float x, float y, float w, float h);

// (x,y) = top-left of the line. pixel_size is the glyph height in pixels;
// glyphs are cached per exact pixel size the first time they're drawn.
void switch_ui_draw_text(float x, float y, float pixel_size, float r, float g, float b, float a,
                          const std::string& text);
float switch_ui_text_width(const std::string& text, float pixel_size);

// Trims `text` with a trailing "..." so it fits within max_width at the
// given pixel size (DraStic's ellipsizedText()). Returns it unchanged if it
// already fits.
std::string switch_ui_ellipsize(const std::string& text, float pixel_size, float max_width);

// Left-aligned text clipped to the pixel-space box (x,y,w,h). Text that
// fits is just drawn (vertically centered in the box); text that doesn't
// slides left then back on a 6-second ping-pong loop instead of being cut
// off (DraStic's drawScrollTextL - used for whichever row/tile is currently
// selected, where switch_ui_ellipsize's truncation would hide the second
// half of a name that's exactly what the user is looking at). viewport_h is
// needed to convert the box into GL's bottom-up scissor coordinates.
void switch_ui_draw_text_marquee(float x, float y, float w, float h, float pixel_size, float r,
                                  float g, float b, float a, const std::string& text,
                                  int viewport_h);
