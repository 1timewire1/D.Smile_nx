#pragma once
#include <cstdint>

// Draws the V.Smile's 320x240 RGB565 framebuffer with the same visual
// options Android's GameRenderer.kt offers - shader mode (pixel/sharp/CRT),
// aspect ratio (4:3/stretch/integer), letterbox background, and TV bezel -
// read each frame from g_settings' Graphics section (switch_settings.h),
// same as GameRenderer.kt reads its own mutable fields every onDrawFrame.
// The "two renderer options" (fast/accurate) aren't a rendering concern
// here - that's VSmile::SetAccurate(), applied by main.cpp's LoadGame() at
// load time, same as Android applies it via nativeSetAccurate().
bool switch_render_init(int viewport_w, int viewport_h);
void switch_render_frame(const uint16_t* framebuffer565);
void switch_render_shutdown();
