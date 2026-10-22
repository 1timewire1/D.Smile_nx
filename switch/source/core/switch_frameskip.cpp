#include "switch_frameskip.h"

#include <algorithm>
#include <cmath>

#include <switch.h>

#include "switch_settings.h"

namespace {

// Switch's display (handheld or docked) is 60Hz. NTSC runs one emulated
// frame per refresh; PAL (Library & Storage > Video Timing) runs 5 per 6, so
// each emulated frame gets 20 ms (switch_frameskip_set_pal()). A few
// hundredths of a percent off doesn't matter for a heuristic that's already
// re-averaging over dozens of frames and stepping gradually.
double kTargetFrameMs = 1000.0 / 60.0;

// How much a single frame's RunFrame() time influences the rolling
// average (exponential moving average) - low weight so one slow frame
// (a save-state write, a GC-like pause, whatever) can't itself trigger a
// skip-level change; only a sustained trend does.
constexpr double kAvgAlpha = 0.1;

// How often (in frames) the auto level is allowed to move, and by how much
// per reevaluation - deliberately slow/gradual (~2x/sec at 60fps, one level
// per step) so the skip level doesn't hunt/oscillate frame-to-frame.
constexpr int kReevalIntervalFrames = 30;

double g_avg_run_frame_ms = kTargetFrameMs;
int g_reeval_counter = 0;
int g_auto_level = 0;
int g_present_counter = 0;

// RunFrame() alone misses everything after it - a heavy shader, the
// Accurate renderer - and its own timing can't simply be added to the GL
// side's: when the emulator is ahead, the wait for vsync lands inside the GL
// calls, so "drawing took long" can mean either. So Auto also watches what
// it's for: emulated frames per real second, over each reevaluation window.
// Below kSlowSpeed, one more level is skipped. Coming back down is a probe -
// skipping hides how much headroom there is - tried after kProbeStart full-
// speed windows (3 s), and if speed drops again the wait doubles (up to
// kProbeMax, 48 s), so a level that's needed isn't flipped back every few
// seconds. A game at 58 fps is better shown in full than skipping, hence 0.95.
constexpr double kSlowSpeed = 0.95;
constexpr double kFullSpeed = 0.99;
constexpr int kProbeStart = 6, kProbeMax = 96;  // reevaluation windows
uint64_t g_window_start_tick = 0;
int g_window_frames = 0;
int g_full_windows = 0;
int g_probe_wait = kProbeStart;
bool g_probing = false;

// The skip level RunFrame()'s cost alone calls for.
int EmulationLevel() {
  // ratio > 1 means RunFrame() alone is already taking longer than a whole
  // target frame period - i.e. even with *zero* render/present cost we
  // couldn't hit 60fps, so skipping render on a proportional share of
  // frames is the only lever this feature has (it never touches RunFrame()
  // itself - see switch_frameskip.h).
  const double ratio = g_avg_run_frame_ms / kTargetFrameMs;
  if (ratio <= 1.05) return 0;
  return std::max(0, std::min(10, (int)std::ceil(ratio) - 1));
}

// Once per window: speed is emulated frames against the wall-clock time
// they took. One level per step either way, never below EmulationLevel().
void ReevaluateAutoLevel(double speed) {
  // RunFrame() alone too slow for kSlowSpeed: skipping drawing can't make up
  // the rest, so no levels beyond EmulationLevel()'s - they'd only show
  // fewer frames for nothing.
  const bool emulation_bound = kTargetFrameMs / g_avg_run_frame_ms < kSlowSpeed;
  if (speed < kSlowSpeed && !emulation_bound) {
    g_auto_level++;
    g_full_windows = 0;
    if (g_probing) g_probe_wait = std::min(g_probe_wait * 2, kProbeMax);  // that level was needed
    g_probing = false;
  } else {
    if (g_probing) g_probe_wait = kProbeStart;  // the lower level held
    g_probing = false;
    if (speed >= kFullSpeed && g_auto_level > EmulationLevel() && ++g_full_windows >= g_probe_wait) {
      g_auto_level--;
      g_full_windows = 0;
      g_probing = true;
    }
  }
  g_auto_level = std::max(EmulationLevel(), std::min(10, g_auto_level));
}

}  // namespace

void switch_frameskip_reset() {
  g_avg_run_frame_ms = kTargetFrameMs;
  g_reeval_counter = 0;
  g_auto_level = 0;
  g_present_counter = 0;
  g_window_start_tick = 0;
  g_window_frames = 0;
  g_full_windows = 0;
  g_probe_wait = kProbeStart;
  g_probing = false;
}

void switch_frameskip_set_pal(bool pal) { kTargetFrameMs = pal ? 1000.0 / 50.0 : 1000.0 / 60.0; }

void switch_frameskip_record_frame_time(double run_frame_ms) {
  if (g_settings.frame_skip_mode != "auto") return;  // no cost tracking a fixed/off level can't use
  g_avg_run_frame_ms = g_avg_run_frame_ms * (1.0 - kAvgAlpha) + run_frame_ms * kAvgAlpha;
  const uint64_t now = armGetSystemTick();
  if (g_window_start_tick == 0) {  // first frame since a reset: start timing here
    g_window_start_tick = now;
    g_window_frames = 0;
    return;
  }
  g_window_frames++;
  if (++g_reeval_counter >= kReevalIntervalFrames) {
    g_reeval_counter = 0;
    const double elapsed_ms = (double)armTicksToNs(now - g_window_start_tick) / 1e6;
    const double speed = elapsed_ms > 0 ? g_window_frames * kTargetFrameMs / elapsed_ms : 1.0;
    g_window_start_tick = now;
    g_window_frames = 0;
    ReevaluateAutoLevel(speed);
  }
}

bool switch_frameskip_should_render() {
  const int level = switch_frameskip_current_level();
  if (level <= 0) return true;
  // Present exactly 1 out of every (level+1) frames, evenly spaced by
  // construction - simpler than MAME's precomputed per-level pattern
  // tables, but the same practical effect (skips spread out rather than
  // bunched together, which is what actually looks/feels smooth).
  const bool render = (g_present_counter % (level + 1)) == 0;
  g_present_counter++;
  return render;
}

int switch_frameskip_current_level() {
  if (g_settings.frame_skip_mode == "manual") return g_settings.frame_skip_manual;
  if (g_settings.frame_skip_mode == "auto") return g_auto_level;
  return 0;
}
