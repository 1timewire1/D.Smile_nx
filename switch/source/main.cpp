// D.Smile Switch port - main loop.
//
// Two states: a ROM browser (list *.bin files and *.zip archives under
// sdmc:/switch/dsmile/games, pick one with A) and gameplay. Single-threaded,
// modeled on reference/Yokoi_Game_-_Watch_emulator_3ds-main's switch/source/
// main.cpp (poll input -> step one frame -> render -> play audio -> swap),
// confirmed working on real Switch hardware with this same devkitA64 +
// libnx + switch-glad + switch-mesa toolchain. Unlike Android's
// jni_bridge.cpp, there's no separate emulation thread or Oboe
// pull-callback here - vsync from eglSwapBuffers paces the loop directly,
// same as Yokoi's.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <sys/stat.h>
#include <vector>

#include <switch.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

#include "vsmile.h"

#include "audio/switch_audio.h"
#include "core/switch_bios.h"
#include "core/switch_frameskip.h"
#include "core/switch_menu.h"
#include "core/switch_present.h"
#include "core/switch_settings.h"
#include "core/switch_zip.h"
#include "input/switch_input.h"
#include "render/switch_chrome.h"
#include "render/switch_render.h"
#include "render/switch_ui.h"

namespace {

constexpr int kViewportW = 1280, kViewportH = 720;

// One NTSC frame of SPU output is ~4682 stereo pairs at 281250 Hz; leave
// headroom (matches Android jni_bridge.cpp's kSpuBufMax).
constexpr int kSpuBufMax = 32768;
int16_t g_spu_buf[kSpuBufMax];

double NowMs() { return (double)armTicksToNs(armGetSystemTick()) / 1'000'000.0; }

// Rewind (Settings > Controller's Rewind hotkey, default hold StickL - no
// pause-menu toggle exists for it, matching Android exactly, which doesn't
// have one either) - a ring buffer of one VSmile::SaveState() snapshot per
// simulated frame, same design as Android's jni_bridge.cpp: popping and
// loading the most recent snapshot each rewinding frame steps the machine
// back exactly one recorded frame at a time (the very first rewind frame
// reloads the *current* state as a harmless no-op, since the last snapshot
// pushed was taken right after the frame that just played - see the
// pop/load/RunFrame ordering below, which matches jni_bridge.cpp's Loop()
// precisely for this reason). ~15s of history at 60fps, same cap Android
// uses (kRewindMax there).
constexpr size_t kRewindMax = 900;

enum class AppState { RomList, Playing, Paused };

EGLDisplay s_display = nullptr;
EGLContext s_context = nullptr;
EGLSurface s_surface = nullptr;

bool InitEgl(NWindow* win, int fixed_w, int fixed_h) {
  // eglQuerySurface(EGL_WIDTH/EGL_HEIGHT) reports 0x0 on Switch's Mesa/nouveau
  // EGL backend for NWindow-backed surfaces, so the surface size has to come
  // from here instead - same fix the Yokoi reference port needed after
  // hitting the identical black-screen bug on hardware.
  nwindowSetDimensions(win, fixed_w, fixed_h);

  s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!s_display) return false;

  eglInitialize(s_display, nullptr, nullptr);
  if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    eglTerminate(s_display);
    s_display = nullptr;
    return false;
  }

  static const EGLint kFbAttribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 0,
      EGL_STENCIL_SIZE, 0,
      EGL_NONE,
  };
  EGLConfig config;
  EGLint num_configs = 0;
  eglChooseConfig(s_display, kFbAttribs, &config, 1, &num_configs);
  if (num_configs == 0) {
    eglTerminate(s_display);
    s_display = nullptr;
    return false;
  }

  s_surface = eglCreateWindowSurface(s_display, config, win, nullptr);
  if (!s_surface) {
    eglTerminate(s_display);
    s_display = nullptr;
    return false;
  }

  static const EGLint kCtxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, kCtxAttribs);
  if (!s_context) {
    eglDestroySurface(s_display, s_surface);
    s_surface = nullptr;
    eglTerminate(s_display);
    s_display = nullptr;
    return false;
  }

  eglMakeCurrent(s_display, s_surface, s_surface, s_context);
  eglSwapInterval(s_display, 1);
  return true;
}

void DeinitEgl() {
  if (!s_display) return;
  eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (s_context) {
    eglDestroyContext(s_display, s_context);
    s_context = nullptr;
  }
  if (s_surface) {
    eglDestroySurface(s_display, s_surface);
    s_surface = nullptr;
  }
  eglTerminate(s_display);
  s_display = nullptr;
}

void RunConsoleErrorLoop(const char* msg) {
  consoleInit(nullptr);
  printf("%s\n\nPress + to exit.\n", msg);
  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(nullptr);
  }
  consoleExit(nullptr);
}

bool LoadFile(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    fclose(f);
    return false;
  }
  out.resize((size_t)size);
  const size_t n = fread(out.data(), 1, out.size(), f);
  fclose(f);
  return n == out.size();
}

