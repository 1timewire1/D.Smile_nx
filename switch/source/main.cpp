// D.Smile Switch port - main loop.
//
// Two states: a ROM browser (list *.bin files under
// sdmc:/switch/dsmile/games, pick one with A) and gameplay. Single-threaded,
// modeled on reference/Yokoi_Game_-_Watch_emulator_3ds-main's switch/source/
// main.cpp (poll input -> step one frame -> render -> play audio -> swap),
// confirmed working on real Switch hardware with this same devkitA64 +
// libnx + switch-glad + switch-mesa toolchain. Unlike Android's
// jni_bridge.cpp, there's no separate emulation thread or Oboe
// pull-callback here - vsync from eglSwapBuffers paces the loop directly,
// same as Yokoi's.

#include <cstdio>
#include <cstdlib>
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
#include "core/switch_frameskip.h"
#include "core/switch_menu.h"
#include "core/switch_present.h"
#include "core/switch_settings.h"
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

// Loads and resets a fresh VSmile instance for `rom_path` (a fresh instance
// per load, same as Android's jni_bridge.cpp nativeInit(), rather than
// trying to reuse/reset one across games). Optional BIOS from
// switch_menu_bios_path() is applied if present - not required (see
// README.md's BIOS note), just improves compatibility on a few titles.
std::unique_ptr<dsmile::VSmile> LoadGame(const std::string& rom_path) {
  std::vector<uint8_t> rom;
  if (!LoadFile(rom_path.c_str(), rom)) return nullptr;

  auto vs = std::make_unique<dsmile::VSmile>();
  if (!vs->LoadCart(rom.data(), rom.size())) return nullptr;

  const std::string bios_path = switch_menu_bios_path();
  if (!bios_path.empty()) {
    std::vector<uint8_t> bios;
    if (LoadFile(bios_path.c_str(), bios)) {
      vs->LoadSysrom(bios.data(), bios.size());
    }
  }

  vs->SetVtechLogo(true);
  vs->SetAccurate(g_settings.accurate_renderer);  // Settings > Graphics > Renderer
  vs->SetRegion(g_settings.region);  // Settings > Library & Storage > Region - see its own comment
  vs->Reset(/*pal=*/false);

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
  return vs;
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
  if (ok) switch_settings_set_last_slot(rom_path, slot);
  return ok;
}

}  // namespace

void switch_present() { eglSwapBuffers(s_display, s_surface); }

int main(int, char**) {
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
    const SwitchInputState in = switch_input_poll();

    if (state == AppState::RomList) {
      std::string rom_path;
      int slot = 0;
      const MenuAction action = switch_menu_update(in.down, in.held, rom_path, slot);
      if (action == MenuAction::Quit) break;  // + at the game list: quit to hbmenu
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
          vs->Reset(/*pal=*/false);
          state = AppState::Playing;
          break;
        case MenuAction::QuitToGrid:
          FlushNvram(*vs, current_rom_path);
          vs.reset();
          rewind_buffer.clear();
          state = AppState::RomList;
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
    if (menu_bit != 0 && (in.down & menu_bit)) {
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
    if (save_bit != 0 && (in.down & save_bit)) {
      SaveStateToSlot(*vs, current_rom_path, switch_settings_last_slot(current_rom_path));
    }
    const uint64_t load_bit = switch_settings_button_bit(g_settings.action_binding[(int)GameAction::LoadState]);
    if (load_bit != 0 && (in.down & load_bit)) {
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

    // Rewind: held-based, no menu toggle (Android doesn't have one for this
    // either).
    const uint64_t rewind_bit = switch_settings_button_bit(g_settings.action_binding[(int)GameAction::Rewind]);
    const bool rewinding = rewind_bit != 0 && (in.held & rewind_bit);

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
        eglSwapBuffers(s_display, s_surface);
      }
    } else {
      vs->SetInput(in.joy_x, in.joy_y, in.buttons);
      if (p2_connected) vs->SetInput2(in2.joy_x, in2.joy_y, in2.buttons);
      const double run_frame_start_ms = NowMs();
      vs->RunFrame();
      switch_frameskip_record_frame_time(NowMs() - run_frame_start_ms);

      // Push this frame's snapshot *after* it ran (matches jni_bridge.cpp's
      // Loop() exactly - see kRewindMax's comment above).
      rewind_buffer.emplace_back();
      vs->SaveState(rewind_buffer.back());
      if (rewind_buffer.size() > kRewindMax) rewind_buffer.pop_front();

      {
        const int n = vs->DrainAudio(g_spu_buf, kSpuBufMax);
        switch_audio_push(g_spu_buf, n / 2);  // no-ops/self-heals internally if audio isn't up
      }

      if (switch_frameskip_should_render()) {
        switch_render_frame(vs->Framebuffer());
        eglSwapBuffers(s_display, s_surface);
      }
    }
  }

  if (vs) FlushNvram(*vs, current_rom_path);  // e.g. quitting via HOME/power while still playing
  vs.reset();
  switch_audio_shutdown();
  switch_menu_shutdown();
  switch_ui_shutdown();
  switch_render_shutdown();
  DeinitEgl();
  return EXIT_SUCCESS;
}
