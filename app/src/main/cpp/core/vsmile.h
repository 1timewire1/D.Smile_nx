#pragma once
#include <deque>

#include "common.h"
#include "spg200.h"

namespace dsmile {

class VSmile;

// What is plugged into a controller port. The Smart Keyboard ("Clavier Tip
// Tap") speaks the same UART/RTS/CTS protocol as the joystick but announces
// itself with a power-up handshake and sends one byte per key press/release
// - see VSmileJoy's keyboard members below. Protocol sources: the
// vtech.pulkomandy.tk wiki's "controllers" page (logic-analyzer captures)
// and MAME's src/devices/bus/vsmile/keyboard.cpp, which agree.
//
// The V.Smile Motion controller is a joystick that identifies itself with a
// 56 keep-alive instead of 55, answers the console's "8n" report-mode
// commands with "Dn", and while a mode is on streams accelerometer packets
// (see VSmileJoy::QueueMotionPacket). Games only offer motion controls on a
// Motion console (VSmile::SetMotionConsole). Worked out from Action Mania's
// controller library - see reference/findings.md section 4.
// Mat: the Jammin' Gym Class dance mat. It keeps the joystick's protocol
// and 55 ID but sends its own bytes (QueueMatUpdates()).
// The console board, as read from port A's IOA14/IOA15 (the wiki's "io"
// page, from the V.Smile Motion schematics): V.Smile and V.Smile Pocket
// 0/x, V.Smile Motion 1/0, Cyber Pocket 1/1.
enum class ConsoleType : u8 { Standard = 0, Motion = 1, CyberPocket = 2 };

enum class PortDevice : u8 { Joystick = 0, SmartKeyboard = 1, Motion = 2, Mat = 3 };

#ifdef DSMILE_TEST_HOOKS
extern s64 g_test_motion_period;
#endif

// The V.Smile joystick: its own MCU speaking a serial protocol over the
// console UART, with RTS/CTS flow control on GPIO port C. Byte pacing is
// driven by the console UART's receive completion (TxDone), like real
// hardware flow control — never by a free-running timer.
class VSmileJoy {
 public:
  // ext_irq_line: which of the console's two controller-request IRQ lines
  // this instance's RTS transitions raise (0 for controller port 1, 1 for
  // port 2 - see Spg200::RaiseExtIrq()). Each physical V.Smile controller
  // port has its own IRQ line, so a second controller needs its own
  // VSmileJoy wired to the second line, not just its own instance.
  VSmileJoy(VSmile& machine, int ext_irq_line) : machine_(machine), ext_irq_line_(ext_irq_line) {}

  void Reset();
  void RunCycles(int cycles);
  // How long RunCycles() can be put off: its nearest counter deadline in the
  // current state, never later than the real one (see Spg200::TickIo).
  s64 NextEvent() const;
  void SetCts(bool state);            // console -> controller select
  void RearmRts();
  // console -> controller byte (via UART TX). The console's UART line is
  // shared by both ports; `selected` is whether this port's CTS is raised.
  void Rx(u8 byte, bool selected);
  void TxDone();                      // console UART finished receiving our byte
  // buttons bitmask: bit0 enter, bit1 back/exit, bit2 help, bit3 abc,
  //                  bit4 red, bit5 yellow, bit6 blue, bit7 green
  // As a Smart Keyboard, only bits 0-2 (its OK/Quit/Help keys) and a
  // digital reading of x/y (its own small joystick) are used.
  void UpdateInput(int joy_x, int joy_y, u32 buttons);  // x/y in -5..5
  bool RtsIdle() const { return rts_idle_; }
  u8 LedState() const { return leds_; }

  // Swaps what is plugged into this port. A change re-plugs it (Reset()); the
  // same value again is a no-op, so it is safe to call every frame.
  // kb_language is the Smart Keyboard's handshake language byte: 0x40 US,
  // 0x41 UK, 0x42 French, 0x44 German.
  void SetDevice(PortDevice device, u8 kb_language);
  PortDevice Device() const { return device_; }
  // A Smart Keyboard key changed state. `code` is the key's protocol code
  // (the wiki's key table, e.g. 0x23 = the key right of Tab); Shift is 0xA9.
  // Ignored unless this port is a Smart Keyboard past its handshake.
  void KeyEvent(u8 code, bool pressed);
  // Motion controller accelerometer, one byte per axis (0x80 = 0g, about
  // 0x40 per g). Level and still: 0x80, 0x80, 0xC0. First axis = left/right.
  void UpdateMotion(int x, int y, int z);
  // Whether both ports have Motion controllers (see VSmile::RunCycles):
  // then each reports and re-sends its ID less often.
  void SetMotionShared(bool shared) {
    motion_period_ = shared ? kMotionPeriodShared : kMotionPeriod;
    motion_id_period_ = shared ? kMotionIdPeriodShared : kMotionIdPeriodAlone;
  }

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

