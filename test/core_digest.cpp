// Desktop tool: runs the core deterministically and prints one FNV-1a digest
// covering every frame's framebuffer, every audio sample and the final save
// state. Two builds of the core that print the same digest for the same
// arguments behave identically - how the 1.2.0 performance work was checked
// to be bit-exact (see switch/README.md, "Performance pass (1.2.0)"). Not
// part of the Android or Switch builds.
//
//   core_digest <rom.bin> <bios.bin> <frames> <joy|mot|motr|kbd|2p>
//
//   joy   joystick, pseudo-random stick and buttons every 17 frames
//   mot   Motion controller and console, tilt following the stick
//   motr  Motion, with random full-range tilt every frame
//   kbd   Smart Keyboard, random keys
//   2p    two joysticks
// Environment: DSMILE_PAL=1 for PAL timing; CHECK_SLEEP=1 to press OFF
// halfway through, then ON (standby and wakeup).
//
// Build, e.g.: g++ -std=c++20 -O2 -I../app/src/main/cpp/core core_digest.cpp
//   ../app/src/main/cpp/core/{vsmile,spg200,ppu,spu,unsp}.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../app/src/main/cpp/core/vsmile.h"

using namespace dsmile;

static std::vector<u8> ReadFile(const char* path) {
  std::vector<u8> d;
  FILE* f = fopen(path, "rb");
  if (!f) return d;
  fseek(f, 0, SEEK_END);
  d.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
  fclose(f);
  return d;
}

static u64 Fnv(u64 h, const void* p, size_t n) {
  const u8* b = (const u8*)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::printf("usage: core_digest <rom> <bios> <frames> <joy|mot|motr|kbd|2p>\n");
    return 1;
  }
  const std::vector<u8> rom = ReadFile(argv[1]);
  const std::vector<u8> bios = ReadFile(argv[2]);
  if (rom.empty() || bios.empty()) {
    std::printf("cannot read rom or bios\n");
    return 1;
  }
  const int frames = std::atoi(argv[3]);
  const char* mode = argv[4];
  const bool motr = !std::strcmp(mode, "motr");
  const bool mot = motr || !std::strcmp(mode, "mot");
  const bool kbd = !std::strcmp(mode, "kbd");
  const bool two = !std::strcmp(mode, "2p");
  const bool sleep_test = std::getenv("CHECK_SLEEP") != nullptr;

  static VSmile vs;
  vs.LoadCart(rom.data(), rom.size());
  vs.LoadSysrom(bios.data(), bios.size());
  vs.SetPort1Device(mot ? PortDevice::Motion : kbd ? PortDevice::SmartKeyboard : PortDevice::Joystick, 0x40);
  vs.SetMotionConsole(mot);
  if (two) vs.SetPort2Device(PortDevice::Joystick);
  vs.Reset(std::getenv("DSMILE_PAL") != nullptr);

  u64 h = 1469598103934665603ull;
  u32 rng = 777;
  int jx = 0, jy = 0;
  u32 b = 0;
  std::vector<s16> audio(8192);
  for (int f = 0; f < frames; f++) {
    if (f % 17 == 0) {
      rng = rng * 1103515245u + 12345u;
      jx = (int)((rng >> 8) % 11) - 5;
      jy = (int)((rng >> 12) % 11) - 5;
      b = ((rng >> 20) % 4 == 0) ? (1u << ((rng >> 24) % 3)) : 0;
      if (kbd) vs.KeyboardKey((u8)(0x10 + (rng >> 16) % 40), (rng >> 4) & 1);
    }
    vs.SetInput(jx, jy, b);
    if (motr) {
      rng = rng * 1103515245u + 12345u;
      vs.SetMotion((int)((rng >> 8) & 0xFF), (int)((rng >> 16) & 0xFF), (int)((rng >> 24) & 0xFF));
    } else if (mot) {
      vs.SetMotion(0x80 + jx * 9, 0x80 + jy * 9, 0xC0);
    }
    if (two) {
      vs.SetPlayer2Connected(true);
      vs.SetInput2(-jy, jx, b);
    }
    if (sleep_test) {
      if (f == frames / 2) vs.SetConsoleButtons(false, true, false);         // OFF
      if (f == frames / 2 + 30) vs.SetConsoleButtons(false, false, false);
      if (f == frames / 2 + 200) vs.SetConsoleButtons(true, false, false);   // ON
      if (f == frames / 2 + 230) vs.SetConsoleButtons(false, false, false);
    }
    vs.RunFrame();
    h = Fnv(h, vs.Framebuffer(), 320 * 240 * 2);
    int n;
    while ((n = vs.DrainAudio(audio.data(), (int)audio.size())) > 0) h = Fnv(h, audio.data(), n * 2);
  }
  std::vector<u8> state;
  vs.SaveState(state);
  h = Fnv(h, state.data(), state.size());
  std::printf("%016llx\n", (unsigned long long)h);
  return 0;
}
