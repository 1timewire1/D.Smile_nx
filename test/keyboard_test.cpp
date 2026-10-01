// Desktop test for the Smart Keyboard port device. Not part of the Android
// or Switch builds.
//
//   keyboard_test <rom.bin> <bios.bin|-> <kbd[lang]|joy|mot|mat> <out_prefix> [event... | @file]
//   (kbd42 = Smart Keyboard reporting language byte 0x42; plain kbd = 0x40)
//
// Events run at a frame number, in order:
//   <frame>:shot         save <out_prefix>_<frame>.bmp
//   <frame>:key<hex>+    Smart Keyboard key down (e.g. 120:key1b+ = A)
//   <frame>:key<hex>-    Smart Keyboard key up
//   <frame>:btn<n>       pad buttons mask (bit0 OK, bit1 Quit, bit2 Help)
//   <frame>:joy<x>,<y>   pad joystick, -5..5
//   <frame>:kbd / :joy   re-plug port 1 as the Smart Keyboard / joystick
//   <frame>:tilt<x>,<y>,<z>  Motion controller accelerometer bytes (hex)
//   <frame>:mcon         emulate the Motion console board (implied by "mot")
//   <frame>:p2joy / :p2mot   connect a joystick / Motion controller on port 2
//   <frame>:p2btn<n> / :p2stick<x>,<y> / :p2tilt<x>,<y>,<z>   port 2 input
//   <frame>:cb<on><off><restart>   hold console buttons, e.g. 0:cb111 (all three) / 90:cb000
//   <frame>:leds         print the controller LED bits
//   <frame>:sleep        print whether the console is in standby
//   <frame>:restart      press RESTART (hardware reset that keeps RAM)
//   <frame>:load<path>   load a save state file (e.g. one written by :save)
//   <frame>:end          stop
// Build with -DDSMILE_TRACE to see the UART traffic.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../app/src/main/cpp/core/vsmile.h"

using namespace dsmile;

static void WriteBmp(const char* path, const u16* fb565) {
  const int w = 320, h = 240;
  const int row = w * 3;
  const int size = 54 + row * h;
  std::vector<u8> bmp(size, 0);
  u8* p = bmp.data();
  p[0] = 'B'; p[1] = 'M';
  *(u32*)(p + 2) = size;
  *(u32*)(p + 10) = 54;
  *(u32*)(p + 14) = 40;
  *(s32*)(p + 18) = w;
  *(s32*)(p + 22) = -h;  // top-down
  *(u16*)(p + 26) = 1;
  *(u16*)(p + 28) = 24;
  u8* px = p + 54;
  for (int i = 0; i < w * h; i++) {
    const u16 v = fb565[i];
    px[i * 3 + 0] = (u8)((v & 31) << 3);
    px[i * 3 + 1] = (u8)(((v >> 5) & 63) << 2);
    px[i * 3 + 2] = (u8)(((v >> 11) & 31) << 3);
  }
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fwrite(bmp.data(), 1, bmp.size(), f);
  fclose(f);
}

