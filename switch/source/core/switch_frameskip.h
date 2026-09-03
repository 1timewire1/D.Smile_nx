#pragma once

// MAME-style frame skipping: VSmile::RunFrame() (game logic + audio) always
// runs at full rate every frame, whether or not that frame ends up on
// screen - only the GL upload+draw+eglSwapBuffers cost gets skipped on
// skipped frames, so gameplay speed and audio stay correct regardless of
// video framerate. The V.Smile core this project is built from was itself
// verified against MAME's SPG2xx driver (see the main README's
// acknowledgements), so the resemblance in spirit here is deliberate -
// though this is a from-scratch reimplementation of the *idea* (measure
// speed, skip a proportional and evenly-spaced share of frames, adjust
// gradually), not a port of MAME's own frameskip tables/thresholds, which
// weren't available to reference directly.
//
// Settings > Graphics > Frame Skip has three modes (g_settings.frame_skip_mode):
//   off    - never skip, every frame renders (this project's behavior
//            before this feature existed).
//   manual - always skip g_settings.frame_skip_manual out of every
//            (N+1) frames, evenly spaced.
//   auto   - same idea, but N is instead adjusted automatically based on
//            how long VSmile::RunFrame() has actually been taking, so it
//            tracks whatever the current game/hardware/other settings
//            (e.g. Accurate renderer, CRT shader) actually cost right now.

// Resets all timing history and the auto-adjusted level back to "no skip
// assumed yet" - call whenever gameplay resumes after a gap that isn't
// representative of real emulation cost (a fresh game load, or unpausing -
// see main.cpp), so that gap itself never gets mistaken for the emulator
// suddenly running slow.
void switch_frameskip_reset();

// Call once per frame with how long VSmile::RunFrame() actually took (just
// that call - not audio/render/present), in milliseconds. Feeds the
// rolling average auto mode reevaluates against periodically.
void switch_frameskip_record_frame_time(double run_frame_ms);

// Call once per frame, after switch_frameskip_record_frame_time(), to
// decide whether *this* frame should be rendered and presented. Always
// true when Frame Skip is Off. Internally advances the even-spacing
// counter, so call it exactly once per frame - not for probing.
bool switch_frameskip_should_render();

// The frame-skip level (0-10) actually in effect right now - g_settings.
// frame_skip_manual in manual mode, the live auto-adjusted value in auto
// mode, 0 when off. For Settings > Graphics to show auto's current value.
int switch_frameskip_current_level();