 private:
  void QueueTx(u8 b);
  void StartTx();
  void QueueJoyUpdates();
  void QueueMatUpdates();
  void SetRtsActive(bool active);
  void KbRunCycles(int cycles);
  void KbRx(u8 byte, bool selected);
  void QueueKbUpdates();
  u8 IdleByte() const;
  void QueueMotionPacket();
  void DropMotionBytes();

  VSmile& machine_;
  int ext_irq_line_;
  u8 fifo_[16]{};
  int fifo_len_ = 0, fifo_head_ = 0;
  bool rts_idle_ = true;     // true = no transfer request
  bool cts_ = false;         // console grant
  bool tx_busy_ = false;     // byte in flight to console UART
  bool tx_starting_ = false; // 3.6 ms CTS-grant delay running
  bool active_ = false;      // controller considered alive by game
  u8 leds_ = 0;
  u8 probe_history_[2]{};
  // current vs last-transmitted input state
  int cur_x_ = 0, cur_y_ = 0;
  u32 cur_buttons_ = 0;
  int sent_x_ = 0, sent_y_ = 0;
  u32 sent_buttons_ = 0;
  bool input_dirty_ = false;
  bool dump_pending_ = false;  // unused since v0.4.3; kept for the v5 state layout
  bool probed_ = false;   // console completed a probe handshake since reset
  u8 report_mode_ = 6;    // 0xDx low nibble; 0 = fast held auto-repeat
  // timers (cycle countdowns)
  s64 idle_counter_ = kIdlePeriod;
  s64 rts_timeout_ = kRtsTimeout;
  s64 tx_start_counter_ = kTxStartDelay;
  s64 rts_rearm_counter_ = 0;  // > 0: RTS released, re-request the line when it runs out
  s64 rts_retry_counter_ = kRtsRetry;  // re-request the line if no CTS by then
  // Not serialized: phase only, safe to restart on load.
  s64 held_repeat_counter_ = kHeldRepeatPeriod;

  // Smart Keyboard. Power-up (MAME's timing): every 300 ms raise RTS for
  // 12.2 ms; if the console selected us in that window, send 52 52 52
  // ("I am a keyboard"). The console answers with 5 bytes (02 02 E6 D6 60),
  // we reply with the language byte, and from then on it's like a joystick:
  // 55 keep-alives, 7x/Bx probe replies, and key codes instead of pad bytes.
  enum KbState : u8 { kKbHello = 0, kKbHandshake = 1, kKbRunning = 2 };
  PortDevice device_ = PortDevice::Joystick;  // survives Reset()
  u8 kb_language_ = 0x40;                     // survives Reset()
  u8 kb_state_ = kKbHello;
  u8 kb_handshake_rx_ = 0;      // console bytes seen since our 52 52 52
  bool kb_hello_window_ = false;
  s64 kb_hello_counter_ = kKbHelloPeriod;
  // Key bytes waiting for room in fifo_. Not serialized: a save state
  // drops at most a few in-flight keystrokes.
  std::deque<u8> kb_pending_;

  static constexpr s64 kIdlePeriod = 27000000;      // 1 s keepalive
  static constexpr s64 kRtsTimeout = 13500000;      // 0.5 s grant timeout
  static constexpr s64 kTxStartDelay = 97200;       // 3.6 ms after CTS
  static constexpr s64 kRtsRearmDelay = 27000;      // 1 ms RTS release before asking again
  static constexpr s64 kRtsRetry = 324000;          // 12 ms without CTS: ask again
  static constexpr s64 kHeldRepeatPeriod = 2700000; // 0.1 s mode-0 auto-repeat
  static constexpr s64 kKbHelloPeriod = 8100000;    // 300 ms between hellos
  static constexpr s64 kKbHelloWindow = 329400;     // 12.2 ms RTS pulse
  static constexpr size_t kKbPendingMax = 64;

