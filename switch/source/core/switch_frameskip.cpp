#include "switch_frameskip.h"

#include <algorithm>
#include <cmath>

#include "switch_settings.h"

namespace {

// Switch's display (handheld or docked) is 60Hz, and this project always
// runs V.Smile carts as NTSC (see main.cpp's Reset(/*pal=*/false)), so a
// single fixed target is close enough - a few hundredths of a percent off
// doesn't matter for a heuristic that's already re-averaging over dozens of
// frames and stepping gradually.
constexpr double kTargetFrameMs = 1000.0 / 60.0;

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

void ReevaluateAutoLevel() {
  // ratio > 1 means RunFrame() alone is already taking longer than a whole
  // target frame period - i.e. even with *zero* render/present cost we
  // couldn't hit 60fps, so skipping render on a proportional share of
  // frames is the only lever this feature has (it never touches RunFrame()
  // itself - see switch_frameskip.h).
  const double ratio = g_avg_run_frame_ms / kTargetFrameMs;
  int desired = 0;
  if (ratio > 1.05) {
    desired = std::max(0, std::min(10, (int)std::ceil(ratio) - 1));
  }
  if (desired > g_auto_level) g_auto_level++;
  else if (desired < g_auto_level) g_auto_level--;
}

}  // namespace

void switch_frameskip_reset() {
  g_avg_run_frame_ms = kTargetFrameMs;
  g_reeval_counter = 0;
  g_auto_level = 0;
  g_present_counter = 0;
}

void switch_frameskip_record_frame_time(double run_frame_ms) {
  if (g_settings.frame_skip_mode != "auto") return;  // no cost tracking a fixed/off level can't use
  g_avg_run_frame_ms = g_avg_run_frame_ms * (1.0 - kAvgAlpha) + run_frame_ms * kAvgAlpha;
  if (++g_reeval_counter >= kReevalIntervalFrames) {
    g_reeval_counter = 0;
    ReevaluateAutoLevel();
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
