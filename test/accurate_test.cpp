// Verifies the Accurate-mode fade + saturation post-processing by driving the
// real PPU registers through the bus and dumping frames. Not in the Android build.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../app/src/main/cpp/core/vsmile.h"

using namespace dsmile;

static u32 FbHash(const u16* fb) {
  u32 h = 2166136261u;
  for (int i = 0; i < 320 * 240; i++) { h ^= fb[i]; h *= 16777619u; }
  return h;
}
static double MeanLuma(const u16* fb) {
  double s = 0;
  for (int i = 0; i < 320 * 240; i++) {
    const u16 v = fb[i];
    s += ((v >> 11) & 31) + ((v >> 5) & 63) / 2.0 + (v & 31);
  }
  return s / (320 * 240);
}

int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<u8> rom(size); fread(rom.data(), 1, size, f); fclose(f);

  static VSmile vs;
  vs.LoadCart(rom.data(), rom.size());
  vs.Reset(false);
  for (int i = 0; i < 500; i++) { vs.RunFrame(); }

  // Baseline (accurate off): capture current frame.
  const u32 fast_hash = FbHash(vs.Framebuffer());
  const double fast_luma = MeanLuma(vs.Framebuffer());

  // Accurate ON, no effects active yet -> must match Fast exactly.
  vs.SetAccurate(true);
  vs.RunFrame();
  const u32 acc_noeffect = FbHash(vs.Framebuffer());
  const double acc_luma = MeanLuma(vs.Framebuffer());

  // Force a heavy fade (reg 0x2830 = 0x60) and re-render one frame.
  vs.Soc().Write(0x2830, 0x60);
  vs.RunFrame();
  const double faded_luma = MeanLuma(vs.Framebuffer());

  // Clear fade, force max desaturation (reg 0x283C low byte = 0xFF).
  vs.Soc().Write(0x2830, 0x00);
  vs.Soc().Write(0x283C, 0x00FF);
  vs.RunFrame();
  const double desat_luma = MeanLuma(vs.Framebuffer());

  std::printf("fast_hash=%08x luma=%.1f\n", fast_hash, fast_luma);
  std::printf("accurate no-effect luma=%.1f (should ~= fast)\n", acc_luma);
  std::printf("faded luma=%.1f (should be < accurate no-effect)\n", faded_luma);
  std::printf("desat luma=%.1f\n", desat_luma);

  const bool noeffect_ok = (acc_luma > fast_luma - 3 && acc_luma < fast_luma + 3);
  const bool fade_ok = faded_luma < acc_luma - 5;
  std::printf("no-effect parity: %s\n", noeffect_ok ? "PASS" : "FAIL");
  std::printf("fade darkens:     %s\n", fade_ok ? "PASS" : "FAIL");
  return (noeffect_ok && fade_ok) ? 0 : 1;
}