  // Motion controller (device_ == Motion).
  u8 motion_mode_ = 0;               // 0 = no reports, 1-4 = report format
  u8 motion_[3] = {0x80, 0x80, 0xC0};  // survives Reset(): it's live input
  s64 motion_counter_ = kMotionPeriod;  // idle time left before the next report
  s64 motion_id_counter_ = kMotionIdPeriodAlone; // re-send the 56 ID with a report this often
  s64 motion_pause_ = 0;                // no reports right after a console 05
  u8 motion_packet_left_ = 0;           // unsent bytes of the queued report (at fifo head)
  static constexpr s64 kMotionPeriod = 1080000;  // 40 ms of idle line between reports
  static constexpr s64 kMotionPause = 810000;     // 30 ms
  s64 motion_id_period_ = kMotionIdPeriodAlone;
  s64 motion_period_ = kMotionPeriod;
  // Two streaming controllers need half the report rate (80 ms): at 40 ms
  // the console stalls after nearly every exchange. The RTS re-arm (see
  // RearmRts()) lifted that, but it broke Toy Story 3's Motion controls, so
  // Motion controllers don't re-arm and this stays as in 1.1.0. The ID goes
  // out only once a second with two: every 56 makes the console redo its
  // Motion setup (05/8n/9n), and at 250 ms that cost each port ~2.5 reports
  // a second.
  static constexpr s64 kMotionPeriodShared = 2160000;
  static constexpr s64 kMotionIdPeriodAlone = 6750000;    // 250 ms
  static constexpr s64 kMotionIdPeriodShared = 27000000;  // 1 s with two controllers
  static constexpr int kUnsent = 99;  // sent_x_/sent_y_ "never sent": forces a resend
};

// The V.Smile console: SPG200 + cartridge + sysrom + controller wiring.
class VSmile : public MachineIo {
 public:
  VSmile();

  bool LoadCart(const u8* data, size_t size_bytes);     // raw LE dump
  void LoadSysrom(const u8* data, size_t size_bytes);   // optional real BIOS
  void SetRegion(int code) { region_ = code & 0xF; }
  void SetVtechLogo(bool on) { vtech_logo_ = on; }
  // Which console board to present (see ConsoleType / GpioIn): its port A
  // pins tell the BIOS which boot animation to play and Motion carts
  // whether motion controls exist. SetMotionConsole() is the common case.
  void SetConsoleType(ConsoleType type) { console_type_ = type; }
  void SetMotionConsole(bool on) { console_type_ = on ? ConsoleType::Motion : ConsoleType::Standard; }
  ConsoleType GetConsoleType() const { return console_type_; }
  void SetAccurate(bool on) { spg_.GetPpu().SetAccurate(on); }

  void Reset(bool pal);
  // The RESTART button (on the Motion and later consoles, ON/RESTART): it
  // pulls the SoC's /RST line (Motion schematic: START -> D4 -> /RST; IOB3
  // "RESTART" only goes to a test point). A hardware reset, not a
  // power-on - RAM keeps its contents, so a game started from standby this
  // way still finds its saved place. Keeps the current video standard.
  void Restart();
  void RunFrame();

  // Black while in standby (the console is "off": video output disabled).
  const u16* Framebuffer() {
    static const u16 kBlack[320 * 240] = {};
    return spg_.Sleeping() ? kBlack : spg_.GetPpu().Framebuffer();
  }
  bool Sleeping() const { return spg_.Sleeping(); }
  int DrainAudio(s16* out, int max_samples) { return spg_.GetSpu().DrainAudio(out, max_samples); }

