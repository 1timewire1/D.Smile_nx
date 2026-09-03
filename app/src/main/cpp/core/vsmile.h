#pragma once
#include "common.h"
#include "spg200.h"

namespace dsmile {

class VSmile;

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
  void SetCts(bool state);            // console -> controller select
  void Rx(u8 byte);                   // console -> controller byte (via UART TX)
  void TxDone();                      // console UART finished receiving our byte
  // buttons bitmask: bit0 enter, bit1 back/exit, bit2 help, bit3 abc,
  //                  bit4 red, bit5 yellow, bit6 blue, bit7 green
  void UpdateInput(int joy_x, int joy_y, u32 buttons);  // x/y in -5..5
  bool RtsIdle() const { return rts_idle_; }
  u8 LedState() const { return leds_; }

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

 private:
  void QueueTx(u8 b);
  void StartTx();
  void QueueJoyUpdates();
  void SetRtsActive(bool active);

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
  // Not serialized: phase only, safe to restart on load.
  s64 held_repeat_counter_ = kHeldRepeatPeriod;

  static constexpr s64 kIdlePeriod = 27000000;      // 1 s keepalive
  static constexpr s64 kRtsTimeout = 13500000;      // 0.5 s grant timeout
  static constexpr s64 kTxStartDelay = 97200;       // 3.6 ms after CTS
  static constexpr s64 kHeldRepeatPeriod = 2700000; // 0.1 s mode-0 auto-repeat
};

// The V.Smile console: SPG200 + cartridge + sysrom + controller wiring.
class VSmile : public MachineIo {
 public:
  VSmile();

  bool LoadCart(const u8* data, size_t size_bytes);     // raw LE dump
  void LoadSysrom(const u8* data, size_t size_bytes);   // optional real BIOS
  void SetRegion(int code) { region_ = code & 0xF; }
  void SetVtechLogo(bool on) { vtech_logo_ = on; }
  void SetAccurate(bool on) { spg_.GetPpu().SetAccurate(on); }

  void Reset(bool pal);
  void RunFrame();

  const u16* Framebuffer() { return spg_.GetPpu().Framebuffer(); }
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

  // Used by VSmileJoy
  Spg200& Soc() { return spg_; }

 private:
  void MakeDummySysrom();

  Spg200 spg_;
  VSmileJoy joy_;
  VSmileJoy joy2_;
  std::vector<u16> cart_;
  std::vector<u16> sysrom_;
  std::vector<u16> nvram_;  // empty unless the loaded cart has onboard SRAM
  u32 cart_checksum_ = 0;
  int region_ = 0xF;        // US English
  bool vtech_logo_ = true;
  bool has_real_sysrom_ = false;
  bool cts0_ = false, cts1_ = false;
  bool on_ = false, off_ = false, restart_ = false;
  int auto_on_frames_ = 0;  // simulated power-button pulse after reset
  bool joy2_connected_ = false;  // see SetPlayer2Connected() above
};

}  // namespace dsmile