static std::vector<u8> ReadFile(const char* path) {
  std::vector<u8> data;
  FILE* f = fopen(path, "rb");
  if (!f) return data;
  fseek(f, 0, SEEK_END);
  data.resize((size_t)ftell(f));
  fseek(f, 0, SEEK_SET);
  if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
  fclose(f);
  return data;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::printf("usage: keyboard_test <rom> <bios|-> <kbd|joy> <out_prefix> [event...]\n");
    return 1;
  }
  const std::vector<u8> rom = ReadFile(argv[1]);
  if (rom.empty()) { std::printf("cannot read rom\n"); return 1; }
  static VSmile vs;
  if (!vs.LoadCart(rom.data(), rom.size())) { std::printf("LoadCart failed\n"); return 1; }
  if (std::strcmp(argv[2], "-") != 0) {
    const std::vector<u8> bios = ReadFile(argv[2]);
    if (bios.empty()) { std::printf("cannot read bios\n"); return 1; }
    vs.LoadSysrom(bios.data(), bios.size());
  }
  const bool kbd = std::strncmp(argv[3], "kbd", 3) == 0;
  const bool mot = std::strcmp(argv[3], "mot") == 0;  // Motion controller (+ Motion console)
  const bool mat = std::strcmp(argv[3], "mat") == 0;  // Jammin' Gym Class dance mat (pad input remapped)
  const u8 lang = (kbd && argv[3][3]) ? (u8)std::strtol(argv[3] + 3, nullptr, 16) : 0x40;
  vs.SetPort1Device(kbd ? PortDevice::SmartKeyboard : mot ? PortDevice::Motion : mat ? PortDevice::Mat : PortDevice::Joystick,
                    lang);
  vs.SetMotionConsole(mot);
  // DSMILE_REGION=<hex> sets the region/language jumpers (default F).
  if (const char* r = std::getenv("DSMILE_REGION")) vs.SetRegion((int)std::strtol(r, nullptr, 16));
  // DSMILE_CONSOLE=std|motion|cyber overrides the console type (port A straps).
  if (const char* c = std::getenv("DSMILE_CONSOLE")) {
    vs.SetConsoleType(c[0] == 'm' ? ConsoleType::Motion : c[0] == 'c' ? ConsoleType::CyberPocket
                                                                     : ConsoleType::Standard);
  }
  // Environment switches: DSMILE_PAL=1 for PAL timing, DSMILE_NOLOGO=1 for the
  // "no VTech logo" jumper (the Switch port's Library & Storage settings).
  vs.SetVtechLogo(!std::getenv("DSMILE_NOLOGO"));
  vs.Reset(std::getenv("DSMILE_PAL") != nullptr);
  const std::string prefix = argv[4];

  // Events come from the command line, or from a file for long scripts
  // ("@events.txt", whitespace-separated) - Windows caps a command line at
  // about 32K characters.
  std::vector<std::string> events;
  for (int a = 5; a < argc; a++) {
    if (argv[a][0] == '@') {
      FILE* ef = fopen(argv[a] + 1, "r");
      char tok[256];
      while (ef && std::fscanf(ef, "%255s", tok) == 1) events.push_back(tok);
      if (ef) fclose(ef);
    } else {
      events.push_back(argv[a]);
    }
  }

  int joy_x = 0, joy_y = 0;
  u32 buttons = 0;
  int joy2_x = 0, joy2_y = 0;  // port 2, once connected by a p2... event
  u32 buttons2 = 0;
  bool p2 = false;
  int frame = 0;
  for (const std::string& event : events) {
    const int at = std::atoi(event.c_str());
    const char* ev = std::strchr(event.c_str(), ':');
    if (!ev) continue;
    ev++;
    while (frame < at) {
      vs.SetInput(joy_x, joy_y, buttons);
      vs.SetPlayer2Connected(p2);
      if (p2) vs.SetInput2(joy2_x, joy2_y, buttons2);
      vs.RunFrame();
      frame++;
    }
    std::printf("== frame %d: %s\n", frame, ev);
    if (std::strcmp(ev, "shot") == 0) {
      WriteBmp((prefix + "_" + std::to_string(frame) + ".bmp").c_str(), vs.Framebuffer());
    } else if (std::strncmp(ev, "key", 3) == 0) {
      const u8 code = (u8)std::strtol(ev + 3, nullptr, 16);
      vs.KeyboardKey(code, ev[std::strlen(ev) - 1] == '+');
    } else if (std::strcmp(ev, "p2joy") == 0 || std::strcmp(ev, "p2mot") == 0) {
      p2 = true;  // plug a joystick / Motion controller into port 2
      vs.SetPort2Device(ev[2] == 'm' ? PortDevice::Motion : PortDevice::Joystick);
    } else if (std::strncmp(ev, "p2btn", 5) == 0) {
      buttons2 = (u32)std::atoi(ev + 5);
    } else if (std::strncmp(ev, "p2stick", 7) == 0) {
      std::sscanf(ev + 7, "%d,%d", &joy2_x, &joy2_y);
    } else if (std::strncmp(ev, "p2tilt", 6) == 0) {
      int x = 0x80, y = 0x80, z = 0xC0;
      std::sscanf(ev + 6, "%x,%x,%x", &x, &y, &z);
      vs.SetMotion2(x, y, z);
    } else if (std::strncmp(ev, "btn", 3) == 0) {
      buttons = (u32)std::atoi(ev + 3);
    } else if (std::strcmp(ev, "kbd") == 0 || std::strcmp(ev, "joy") == 0) {
      vs.SetPort1Device(ev[0] == 'k' ? PortDevice::SmartKeyboard : PortDevice::Joystick, lang);
    } else if (std::strncmp(ev, "joy", 3) == 0) {
      std::sscanf(ev + 3, "%d,%d", &joy_x, &joy_y);
#ifdef DSMILE_TEST_HOOKS
    } else if (std::strncmp(ev, "ww", 2) == 0) {  // e.g. 3690:ww0077-0077 (test builds only)
      std::sscanf(ev + 2, "%x-%x", &g_test_wwatch_lo, &g_test_wwatch_hi);
    } else if (std::strncmp(ev, "rw", 2) == 0) {  // e.g. 4000:rw0b57-0b76 (test builds only)
      std::sscanf(ev + 2, "%x-%x", &g_test_rwatch_lo, &g_test_rwatch_hi);
    } else if (std::strncmp(ev, "bpoff", 5) == 0) {  // e.g. 0:bpoff6 (test builds only)
      g_test_watch_bpoff = (int)std::strtol(ev + 5, nullptr, 16);
    } else if (std::strncmp(ev, "mem", 3) == 0) {  // e.g. 0:mem20d7 (test builds only)
      g_test_watch_mem = (int)std::strtol(ev + 3, nullptr, 16);
    } else if (std::strncmp(ev, "period", 6) == 0) {  // e.g. 0:period1800000 (test builds only)
      g_test_motion_period = std::atoll(ev + 6);
    } else if (std::strncmp(ev, "watch", 5) == 0) {  // e.g. 0:watch43f27 (test builds only)
      if (g_test_watch_count < 16) g_test_watch_pc[g_test_watch_count++] = (u32)std::strtoul(ev + 5, nullptr, 16);
#endif
    } else if (std::strncmp(ev, "tilt", 4) == 0) {  // e.g. 4500:tilt98,80,c0 (Motion accelerometer, hex)
      int x = 0x80, y = 0x80, z = 0xC0;
      std::sscanf(ev + 4, "%x,%x,%x", &x, &y, &z);
      vs.SetMotion(x, y, z);
    } else if (std::strcmp(ev, "ram") == 0) {  // dump work RAM to <prefix>_<frame>.ram
      FILE* rf = fopen((prefix + "_" + std::to_string(frame) + ".ram").c_str(), "wb");
      if (rf) { fwrite(vs.Soc().DebugRam(), 2, 0x2800, rf); fclose(rf); }
    } else if (std::strcmp(ev, "save") == 0) {  // write a save state to <prefix>_<frame>.dss
      std::vector<u8> st;
      vs.SaveState(st);
      FILE* sf = fopen((prefix + "_" + std::to_string(frame) + ".dss").c_str(), "wb");
      if (sf) { fwrite(st.data(), 1, st.size(), sf); fclose(sf); }
    } else if (std::strncmp(ev, "cb", 2) == 0 && std::strlen(ev) == 5) {  // e.g. 0:cb111 (ON, OFF, RESTART held)
      vs.SetConsoleButtons(ev[2] == '1', ev[3] == '1', ev[4] == '1');
    } else if (std::strcmp(ev, "restart") == 0) {  // RESTART button: hardware reset, RAM kept
      vs.Restart();
    } else if (std::strcmp(ev, "sleep") == 0) {  // print whether the console is in standby
      std::printf("sleeping=%d\n", vs.Sleeping() ? 1 : 0);
    } else if (std::strcmp(ev, "leds") == 0) {  // print the controller LED bits (6x commands)
      std::printf("leds p1=%X p2=%X\n", vs.Leds(), vs.Leds2());
    } else if (std::strncmp(ev, "load", 4) == 0) {  // e.g. 1:loadC:/x/state.dss - LoadState a file
      const std::vector<u8> st = ReadFile(ev + 4);
      std::printf("load %s: %s\n", ev + 4, (!st.empty() && vs.LoadState(st.data(), st.size())) ? "ok" : "FAILED");
    } else if (std::strcmp(ev, "mcon") == 0) {  // emulate the V.Smile Motion console board
      vs.SetMotionConsole(true);
    } else if (std::strcmp(ev, "end") == 0) {
      break;
    }
  }
  return 0;
}