  void SetInput(int joy_x, int joy_y, u32 buttons) { joy_.UpdateInput(joy_x, joy_y, buttons); }
  // Controller port 2 - only meaningful while SetPlayer2Connected(true) is
  // in effect (see below); a game that never probes port 2 just never sees
  // it, same as an empty port on real hardware.
  void SetInput2(int joy_x, int joy_y, u32 buttons) { joy2_.UpdateInput(joy_x, joy_y, buttons); }
  // Whether anything is actually plugged into controller port 2 right now -
  // gates whether joy2_ participates in the UART/RTS protocol at all
  // (RunCycles() below). Without this, joy2_ would keep sending its
  // periodic keepalive/probe traffic even with nothing really connected on
  // the platform side, since that timing is internal to VSmileJoy and
  // doesn't otherwise know whether a platform is actually feeding it real
  // input - which would make a game see "something's in port 2" even when
  // there's no second gamepad at all. Expected to be called once per frame
  // with the platform's own live connection state (e.g. a Switch build's
  // padIsConnected() on a second PadState), not just once when a second
  // controller first appears.
  void SetPlayer2Connected(bool connected) { joy2_connected_ = connected; }
  // What is plugged into controller port 1 (see PortDevice). Safe to call
  // every frame; a change re-plugs the port.
  void SetPort1Device(PortDevice device, u8 kb_language) { joy_.SetDevice(device, kb_language); }
  // Smart Keyboard key press/release on port 1 (see VSmileJoy::KeyEvent).
  void KeyboardKey(u8 code, bool pressed) { joy_.KeyEvent(code, pressed); }
  // Motion controller accelerometer on port 1 (see VSmileJoy::UpdateMotion).
  void SetMotion(int x, int y, int z) { joy_.UpdateMotion(x, y, z); }
  // The same for controller port 2 (joystick or Motion controller; the Smart
  // Keyboard is port 1 only). On the Motion console both wireless
  // controllers are presented to the SoC as ordinary ports 1 and 2 - each
  // controller's P1/P2 switch picks which - so a second Motion controller is
  // simply a second port with its own ID, modes and reports.
  void SetPort2Device(PortDevice device) {
    joy2_.SetDevice(device == PortDevice::SmartKeyboard ? PortDevice::Joystick : device, 0x40);
  }
  void SetMotion2(int x, int y, int z) { joy2_.UpdateMotion(x, y, z); }
  void SetConsoleButtons(bool on, bool off, bool restart) {
    on_ = on; off_ = off; restart_ = restart;
  }

  // Save states: versioned blob, cart-checksum guarded.
  void SaveState(std::vector<u8>& out) const;
  bool LoadState(const u8* data, size_t size);

  u8 Leds() const { return joy_.LedState(); }
  u8 Leds2() const { return joy2_.LedState(); }

  // Cartridge-onboard battery-backed SRAM - only a handful of real V.Smile
  // titles have this (VTech's "Art Studio" and its regional variants);
  // LoadCart() recognizes them by CRC-32 (mirrors MAME's own
  // hash/vsmile_cart.xml vsmile_nvram entries) and allocates nvram_
  // automatically, same as how real hardware only has this memory on those
  // specific cart PCBs. HasNvram() is false for every other cart.
  bool HasNvram() const { return !nvram_.empty(); }
  // Loads a previously-saved NVRAM file's raw bytes into the cart's onboard
  // SRAM (little-endian, same convention as LoadCart/LoadSysrom) - call
  // once right after LoadCart(), before Reset(). A no-op if the loaded
  // cart has no NVRAM, or if size_bytes doesn't fully cover it.
  void SetNvram(const u8* data, size_t size_bytes);
  // Raw little-endian bytes of the cart's current NVRAM content, for the
  // platform layer to persist - empty if the loaded cart has none.
  std::vector<u8> GetNvram() const;

  // MachineIo
  u16 GpioIn(int port) override;
  void GpioOut(int port, u16 data, u16 mask) override;
  void UartTx(u8 byte) override;
  // Acks whichever controller's byte the console's UART just finished
  // receiving - deliberately unconditional, not gated on cts0_/cts1_'s
  // *current* value: those can change between a controller starting a
  // transmission and this callback firing for it, so gating on them here
  // could ack the wrong (or no) controller and leave the real sender's
  // tx_busy_ stuck true forever, silently wedging its output. Safe to call
  // both unconditionally - TxDone() itself already no-ops on a controller
  // that isn't mid-transmission (`if (!tx_busy_) return;`).
  void UartRxDone() override {
    joy_.TxDone();
    joy2_.TxDone();
  }
  u16 AdcIn(int ch) override;
  void RunCycles(int cycles) override;
  s64 NextEvent() override;

  // Used by VSmileJoy
  Spg200& Soc() { return spg_; }

 private:
  void MakeDummySysrom();

  void ResetMachine(bool pal, bool keep_ram);
  Spg200 spg_;
  VSmileJoy joy_;
  VSmileJoy joy2_;
  std::vector<u16> cart_;
  std::vector<u16> sysrom_;
  std::vector<u16> nvram_;  // empty unless the loaded cart has onboard SRAM
  u32 cart_checksum_ = 0;
  int region_ = 0xF;        // US English
  bool vtech_logo_ = true;
  ConsoleType console_type_ = ConsoleType::Standard;
  bool has_real_sysrom_ = false;
  bool cts0_ = false, cts1_ = false;
  bool on_ = false, off_ = false, restart_ = false;
  int auto_on_frames_ = 0;  // simulated power-button pulse after reset
  bool joy2_connected_ = false;  // see SetPlayer2Connected() above
};

}  // namespace dsmile