// Cart-side NVRAM save file path - named after the cart like save states
// are (switch_settings_state_file_path()), but with no slot number: real
// V.Smile cartridge SRAM (see vsmile.h's HasNvram()) only ever holds one
// "current drawing", there's no multi-slot concept to mirror here. A
// dedicated folder, separate from sdmc:/switch/dsmile/states/.
std::string NvramFilePath(const std::string& rom_path) {
  std::string base = rom_path;
  const size_t slash = base.find_last_of('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);
  const size_t dot = base.find_last_of('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  return "sdmc:/switch/dsmile/nvram/" + base + ".nvram";
}

// The console's OFF state for a cart, kept between sessions: a full core
// state captured once the game has put the console into standby (after
// OFF and its goodbye). Real hardware keeps RAM powered in standby, which is
// how a game "remembers your place" (Smart Keyboard manual); loading this at
// the next launch and pressing ON is the same as never having taken the
// cartridge out. Consumed on load, like the RAM it stands for: quitting
// without turning the console off (L+R+Plus, HOME) boots cold next time.
std::string StandbyFilePath(const std::string& rom_path) {
  std::string base = rom_path;
  const size_t slash = base.find_last_of('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);
  const size_t dot = base.find_last_of('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  return "sdmc:/switch/dsmile/standby/" + base + ".dss";
}

bool HasZipExtension(const std::string& path) {
  if (path.size() < 4) return false;
  std::string ext = path.substr(path.size() - 4);
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext == ".zip";
}

// Loads and resets a fresh VSmile instance for `rom_path` (a fresh instance
// per load, same as Android's jni_bridge.cpp nativeInit(), rather than
// trying to reuse/reset one across games). `rom_path` may be a .bin file or
// a .zip archive containing one (switch_zip_extract_bin() unpacks it into
// memory first - the emulated cart is never loaded from a file still
// sitting compressed on disk). Optional BIOS from switch_menu_bios_path()
// is applied if present - not required (see README.md's BIOS note), just
// improves compatibility on a few titles.
bool KeyboardSelected() { return g_settings.controller_type == "keyboard"; }
bool MotionSelected() { return g_settings.controller_type == "motion"; }
bool P2MotionSelected() { return g_settings.p2_controller_type == "motion"; }
bool MatSelected() { return g_settings.controller_type == "mat"; }
bool P2MatSelected() { return g_settings.p2_controller_type == "mat"; }

// Whether the loaded BIOS is a V.Smile Motion one - set by LoadGame().
bool s_motion_bios = false;
// The console type ApplyPortDevices() last applied (-1: none yet, so the
// next call applies it - reset per launch).
int s_applied_motion_console = -1;

// Port 1 is the Switch pad as a joystick, a USB keyboard as the Smart
// Keyboard, or the pad as a Motion controller or dance mat; port 2 (the
// second pad) is a joystick, Motion controller or mat. Cheap no-op when unchanged,
// so it's also called every frame to pick up a change made from the pause
// menu (the core re-plugs the port). Motion carts only offer motion
// controls on a Motion console, so a Motion controller implies one (as does
// a Motion BIOS, matching the real hardware).
void ApplyPortDevices(dsmile::VSmile& vs) {
  vs.SetPort1Device(KeyboardSelected() ? dsmile::PortDevice::SmartKeyboard
                    : MotionSelected() ? dsmile::PortDevice::Motion
                    : MatSelected()    ? dsmile::PortDevice::Mat
                                       : dsmile::PortDevice::Joystick,
                    switch_settings_keyboard_language_code());
  vs.SetPort2Device(P2MotionSelected() ? dsmile::PortDevice::Motion
                    : P2MatSelected()  ? dsmile::PortDevice::Mat
                                       : dsmile::PortDevice::Joystick);
  // The console type only follows the settings when they change (or at
  // launch): a loaded save state brings the board it was saved on, and
  // switching it under a running game makes the game restart.
  const int motion_console = (s_motion_bios || MotionSelected() || P2MotionSelected()) ? 1 : 0;
  if (motion_console != s_applied_motion_console) {
    vs.SetMotionConsole(motion_console != 0);
    s_applied_motion_console = motion_console;
  }
}

// Whether the running console uses PAL video timing (set at launch from
// g_settings.pal_timing). PAL is 50 frames a second on a 60 Hz display:
// each display refresh adds 5 to s_pal_credit and each shown emulated frame
// costs 6; a refresh with no credit shows the previous frame again, giving 5
// emulated frames per 6 refreshes. Frames frame skip doesn't render don't
// wait for vsync, so they aren't charged.
bool s_pal = false;
int s_pal_credit = 0;
double s_next_frame_due_ms = 0;  // when an undrawn (frame-skipped) frame may end - see the main loop

// Controller LEDs overlay (Settings > Graphics > Controller LEDs): the four
// colour-button lights the console sets with its 6x commands (bit 0 green,
// 1 blue, 2 yellow, 3 red - veesem's order), drawn red/yellow/blue/green
// like the pad, dim when off. One row per connected port, bottom left.
void DrawLeds(uint8_t leds1, uint8_t leds2, bool p2) {
  static const float kColour[4][3] = {{0.95f, 0.2f, 0.2f}, {1.0f, 0.85f, 0.1f},
                                      {0.25f, 0.45f, 1.0f}, {0.2f, 0.85f, 0.3f}};
  static const int kBit[4] = {3, 2, 1, 0};  // red, yellow, blue, green
  constexpr float kSize = 22.0f, kGap = 8.0f, kMargin = 20.0f;
  glViewport(0, 0, kViewportW, kViewportH);  // the game pass may leave a smaller one
  switch_ui_begin_frame(kViewportW, kViewportH);
  const int rows = p2 ? 2 : 1;
  for (int row = 0; row < rows; row++) {
    const uint8_t leds = row == 0 ? leds1 : leds2;
    const float y = kViewportH - kMargin - kSize - (rows - 1 - row) * (kSize + kGap);
    for (int i = 0; i < 4; i++) {
      const float x = kMargin + i * (kSize + kGap);
      const float k = ((leds >> kBit[i]) & 1) ? 1.0f : 0.3f;
      switch_ui_draw_rect(x - 2, y - 2, kSize + 4, kSize + 4, 0.0f, 0.0f, 0.0f, 0.6f);
      switch_ui_draw_rect(x, y, kSize, kSize, kColour[i][0] * k, kColour[i][1] * k, kColour[i][2] * k, 1.0f);
    }
  }
  glDisable(GL_BLEND);  // switch_ui_begin_frame() turns it on; the game pass expects it off
}

// Performance overlay (Settings > Graphics > Performance): per frame, the
// emulation (RunFrame), the rewind snapshot and the GL draw, averaged - and
// the emulation's worst frame - over half a second, plus the frame rate
// actually shown. "Emu" against the 16.7 ms budget (20 ms PAL) is the number
// that says whether the core keeps up.
struct PerfStats {
  double emu_sum = 0, emu_max = 0, snap_sum = 0, draw_sum = 0;
  int frames = 0, shown = 0;
  double window_start_ms = 0;
  std::string text = "measuring...";

  void Add(double emu_ms, double snap_ms, double draw_ms, bool rendered) {
    if (window_start_ms == 0) window_start_ms = NowMs();
    emu_sum += emu_ms;
    emu_max = std::max(emu_max, emu_ms);
    snap_sum += snap_ms;
    draw_sum += draw_ms;
    frames++;
    if (rendered) shown++;
    const double elapsed = NowMs() - window_start_ms;
    if (elapsed < 500.0) return;
    char buf[160];
    int n = std::snprintf(buf, sizeof(buf), "Emu %.1f ms (max %.1f)  Snap %.2f  Draw %.1f  |  %.1f fps",
                          emu_sum / frames, emu_max, snap_sum / frames, draw_sum / std::max(shown, 1),
                          shown * 1000.0 / elapsed);
    // Frame skip on: what's shown isn't the game's speed, so add that too.
    const int skip = switch_frameskip_current_level();
    if (skip > 0 && n > 0 && n < (int)sizeof(buf)) {
      std::snprintf(buf + n, sizeof(buf) - n, "  (skip %d, %.1f emulated)", skip, frames * 1000.0 / elapsed);
    }
    text = buf;
    *this = PerfStats{0, 0, 0, 0, 0, 0, NowMs(), text};
  }
};
PerfStats s_perf;

std::string s_nro_path;  // argv[0]: which build is running (baseline/optimized test builds)
BiosInfo s_bios_info;    // the BIOS ReadBios() last identified, for the log header

// Clock rate of a PCV module in Hz, 0 if unreadable. clkrst on 8.0.0+,
// pcv before that.
uint32_t ReadClockHz(PcvModule module, PcvModuleId module_id) {
  uint32_t hz = 0;
  if (hosversionAtLeast(8, 0, 0)) {
    if (R_SUCCEEDED(clkrstInitialize())) {
      ClkrstSession session;
      if (R_SUCCEEDED(clkrstOpenSession(&session, module_id, 3))) {
        clkrstGetClockRate(&session, &hz);
        clkrstCloseSession(&session);
      }
      clkrstExit();
    }
  } else if (R_SUCCEEDED(pcvInitialize())) {
    pcvGetClockRate(module, &hz);
    pcvExit();
  }
  return hz;
}

std::string ClockLine() {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "cpu=%u MHz, gpu=%u MHz, mem=%u MHz, %s",
                ReadClockHz(PcvModule_CpuBus, PcvModuleId_CpuBus) / 1000000,
                ReadClockHz(PcvModule_GPU, PcvModuleId_GPU) / 1000000,
                ReadClockHz(PcvModule_EMC, PcvModuleId_EMC) / 1000000,
                appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");
  return buf;
}

// Performance log (Performance: "Shown + log"): one CSV per play session in
// sdmc:/switch/dsmile/perf/. "#" lines carry the setup - build, game, BIOS,
// graphics settings, clocks (re-read every 5 s, in case a clock profile
// changes them) - then one row per emulated frame, in ms:
//   emu   VSmile::RunFrame()
//   snap  the rewind snapshot (SaveState)
//   upload   the game picture's glTexSubImage2D, within render
//   render   switch_render_frame(): GL submission for the game picture
//   overlay  the LED / standby / performance overlays
//            (all three 0 when frame skip skipped drawing the frame)
//   swap     eglSwapBuffers(), mostly the wait for vsync
//   loop  the whole main-loop iteration, input polling to swap
struct PerfLog {
  FILE* file = nullptr;
  int frame = 0;
  double last_ms = 0;
  std::string status;  // shown under the overlay while logging
  bool started = false;  // Open() tried this session (a failed open isn't retried every frame)

  void Open(const std::string& rom_path) {
    started = true;
    mkdir("sdmc:/switch/dsmile/perf", 0777);  // EEXIST is fine
    std::string base = rom_path.substr(rom_path.find_last_of('/') + 1);
    base = base.substr(0, base.find_last_of('.'));
    const time_t now = time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
    const std::string path = "sdmc:/switch/dsmile/perf/" + base + "_" + stamp + ".csv";
    file = std::fopen(path.c_str(), "w");
    if (!file) {
      status = "Log failed: " + std::string(std::strerror(errno)) + " (" + path + ")";
      return;
    }
    status = "Logging to " + path.substr(path.find("perf/"));
    static char buf[1 << 16];
    std::setvbuf(file, buf, _IOFBF, sizeof(buf));
    std::fprintf(file, "# build: %s\n# game: %s\n# bios: %s\n", s_nro_path.c_str(), rom_path.c_str(),
                 s_bios_info.label[0] ? s_bios_info.label : "unrecognized");
    std::fprintf(file, "# renderer=%s shader=%s aspect=%s background=%s bezel=%s leds=%d frameskip=%s/%d pal=%d\n",
                 g_settings.accurate_renderer ? "accurate" : "fast", g_settings.shader_mode.c_str(),
                 g_settings.aspect_mode.c_str(), g_settings.background_mode.c_str(), g_settings.bezel_mode.c_str(),
                 g_settings.show_leds ? 1 : 0, g_settings.frame_skip_mode.c_str(), g_settings.frame_skip_manual,
                 s_pal ? 1 : 0);
    std::fprintf(file, "# clocks at frame 0: %s\nframe,emu,snap,upload,render,overlay,swap,loop\n",
                 ClockLine().c_str());
    std::fflush(file);  // the file and its header reach the card even if the session ends abruptly
    frame = 0;
    last_ms = NowMs();
  }
  void Close() {
    if (file) std::fclose(file);
    file = nullptr;
    started = false;
    status.clear();
  }
  void Row(double emu_ms, double snap_ms, double upload_ms, double render_ms, double overlay_ms, double swap_ms) {
    if (!file) return;
    const double now = NowMs();
    if (frame > 0 && frame % 300 == 0) {
      const SwitchAudioStats a = switch_audio_stats();
      std::fprintf(file,
                   "# clocks at frame %d: %s; audio dropped=%d underruns=%d reinits=%d level=%.0f adjust=%+.3f%%; "
                   "frameskip=%d\n",
                   frame, ClockLine().c_str(), a.dropped, a.underruns, a.reinits, a.level, a.adjust * 100.0,
                   switch_frameskip_current_level());
      std::fflush(file);  // rows reach the card every 5 s, not only when the game is left
    }
    std::fprintf(file, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", frame, emu_ms, snap_ms, upload_ms, render_ms,
                 overlay_ms, swap_ms, now - last_ms);
    last_ms = now;
    frame++;
  }
};
PerfLog s_perf_log;

// The overlay's text changes twice a second at most, but switch_ui draws
// every glyph as its own textured quad - about 2 ms a frame for these two
// lines on hardware, which skewed the very timings they show. So the text
// is drawn into this texture only when it changes, and each frame draws
// just the box and one quad.
struct PerfText {
  static constexpr int kW = kViewportW, kH = 64;
  GLuint fbo = 0, tex = 0;
  std::string key;  // what the texture holds
  float width = 0;  // of its widest line
};
PerfText s_perf_text;

void DrawPerf() {
  constexpr float kSize = 22.0f, kPad = 8.0f, kMargin = 12.0f;
  const std::string& log = s_perf_log.status;  // empty unless "Shown + log"
  const int lines = log.empty() ? 1 : 2;
  PerfText& t = s_perf_text;
  if (!t.fbo) {
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PerfText::kW, PerfText::kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  const std::string key = s_perf.text + "\n" + log;
  if (key != t.key) {
    t.key = key;
    t.width = std::max(switch_ui_text_width(s_perf.text, kSize), switch_ui_text_width(log, kSize));
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, PerfText::kW, PerfText::kH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    switch_ui_begin_frame(PerfText::kW, PerfText::kH);
    // Premultiplied into the transparent texture, so it composites cleanly below.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    switch_ui_draw_text(0, 0, kSize, 1.0f, 1.0f, 1.0f, 1.0f, s_perf.text);
    if (!log.empty()) {
      const bool failed = s_perf_log.file == nullptr;
      switch_ui_draw_text(0, kSize + kPad, kSize, 1.0f, failed ? 0.4f : 0.8f, failed ? 0.4f : 0.8f, 1.0f, log);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  glViewport(0, 0, kViewportW, kViewportH);
  switch_ui_begin_frame(kViewportW, kViewportH);
  switch_ui_draw_rect(kMargin, kMargin, t.width + kPad * 2, kSize * lines + kPad * (lines + 1), 0.0f, 0.0f, 0.0f,
                      0.6f);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  switch_ui_draw_render_texture(t.tex, kMargin + kPad, kMargin + kPad, PerfText::kW, PerfText::kH);
  glDisable(GL_BLEND);
}

// Shown over the black screen while the console is off (standby) and the
// player turned it off themselves, so it doesn't look like a hang.
void DrawStandbyHint(bool on_bound) {
  glViewport(0, 0, kViewportW, kViewportH);
  switch_ui_begin_frame(kViewportW, kViewportH);
  const std::string line1 = "The V.Smile is off";
  const std::string line2 = on_bound ? "Press ON to turn it on" : "Press Enter to turn it on";
  constexpr float kBig = 34.0f, kSmall = 24.0f;
  switch_ui_draw_text((kViewportW - switch_ui_text_width(line1, kBig)) / 2, kViewportH / 2 - 44, kBig,
                      0.85f, 0.85f, 0.85f, 1.0f, line1);
  switch_ui_draw_text((kViewportW - switch_ui_text_width(line2, kSmall)) / 2, kViewportH / 2 + 8, kSmall,
                      0.6f, 0.6f, 0.6f, 1.0f, line2);
  glDisable(GL_BLEND);
}

// A scripted sequence of the console's buttons, one entry per frame (bit 0
// ON, 1 OFF, 2 RESTART), played from power-on after a "BIOS test screen"
// launch (Library & Storage). Each BIOS has its own way in:
// - v1.00-v1.03 (and the US "Motion" dump, which is v1.03): hold ON, OFF
//   and RESTART at power-on, then release (BIOS showBiosScreens, 0x19317).
// - Spanish Motion BIOS (test program v7.05): hold OFF at power-on, then
//   press OFF about 5 more times - 10 changes, 9-15 frames apart, within its
//   ~2 s window (0x11A16; faster is debounced away, slower times out).
std::deque<uint8_t> s_console_script;

void ScriptTestScreenEntry(const BiosInfo& info) {
  s_console_script.clear();
  if (info.family == BiosFamily::Motion && info.version == 0) {
    for (int i = 0; i < 5; i++) s_console_script.push_back(2);
    for (int k = 0; k < 12; k++) {
      for (int i = 0; i < 12; i++) s_console_script.push_back(k % 2 == 0 ? 0 : 2);
    }
  } else {
    for (int i = 0; i < 59; i++) s_console_script.push_back(7);
  }
  s_console_script.push_back(0);
}

// Scripted presses of the console's ON / OFF buttons, in frames left: ON
// after resuming a saved standby, OFF when the pause menu's Quit turns the
// console off. s_powering_off: Quit is waiting (up to kPowerOffTimeout
// frames) for the game to finish its goodbye and enter standby.
int s_press_on = 0, s_press_off = 0;
bool s_powering_off = false;
int s_power_off_frames = 0;
constexpr int kPowerPressFrames = 10;
constexpr int kPowerOffTimeout = 600;  // 10 s
// Set by LoadBiosOnly(): no cartridge, so no standby to keep.
bool s_bios_only = false;

// Reads a BIOS dump, fixing a known byte-swapped one (the core reads
// little-endian words, so a swapped file won't boot). Sets s_motion_bios.
bool ReadBios(const std::string& path, std::vector<uint8_t>& bios, BiosInfo* out_info = nullptr) {
  s_motion_bios = false;
  if (path.empty() || !LoadFile(path.c_str(), bios)) return false;
  const BiosInfo info = switch_bios_identify_crc(switch_bios_crc32(bios.data(), bios.size()));
  if (out_info) *out_info = info;
  s_bios_info = info;
  switch_bios_fix_byte_order(bios, info);
  s_motion_bios = info.family == BiosFamily::Motion;
  return true;
}

// Boots the BIOS with no cartridge inserted, as on a real console: with the
// slot empty the BIOS answers at every bank (MAME's vsmile banked_map), so
// it's loaded as a 4x-mirrored "cart" as well as the sysrom. It then shows
// its insert-a-cartridge screen, or its test screen with `test_screen`.
std::unique_ptr<dsmile::VSmile> LoadBiosOnly(const std::string& bios_path, bool test_screen) {
  std::vector<uint8_t> bios;
  BiosInfo info;
  if (!ReadBios(bios_path, bios, &info) || bios.size() < 2) return nullptr;
  constexpr size_t kBankBytes = 0x200000;  // 1M words
  std::vector<uint8_t> mirrored(kBankBytes * 4, 0);
  for (size_t bank = 0; bank < 4; bank++) {
    std::copy_n(bios.begin(), std::min(bios.size(), kBankBytes), mirrored.begin() + bank * kBankBytes);
  }
  auto vs = std::make_unique<dsmile::VSmile>();
  if (!vs->LoadCart(mirrored.data(), mirrored.size())) return nullptr;
  vs->LoadSysrom(bios.data(), bios.size());
  vs->SetVtechLogo(g_settings.vtech_logo);  // Settings > Library & Storage > VTech Logo
  vs->SetAccurate(g_settings.accurate_renderer);
  vs->SetRegion(g_settings.region);
  s_applied_motion_console = -1;  // a fresh console: apply the settings' type
  ApplyPortDevices(*vs);
  s_pal = g_settings.pal_timing;  // Settings > Library & Storage > Video Timing
  switch_frameskip_set_pal(s_pal);
  vs->Reset(s_pal);
  if (test_screen) ScriptTestScreenEntry(info);
  else s_console_script.clear();
  s_bios_only = true;
  s_press_on = s_press_off = 0;
  return vs;
}

std::unique_ptr<dsmile::VSmile> LoadGame(const std::string& rom_path) {
  std::vector<uint8_t> rom;
  const bool ok = HasZipExtension(rom_path) ? switch_zip_extract_bin(rom_path, rom)
                                             : LoadFile(rom_path.c_str(), rom);
  if (!ok) return nullptr;

  auto vs = std::make_unique<dsmile::VSmile>();
  if (!vs->LoadCart(rom.data(), rom.size())) return nullptr;

  std::vector<uint8_t> bios;
  if (ReadBios(switch_menu_bios_path(), bios)) vs->LoadSysrom(bios.data(), bios.size());
  s_console_script.clear();

  vs->SetVtechLogo(g_settings.vtech_logo);  // Settings > Library & Storage > VTech Logo
  vs->SetAccurate(g_settings.accurate_renderer);  // Settings > Graphics > Renderer
  vs->SetRegion(g_settings.region);  // Settings > Library & Storage > Region - see its own comment
  s_applied_motion_console = -1;  // a fresh console: apply the settings' type
  ApplyPortDevices(*vs);  // Settings > Controller > Controller Type
  s_pal = g_settings.pal_timing;  // Settings > Library & Storage > Video Timing
  switch_frameskip_set_pal(s_pal);
  vs->Reset(s_pal);

  // Cart-side NVRAM (see vsmile.h's HasNvram()/GetNvram()) - only ever
  // non-empty for the handful of carts LoadCart() itself recognizes as
  // having onboard SRAM; a no-op call otherwise. Loaded *after* Reset()
  // since Reset() doesn't touch nvram_ at all (real hardware SRAM survives
  // a console reset too), same reasoning as loading a BIOS/setting the
  // region before Reset() but independent of it either way.
  if (vs->HasNvram()) {
    std::vector<uint8_t> nvram;
    if (LoadFile(NvramFilePath(rom_path).c_str(), nvram)) {
      vs->SetNvram(nvram.data(), nvram.size());
    }  // else: no save yet, or first time this cart's been seen - stays zeroed
  }

  s_bios_only = false;
  s_press_on = s_press_off = 0;
  const std::string standby = StandbyFilePath(rom_path);
  std::vector<uint8_t> state;
  if (LoadFile(standby.c_str(), state)) {
    // Resume the console as it was left: asleep, RAM intact. Then press ON,
    // as a player would. A state from another version (or a different cart
    // with the same name) fails LoadState's guards and we boot cold.
    if (vs->LoadState(state.data(), state.size())) {
      s_press_on = kPowerPressFrames;
      s_pal = vs->Soc().IsPal();  // the saved console's video standard wins
      switch_frameskip_set_pal(s_pal);
    } else {
      vs->Reset(s_pal);
    }
    remove(standby.c_str());
  }
  return vs;
}

// Writes the console's standby state for `rom_path` (see StandbyFilePath()).
void SaveStandby(dsmile::VSmile& vs, const std::string& rom_path) {
  mkdir("sdmc:/switch/dsmile/standby", 0777);  // EEXIST is fine
  std::vector<uint8_t> data;
  vs.SaveState(data);
  FILE* f = fopen(StandbyFilePath(rom_path).c_str(), "wb");
  if (!f) return;
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
}

// Writes the cart's current NVRAM content back to its dedicated save file -
// a no-op for every cart except the handful with onboard SRAM (see
// vsmile.h's HasNvram()). Deliberately separate from save states/rewind:
// this mirrors what the physical cartridge's own battery-backed SRAM does
// on real hardware, independent of anything this port's own save-state
// system does (see switch/README.md's "Cart-side NVRAM" section). Called
// right before every place `vs` gets torn down, so nothing's lost between
// "still playing" and "cart's been swapped out."
void FlushNvram(dsmile::VSmile& vs, const std::string& rom_path) {
  if (!vs.HasNvram()) return;
  mkdir("sdmc:/switch/dsmile/nvram", 0777);  // EEXIST (already there) is fine to ignore
  const std::vector<uint8_t> data = vs.GetNvram();
  FILE* f = fopen(NvramFilePath(rom_path).c_str(), "wb");
  if (!f) return;
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
}

// VSmile::SaveState()/LoadState() and the StateWriter/StateReader they're
// built on (app/src/main/cpp/core/state.h) are already portable, byte-stable
// serialization, unchanged from what Android's jni_bridge.cpp uses - this is
// purely the Switch-side file I/O around them, same filename convention as
// Android's own EmuActivity.kt (see switch_settings_state_file_path()).
bool SaveStateToSlot(dsmile::VSmile& vs, const std::string& rom_path, int slot) {
  mkdir("sdmc:/switch/dsmile/states", 0777);  // EEXIST (already there) is fine to ignore
  std::vector<uint8_t> data;
  vs.SaveState(data);
  FILE* f = fopen(switch_settings_state_file_path(rom_path, slot).c_str(), "wb");
  if (!f) return false;
  const size_t n = fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  const bool ok = n == data.size();
  // Only on success, matching Android's saveState() - remembers where the
  // Save/Load State hotkeys and the pause menu's slot picker should default
  // to next, same as EmuActivity.kt's lastSlot_$romName preference.
  if (ok) switch_settings_set_last_slot(rom_path, slot);
  return ok;
}

// LoadState() itself is cart-checksum guarded (see vsmile.h) - loading a
// state saved by a different cart/version just fails harmlessly, same as
// Android's nativeLoadState() returning false in that case.
bool LoadStateFromSlot(dsmile::VSmile& vs, const std::string& rom_path, int slot) {
  std::vector<uint8_t> data;
  if (!LoadFile(switch_settings_state_file_path(rom_path, slot).c_str(), data)) return false;
  const bool ok = vs.LoadState(data.data(), data.size());
  if (ok) {
    switch_settings_set_last_slot(rom_path, slot);
    s_pal = vs.Soc().IsPal();  // the state's video standard, like its console type
    switch_frameskip_set_pal(s_pal);
  }
  return ok;
}

}  // namespace

void switch_present() { eglSwapBuffers(s_display, s_surface); }

int main(int argc, char** argv) {
  if (argc > 0 && argv[0]) s_nro_path = argv[0];
  if (!InitEgl(nwindowGetDefault(), kViewportW, kViewportH)) {
    RunConsoleErrorLoop("EGL init failed.");
    return EXIT_FAILURE;
  }

  if (!gladLoadGL()) {
    RunConsoleErrorLoop("gladLoadGL failed.");
    DeinitEgl();
    return EXIT_FAILURE;
  }

  if (!switch_render_init(kViewportW, kViewportH) || !switch_ui_init()) {
    DeinitEgl();
    RunConsoleErrorLoop("Renderer init failed.");
    return EXIT_FAILURE;
  }
  // No result kept here - switch_audio_push() tracks its own init/retry
  // state internally now (see switch_audio.cpp), so a failed or later-
  // wedged audio session can self-heal instead of staying silent for the
  // rest of the process's life.
  switch_audio_init();
  switch_input_init();
  switch_menu_init();  // loads settings, applies the saved theme, scans the library

  AppState state = AppState::RomList;
  std::unique_ptr<dsmile::VSmile> vs;
  std::string current_rom_path;  // valid while vs is - for the state-slot file paths
  std::deque<std::vector<uint8_t>> rewind_buffer;  // valid while vs is - see kRewindMax's comment

  while (appletMainLoop()) {
    if (!vs) s_perf_log.Close();  // the play session that owned the log has ended
    const SwitchInputState in = switch_input_poll();

    if (state == AppState::RomList) {
      std::string rom_path;
      int slot = 0;
      const MenuAction action = switch_menu_update(in.down, in.held, rom_path, slot);
      if (action == MenuAction::Quit) break;  // + at the game list: quit to hbmenu
      if (action == MenuAction::LaunchBios) {
        auto loaded = LoadBiosOnly(rom_path, slot == 1);
        if (loaded) {
          vs = std::move(loaded);
          current_rom_path = rom_path;  // save states go under the BIOS file's name
          state = AppState::Playing;
          rewind_buffer.clear();
          switch_frameskip_reset();
          switch_menu_set_current_game(rom_path);
        }
      }
      if (action == MenuAction::LaunchGame) {
        auto loaded = LoadGame(rom_path);
        if (loaded) {
          vs = std::move(loaded);
          current_rom_path = rom_path;
          state = AppState::Playing;
          rewind_buffer.clear();  // a previous cart's snapshots would just fail LoadState()'s
                                   // checksum guard, but there's no reason to keep them around
          switch_frameskip_reset();  // fresh timing history, not carried over from a previous cart
          switch_settings_mark_played(rom_path);  // for SortMode::RecentlyPlayed
          switch_menu_set_current_game(rom_path);  // for the pause menu's state-slot picker
        }
        // Load failure (corrupt dump, etc.): stay on the list. No error
        // toast yet - see README.md's known gaps.
      }

      switch_chrome_clear_background(kViewportW, kViewportH);
      switch_menu_render(kViewportW, kViewportH);
      eglSwapBuffers(s_display, s_surface);
      continue;
    }

    if (state == AppState::Paused) {
      std::string unused;
      int slot = 0;
      switch (switch_menu_update(in.down, in.held, unused, slot)) {
        case MenuAction::ResumeGame: state = AppState::Playing; break;
        case MenuAction::ResetGame:
          // The console's RESTART button: a hardware reset that keeps RAM
          // (VSmile::Restart()), as the manuals describe it - "start the
          // game over from the beginning".
          vs->Restart();
          state = AppState::Playing;
          break;
        case MenuAction::QuitToGrid:
          if (s_bios_only) {
            vs.reset();
            rewind_buffer.clear();
            state = AppState::RomList;
            break;
          }
          // Already off (the OFF binding): keep that standby and leave now.
          if (vs->Sleeping()) {
            SaveStandby(*vs, current_rom_path);
            FlushNvram(*vs, current_rom_path);
            vs.reset();
            rewind_buffer.clear();
            state = AppState::RomList;
            break;
          }
          // Turn the console off first: press OFF and keep running so the
          // game can play its goodbye and enter standby, which is then kept
          // for the next launch (see the power-off check after RunFrame()).
          s_powering_off = true;
          s_power_off_frames = 0;
          s_press_off = kPowerPressFrames;
          state = AppState::Playing;
          break;
        case MenuAction::SaveState:
          SaveStateToSlot(*vs, current_rom_path, slot);
          state = AppState::Playing;
          break;
        case MenuAction::LoadState:
          LoadStateFromSlot(*vs, current_rom_path, slot);
          state = AppState::Playing;
          break;
        default: break;  // still navigating the pause menu / its Settings screens
      }
      // Whichever of the cases above resumed gameplay, the pause itself
      // shouldn't count toward "RunFrame() is running slow" - reset the
      // timing history rather than let the pause's own duration skew it.
      if (state == AppState::Playing) switch_frameskip_reset();

      switch_chrome_clear_background(kViewportW, kViewportH);
      switch_menu_render(kViewportW, kViewportH);
      eglSwapBuffers(s_display, s_surface);
      continue;
    }

    // Playing. L+R+Plus (all three held together) returns to the ROM list -
    // same chord the bring-up build used to quit outright, repurposed now
    // that there's somewhere to return to. Nothing else needs all three
    // held at once, so this doesn't risk an accidental trigger mid-game.
    // Still available alongside the pause menu's own Quit as a hard escape
    // hatch, e.g. if Menu itself is unbound.
    if ((in.held & HidNpadButton_L) && (in.held & HidNpadButton_R) &&
        (in.down & HidNpadButton_Plus)) {
      FlushNvram(*vs, current_rom_path);
      vs.reset();
      rewind_buffer.clear();
      s_powering_off = false;
      // No automatic rescan here anymore - it decodes every cover PNG
      // again, which isn't free now that the grid has cover art. Library &
      // Storage > Rescan library covers the "SD card changed" case instead.
      state = AppState::RomList;
      continue;
    }

    // Menu action (default +, see Settings > Controller) pauses in place and
    // opens the Resume/Save/Load/Reset/Quit/Settings screen - main.cpp only
    // handles the pause/resume transition itself; switch_menu owns
    // everything about what's actually in that screen.
    const uint64_t menu_bit = switch_settings_button_bit(g_settings.action_binding[(int)GameAction::Menu]);
    if (menu_bit != 0 && (in.down & menu_bit) && !s_powering_off) {
      switch_menu_enter_pause();
      state = AppState::Paused;
      continue;
    }

    // Save/Load State hotkeys (unbound by default, same as Android's own
    // InputMapper.kt defaults): quick-save/load to whichever slot was used
    // last for this cart, no menu involved - same as Android's onHotkey()
    // calling saveState(lastSlot())/loadState(lastSlot()) directly. Doesn't
    // pause or skip this frame's gameplay, just fires alongside it.
    const uint64_t save_bit = switch_settings_button_bit(g_settings.action_binding[(int)GameAction::SaveState]);
    if (save_bit != 0 && (in.down & save_bit) && !s_powering_off) {
      SaveStateToSlot(*vs, current_rom_path, switch_settings_last_slot(current_rom_path));
    }
    const uint64_t load_bit = switch_settings_button_bit(g_settings.action_binding[(int)GameAction::LoadState]);
    if (load_bit != 0 && (in.down & load_bit) && !s_powering_off) {
      LoadStateFromSlot(*vs, current_rom_path, switch_settings_last_slot(current_rom_path));
    }

    // Two-player: fully automatic, no setting - a second V.Smile controller
    // port simply exists whenever a second Switch controller is physically
    // connected right now, and doesn't when it isn't (see
    // VSmile::SetPlayer2Connected()'s own comment for why this needs
    // checking every frame rather than just once). Player 1 vs. player 2
    // share the same action_binding mapping - real V.Smile's two pads are
    // identical, so there's nothing separate to configure for player 2.
    // Poll first, then check readiness from what that just updated - see
    // switch_input_poll_p2()'s own comment for why this order matters.
    const SwitchInputState in2 = switch_input_poll_p2();
    const bool p2_connected = switch_input_player2_ready();
    vs->SetPlayer2Connected(p2_connected);

    // PAL: no credit for an emulated frame this refresh - show the last one
    // again (see s_pal_credit).
    if (s_pal && s_pal_credit < 0) {
      s_pal_credit += 5;
      switch_render_frame(vs->Framebuffer());
      if (g_settings.show_leds) DrawLeds(vs->Leds(), vs->Leds2(), p2_connected);
      eglSwapBuffers(s_display, s_surface);
      continue;
    }

    // Rewind: held-based, no menu toggle (Android doesn't have one for this
    // either).
    const uint64_t rewind_bit = switch_settings_button_bit(g_settings.action_binding[(int)GameAction::Rewind]);
    const bool rewinding = rewind_bit != 0 && (in.held & rewind_bit) && !s_powering_off;

    if (rewinding) {
      // Pop and load the most recently recorded snapshot, then simulate one
      // frame forward from it - see kRewindMax's comment above for why that
      // sequence (rather than just loading) is what actually steps backward
      // one recorded frame per real frame. Audio is discarded while
      // rewinding, matching Android exactly - playing it back would sound
      // like noise, not a coherent reversed track.
      if (!rewind_buffer.empty()) {
        vs->LoadState(rewind_buffer.back().data(), rewind_buffer.back().size());
        rewind_buffer.pop_back();
      }
      const double run_frame_start_ms = NowMs();
      vs->RunFrame();
      switch_frameskip_record_frame_time(NowMs() - run_frame_start_ms);
      vs->DrainAudio(g_spu_buf, kSpuBufMax);

      if (switch_frameskip_should_render()) {
        switch_render_frame(vs->Framebuffer());
        if (g_settings.show_leds) DrawLeds(vs->Leds(), vs->Leds2(), p2_connected);
        eglSwapBuffers(s_display, s_surface);
        if (s_pal) s_pal_credit -= 1;  // this frame (-6) plus this refresh (+5)
      }
    } else {
      ApplyPortDevices(*vs);
      uint32_t p1_buttons = in.buttons;
      if (KeyboardSelected()) {
        // The pad stays usable as the keyboard's own joystick and
        // OK/Quit/Help keys; the USB keyboard adds typing on top.
        const SwitchKeyboardInput kb = switch_input_poll_keyboard();
        for (const auto& ev : kb.events) vs->KeyboardKey(ev.first, ev.second);
        p1_buttons |= kb.buttons;
      } else if (MotionSelected()) {
        const SwitchMotionInput m = switch_input_poll_motion();
        vs->SetMotion(m.x, m.y, m.z);
      }
      // While Quit is turning the console off, the pads let go.
      if (s_powering_off) vs->SetInput(0, 0, 0);
      else vs->SetInput(in.joy_x, in.joy_y, p1_buttons);

      // Console buttons: the bindings (Settings > Controller, unbound by
      // default), plus scripted presses. RESTART is the hardware reset
      // line, so it fires once per press rather than being held.
      const auto action_bit = [](GameAction a) {
        return switch_settings_button_bit(g_settings.action_binding[(int)a]);
      };
      const uint64_t on_bit = action_bit(GameAction::PowerOn), off_bit = action_bit(GameAction::PowerOff);
      const uint64_t restart_bit = action_bit(GameAction::Restart);
      if (!s_powering_off && restart_bit != 0 && (in.down & restart_bit)) vs->Restart();
      // Asleep with ON unbound, Enter turns the console back on (there'd be
      // no other way short of Reset Game).
      const uint64_t enter_bit = action_bit(GameAction::Enter);
      if (!s_powering_off && on_bit == 0 && vs->Sleeping() && enter_bit != 0 && (in.down & enter_bit)) {
        s_press_on = kPowerPressFrames;
      }
      bool on = s_press_on > 0, off = s_press_off > 0;
      if (!s_powering_off) {
        on = on || (on_bit != 0 && (in.held & on_bit));
        off = off || (off_bit != 0 && (in.held & off_bit));
      }
      if (s_press_on > 0) s_press_on--;
      if (s_press_off > 0) s_press_off--;
      if (!s_console_script.empty()) {
        const uint8_t b = s_console_script.front();
        s_console_script.pop_front();
        vs->SetConsoleButtons(b & 1, b & 2, b & 4);
      } else {
        vs->SetConsoleButtons(on, off, false);
      }
      if (p2_connected) {
        vs->SetInput2(in2.joy_x, in2.joy_y, in2.buttons);
        if (P2MotionSelected()) {
          const SwitchMotionInput m2 = switch_input_poll_motion_p2();
          vs->SetMotion2(m2.x, m2.y, m2.z);
        }
      }
      const double run_frame_start_ms = NowMs();
      vs->RunFrame();
      const double emu_ms = NowMs() - run_frame_start_ms;
      switch_frameskip_record_frame_time(emu_ms);

      // Quit's power-off: once the game has put the console into standby,
      // keep that state for the next launch and leave. If it never does
      // (a game that ignores OFF on this screen), give up after the
      // timeout and quit without one.
      if (s_powering_off && (vs->Sleeping() || ++s_power_off_frames > kPowerOffTimeout)) {
        if (vs->Sleeping()) SaveStandby(*vs, current_rom_path);
        else remove(StandbyFilePath(current_rom_path).c_str());
        s_powering_off = false;
        FlushNvram(*vs, current_rom_path);
        vs.reset();
        rewind_buffer.clear();
        state = AppState::RomList;
        continue;
      }

      // Push this frame's snapshot *after* it ran (matches jni_bridge.cpp's
      // Loop() exactly - see kRewindMax's comment above).
      const double snap_start_ms = NowMs();
      rewind_buffer.emplace_back();
      vs->SaveState(rewind_buffer.back());
      if (rewind_buffer.size() > kRewindMax) rewind_buffer.pop_front();
      const double snap_ms = NowMs() - snap_start_ms;

      {
        const int n = vs->DrainAudio(g_spu_buf, kSpuBufMax);
        switch_audio_push(g_spu_buf, n / 2);  // no-ops/self-heals internally if audio isn't up
      }

      const bool render = switch_frameskip_should_render();
      double draw_ms = 0, render_ms = 0, swap_ms = 0;
      if (render) {
        const double draw_start_ms = NowMs();
        switch_render_frame(vs->Framebuffer());
        render_ms = NowMs() - draw_start_ms;
        if (g_settings.show_leds) DrawLeds(vs->Leds(), vs->Leds2(), p2_connected);
        if (vs->Sleeping() && !s_powering_off) DrawStandbyHint(on_bit != 0);
        if (g_settings.perf_mode != 0) DrawPerf();
        draw_ms = NowMs() - draw_start_ms;  // GL submission, not the vsync wait in the swap
        const double swap_start_ms = NowMs();
        eglSwapBuffers(s_display, s_surface);
        swap_ms = NowMs() - swap_start_ms;
        if (s_pal) s_pal_credit -= 1;  // this frame (-6) plus this refresh (+5)
      }
      // A drawn frame is paced by the swap's wait for the display; one frame
      // skip leaves undrawn isn't, so the next frame used to start at once.
      // A run of those got ahead of real time: Manual skip sped fast games
      // up, and Auto's skipping overfilled the audio queue (dropped audio).
      // So an undrawn frame that finishes early waits for its slot: one
      // frame period after the previous frame's. Running behind, nothing
      // waits and the schedule starts over.
      {
        const double period = s_pal ? 20.0 : 1000.0 / 60.0;
        const double now = NowMs();
        if (!render && now < s_next_frame_due_ms) {
          svcSleepThread((int64_t)((s_next_frame_due_ms - now) * 1e6));
          s_next_frame_due_ms += period;
        } else {
          s_next_frame_due_ms = now + period;
        }
      }
      if (g_settings.perf_mode != 0) s_perf.Add(emu_ms, snap_ms, draw_ms, render);
      if (g_settings.perf_mode == 2) {
        if (!s_perf_log.started) s_perf_log.Open(current_rom_path);
        s_perf_log.Row(emu_ms, snap_ms, render ? switch_render_last_upload_ms() : 0, render_ms, draw_ms - render_ms, swap_ms);
      } else {
        s_perf_log.Close();
      }
    }
  }

  if (vs) FlushNvram(*vs, current_rom_path);  // e.g. quitting via HOME/power while still playing
  s_perf_log.Close();
  vs.reset();
  switch_audio_shutdown();
  switch_menu_shutdown();
  switch_ui_shutdown();
  switch_render_shutdown();
  DeinitEgl();
  return EXIT_SUCCESS;
}
