#include "vsmile.h"

#include <algorithm>

#include "state.h"

namespace dsmile {

namespace {
constexpr u32 kStateMagic = 0x44534D53;  // "DSMS"
constexpr u32 kStateVersion = 6;  // v6 adds joy2_ and nvram_ blocks
constexpr u32 kMinStateVersion = 3;  // v3 ADC block and v4 pad block are migrated on load

u32 Fnv1a(const u8* data, size_t n) {
  u32 h = 2166136261u;
  for (size_t i = 0; i < n; i++) { h ^= data[i]; h *= 16777619u; }
  return h;
}

// Standard zlib/PNG CRC-32 (reflected, poly 0xEDB88320) - used only to
// recognize which cartridges have onboard NVRAM (below), matching MAME's
// own hash/vsmile_cart.xml checksums for the same dumps.
u32 Crc32(const u8* data, size_t n) {
  static u32 table[256];
  static bool table_ready = false;
  if (!table_ready) {
    for (u32 i = 0; i < 256; i++) {
      u32 c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    table_ready = true;
  }
  u32 crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// The only known V.Smile cartridges with onboard battery-backed SRAM: one
// title ("Art Studio", a drawing game whose saved pictures live on the
// cart itself) and its regional re-releases - source: MAME's
// hash/vsmile_cart.xml, the 5 entries with
// <feature name="slot" value="vsmile_nvram"/>. Every other cart is plain
// ROM. Matched by CRC-32 of the raw dump, same as MAME's own softlist
// hashes - not detectable from the ROM's own contents, there's no header
// field for it, same as real hardware has no way to ask a cart "do you
// have SRAM" other than knowing which PCB it's built on.
constexpr u32 kNvramCartCrc[] = {
    0x3bbfb917u,  // techart   - V.Smile Art Studio (USA)
    0xae5f4fe2u,  // techartge - V.Smile Zeichenatelier (Germany)
    0xfec3361eu,  // techartfr - V.Smile Studio De Dessin (France)
    0x27449e15u,  // techartse - V.Smile Tecknarstudio (Sweden)
    0xf9911730u,  // techartsp - V.Smile Estudio De Arte (Spain)
};

bool IsNvramCart(const u8* data, size_t size_bytes) {
  const u32 crc = Crc32(data, size_bytes);
  for (u32 known : kNvramCartCrc) {
    if (crc == known) return true;
  }
  return false;
}

// Matches Spg200::ExtRead/ExtWrite's own `& 0x1FFFF` mask on the NVRAM bank
// (spg200.cpp) - the addressable window that decode logic already exposes,
// which is what has to be satisfied regardless of the real chip's exact
// physical capacity (MAME's own softlist declares the real SRAM as 128KB/
// 65536 words, smaller than this 256KB/131072-word window - real hardware
// presumably aliases the difference away on the actual PCB, a detail this
// core doesn't model since it isn't observable from software behavior).
constexpr size_t kNvramWords = 0x20000;
}  // namespace

// ---------------- VSmileJoy ----------------

void VSmileJoy::Reset() {
  fifo_len_ = fifo_head_ = 0;
  rts_idle_ = true;
  cts_ = false;
  tx_busy_ = false;
  tx_starting_ = false;
  active_ = false;
  leds_ = 0;
  probe_history_[0] = probe_history_[1] = 0;
  cur_x_ = cur_y_ = 0;
  cur_buttons_ = 0;
  sent_x_ = sent_y_ = 0;
  sent_buttons_ = 0;
  input_dirty_ = false;
  dump_pending_ = false;
  probed_ = false;
  report_mode_ = 6;
  idle_counter_ = kIdlePeriod;
  rts_timeout_ = kRtsTimeout;
  tx_start_counter_ = kTxStartDelay;
  held_repeat_counter_ = kHeldRepeatPeriod;
}

void VSmileJoy::SetRtsActive(bool active) {
  if (rts_idle_ == !active) return;  // no change
  rts_idle_ = !active;
  if (active) machine_.Soc().RaiseExtIrq(ext_irq_line_);  // falling edge -> EXT1/EXT2
}

void VSmileJoy::QueueTx(u8 b) {
  DTRACE("[joy] queue %02x (cts=%d busy=%d)\n", b, cts_, tx_busy_);
  if (fifo_len_ >= 16) {
    DTRACE("[joy] DROP %02x (fifo full)\n", b);
    return;
  }
  const bool was_empty = fifo_len_ == 0;
  fifo_[(fifo_head_ + fifo_len_) & 15] = b;
  fifo_len_++;
  if (was_empty) {
    SetRtsActive(true);
    if (cts_) {
      if (!tx_busy_ && !tx_starting_) {
        tx_starting_ = true;
        tx_start_counter_ = kTxStartDelay;
      }
    } else {
      rts_timeout_ = kRtsTimeout;
    }
  }
  idle_counter_ = kIdlePeriod;
}

void VSmileJoy::StartTx() {
  if (tx_busy_ || fifo_len_ == 0) return;
  const u8 b = fifo_[fifo_head_];
  fifo_head_ = (fifo_head_ + 1) & 15;
  fifo_len_--;
  if (fifo_len_ == 0) SetRtsActive(false);
  DTRACE("[joy] tx %02x\n", b);
  machine_.Soc().UartRxStart(b);
  tx_busy_ = true;
}

// Console UART finished receiving our byte: flow-control feedback.
void VSmileJoy::TxDone() {
  DTRACE("[joy] txdone (busy=%d)\n", tx_busy_);
  if (!tx_busy_) return;
  active_ = true;
  tx_busy_ = false;
  if (cts_ && fifo_len_ > 0) StartTx();
}


void VSmileJoy::SetCts(bool state) {
  cts_ = state;
  if (cts_ && fifo_len_ > 0 && !tx_busy_ && !tx_starting_) {
    tx_starting_ = true;
    tx_start_counter_ = kTxStartDelay;
  }
}

void VSmileJoy::Rx(u8 byte) {
  DTRACE("[joy] rx %02x\n", byte);
  const u8 hi = byte & 0xF0;
  if (hi == 0x60) {
    leds_ = byte & 0x0F;
  } else if (hi == 0x70 || hi == 0xB0) {
    probe_history_[0] = (hi == 0x70) ? 0 : probe_history_[1];
    probe_history_[1] = byte & 0x0F;
    QueueTx(0xB0 | (((0u - probe_history_[0] - probe_history_[1]) ^ 0xA) & 0xF));
    probed_ = true;
  } else if (hi == 0xD0) {
    // Reporting-mode command, sent by games (as an 0xEx/0xDx pair) after
    // each of our 0x55 keep-alives. The low nibble selects how the pad
    // reports: games use D6 (or D4) in menus and scenes, and hold-to-drive
    // minigames switch to D0 (SpongeBob's boating lesson). Mode 0 = fast
    // auto-repeat of held function buttons; other modes = change-only.
    // Deliberately no other response: answering these with state dumps
    // congested the line and got real presses flushed with the queue.
    report_mode_ = byte & 0x0F;
  }
}

void VSmileJoy::UpdateInput(int joy_x, int joy_y, u32 buttons) {
  cur_x_ = std::max(-5, std::min(5, joy_x));
  cur_y_ = std::max(-5, std::min(5, joy_y));
  cur_buttons_ = buttons;
  input_dirty_ = true;
}

// Converts pending input changes into protocol bytes. Only runs once the
// controller is active (game has completed at least one transfer with us).
void VSmileJoy::QueueJoyUpdates() {
  DTRACE("[joy] queue-input x=%d y=%d b=%u\n", cur_x_, cur_y_, cur_buttons_);
  const bool update_fn = (cur_buttons_ & 0xF) != (sent_buttons_ & 0xF);
  const bool update_colors = (cur_buttons_ >> 4) != (sent_buttons_ >> 4);
  const bool update_joy = cur_x_ != sent_x_ || cur_y_ != sent_y_;
  if (!update_fn && !update_colors && !update_joy) {
    input_dirty_ = false;
    return;
  }

  if (update_fn) {
    u8 b = 0xA0;
    if (cur_buttons_ & 1) b = 0xA1;        // enter/OK
    else if (cur_buttons_ & 2) b = 0xA2;   // back/exit
    else if (cur_buttons_ & 4) b = 0xA3;   // help
    else if (cur_buttons_ & 8) b = 0xA4;   // abc / learning zone
    QueueTx(b);
  }
  if (update_colors) {
    // Protocol color bit order: bit0 green, bit1 blue, bit2 yellow, bit3 red.
    u8 b = 0x90;
    if (cur_buttons_ & (1u << 7)) b |= 1;  // green
    if (cur_buttons_ & (1u << 6)) b |= 2;  // blue
    if (cur_buttons_ & (1u << 5)) b |= 4;  // yellow
    if (cur_buttons_ & (1u << 4)) b |= 8;  // red
    QueueTx(b);
  }
  if (update_joy) {
    u8 xb = 0xC0, yb = 0x80;
    if (cur_x_ > 0) xb = 0xC3 + (cur_x_ - 1);
    else if (cur_x_ < 0) xb = 0xCB + (-cur_x_ - 1);
    if (cur_y_ > 0) yb = 0x83 + (cur_y_ - 1);
    else if (cur_y_ < 0) yb = 0x8B + (-cur_y_ - 1);
    QueueTx(xb);
    QueueTx(yb);
  }

  sent_x_ = cur_x_;
  sent_y_ = cur_y_;
  sent_buttons_ = cur_buttons_;
  input_dirty_ = false;
}

void VSmileJoy::RunCycles(int cycles) {
  if (!tx_busy_) {
    idle_counter_ -= cycles;
    if (idle_counter_ <= 0) {
      idle_counter_ = kIdlePeriod;
      QueueTx(0x55);
    }
  }

  // Mode 0 (hold-to-drive minigames): re-send held function buttons fast.
  held_repeat_counter_ -= cycles;
  if (held_repeat_counter_ <= 0) {
    held_repeat_counter_ = kHeldRepeatPeriod;
    if (report_mode_ == 0 && active_ && probed_ && (cur_buttons_ & 0xF) != 0 &&
        fifo_len_ == 0 && !tx_busy_ && !tx_starting_) {
      if (cur_buttons_ & 1) QueueTx(0xA1);
      if (cur_buttons_ & 2) QueueTx(0xA2);
      if (cur_buttons_ & 4) QueueTx(0xA3);
      if (cur_buttons_ & 8) QueueTx(0xA4);
    }
  }

  if (tx_starting_) {
    tx_start_counter_ -= cycles;
    if (tx_start_counter_ <= 0) {
      tx_starting_ = false;
      StartTx();
    }
  }

  // Waiting for a CTS grant that never comes.
  if (!rts_idle_ && !cts_ && !tx_starting_ && !tx_busy_) {
    rts_timeout_ -= cycles;
    if (rts_timeout_ <= 0) {
      rts_timeout_ = kRtsTimeout;
      SetRtsActive(false);
      if (active_) {
        cur_x_ = cur_y_ = 0;
        cur_buttons_ = 0;
        sent_x_ = sent_y_ = 0;
        sent_buttons_ = 0;
        input_dirty_ = false;
        probe_history_[0] = probe_history_[1] = 0;
        idle_counter_ = kIdlePeriod;
      }
      active_ = false;
      // probed_ and report_mode_ deliberately survive this reset: they are
      // session facts (the console only probes every ~17 s and re-commands
      // the mode only after our keep-alives), so wiping them here left the
      // pad ignoring holds for many seconds after any brief console stall.
      dump_pending_ = false;
      fifo_len_ = fifo_head_ = 0;
      QueueTx(0x55);
    }
  }

  if (active_ && input_dirty_) QueueJoyUpdates();
}

void VSmileJoy::SaveState(StateWriter& w) const {
  w.Arr8(fifo_, 16);
  w.U32((u32)fifo_len_); w.U32((u32)fifo_head_);
  w.B(rts_idle_); w.B(cts_); w.B(tx_busy_); w.B(tx_starting_); w.B(active_);
  w.U8(leds_);
  w.Arr8(probe_history_, 2);
  w.U32((u32)cur_x_); w.U32((u32)cur_y_); w.U32(cur_buttons_);
  w.U32((u32)sent_x_); w.U32((u32)sent_y_); w.U32(sent_buttons_);
  w.B(input_dirty_);
  w.S64(idle_counter_); w.S64(rts_timeout_); w.S64(tx_start_counter_);
  w.B(dump_pending_); w.B(probed_); w.U8(report_mode_);
}

void VSmileJoy::LoadState(StateReader& r) {
  r.Arr8(fifo_, 16);
  fifo_len_ = (int)r.U32(); fifo_head_ = (int)r.U32();
  rts_idle_ = r.B(); cts_ = r.B(); tx_busy_ = r.B(); tx_starting_ = r.B(); active_ = r.B();
  leds_ = r.U8();
  r.Arr8(probe_history_, 2);
  cur_x_ = (int)r.U32(); cur_y_ = (int)r.U32(); cur_buttons_ = r.U32();
  sent_x_ = (int)r.U32(); sent_y_ = (int)r.U32(); sent_buttons_ = r.U32();
  input_dirty_ = r.B();
  idle_counter_ = r.S64(); rts_timeout_ = r.S64(); tx_start_counter_ = r.S64();
  if (r.version >= 5) {
    dump_pending_ = r.B();
    probed_ = r.B();
    report_mode_ = r.U8();
  } else {  // older saves came from live sessions: detection had completed
    dump_pending_ = false;
    probed_ = true;
    report_mode_ = 6;
  }
}

// ---------------- VSmile ----------------

VSmile::VSmile() : spg_(*this), joy_(*this, 0), joy2_(*this, 1) {
  MakeDummySysrom();
}

void VSmile::MakeDummySysrom() {
  sysrom_.assign(0x100000, 0);
  // Stuff the sysrom entry-point vector area so games that call into the BIOS
  // land on harmless zero-filled memory.
  for (u32 i = 0xFFFC0; i < 0xFFFDC; i += 2) sysrom_[i + 1] = 0x0031;
  spg_.SetSysrom(sysrom_.data());
}

bool VSmile::LoadCart(const u8* data, size_t size_bytes) {
  const size_t words = size_bytes / 2;
  if (words == 0 || words > 0x800000) return false;
  size_t cap = 0x400000;  // at least 4M words (8 MiB)
  while (cap < words) cap <<= 1;
  cart_.assign(cap, 0);
  for (size_t i = 0; i < words; i++) {
    cart_[i] = (u16)(data[i * 2] | (data[i * 2 + 1] << 8));  // little-endian words
  }
  nvram_.clear();
  if (IsNvramCart(data, size_bytes)) nvram_.assign(kNvramWords, 0);
  spg_.SetCart(cart_.data(), (u32)cap - 1, nvram_.empty() ? nullptr : nvram_.data());
  cart_checksum_ = Fnv1a(data, std::min<size_t>(size_bytes, 0x10000));
  return true;
}

void VSmile::SetNvram(const u8* data, size_t size_bytes) {
  if (nvram_.empty()) return;
  const size_t words = std::min(size_bytes / 2, nvram_.size());
  for (size_t i = 0; i < words; i++) {
    nvram_[i] = (u16)(data[i * 2] | (data[i * 2 + 1] << 8));
  }
}

std::vector<u8> VSmile::GetNvram() const {
  std::vector<u8> out;
  if (nvram_.empty()) return out;
  out.resize(nvram_.size() * 2);
  for (size_t i = 0; i < nvram_.size(); i++) {
    out[i * 2] = (u8)(nvram_[i] & 0xFF);
    out[i * 2 + 1] = (u8)(nvram_[i] >> 8);
  }
  return out;
}

void VSmile::SaveState(std::vector<u8>& out) const {
  out.clear();
  StateWriter w(out);
  w.U32(kStateMagic);
  w.U32(kStateVersion);
  w.U32(cart_checksum_);
  spg_.SaveState(w);
  joy_.SaveState(w);
  joy2_.SaveState(w);
  w.B(cts0_); w.B(cts1_);
  w.B(on_); w.B(off_); w.B(restart_);
  w.U32((u32)auto_on_frames_);
  if (!nvram_.empty()) w.Arr16(nvram_.data(), nvram_.size());
}

bool VSmile::LoadState(const u8* data, size_t size) {
  StateReader r(data, size);
  if (r.U32() != kStateMagic) return false;
  r.version = r.U32();
  if (r.version < kMinStateVersion || r.version > kStateVersion) return false;
  if (r.U32() != cart_checksum_) return false;
  spg_.LoadState(r);
  joy_.LoadState(r);
  if (r.version >= 6) {
    joy2_.LoadState(r);
  } else {  // no second-controller block on older saves - nothing to restore
    joy2_.Reset();
  }
  cts0_ = r.B(); cts1_ = r.B();
  on_ = r.B(); off_ = r.B(); restart_ = r.B();
  auto_on_frames_ = (int)r.U32();
  // Cart identity (and so whether nvram_ is populated at all) is guarded by
  // cart_checksum_ above, already matched by this point - safe to assume
  // nvram_'s emptiness agrees between the state being loaded and the cart
  // already loaded into this session.
  if (r.version >= 6 && !nvram_.empty()) r.Arr16(nvram_.data(), nvram_.size());
  return r.ok;
}

void VSmile::LoadSysrom(const u8* data, size_t size_bytes) {
  has_real_sysrom_ = true;
  const size_t words = std::min<size_t>(size_bytes / 2, 0x100000);
  sysrom_.assign(0x100000, 0);
  for (size_t i = 0; i < words; i++) {
    sysrom_[i] = (u16)(data[i * 2] | (data[i * 2 + 1] << 8));
  }
  spg_.SetSysrom(sysrom_.data());
}

void VSmile::Reset(bool pal) {
  cts0_ = cts1_ = false;
  on_ = off_ = restart_ = false;
  auto_on_frames_ = 20;  // hold the power button through early boot polling
  joy_.Reset();
  joy2_.Reset();
  spg_.Reset(pal);
  // Real hardware boots with both controller-request lines pending; the game's
  // controller driver arms itself off this.
  spg_.RaiseExtIrq(0);
  spg_.RaiseExtIrq(1);
}

void VSmile::RunFrame() {
  spg_.RunFrame();
  if (auto_on_frames_ > 0) auto_on_frames_--;
}

u16 VSmile::GpioIn(int port) {
  switch (port) {
    case 0:
      return 0;
    case 1: {
      // Active-low console buttons: bit3 restart, bit6 ON, bit7 OFF.
      u16 v = 0x00C8;
      if (restart_) v &= (u16)~0x0008;
      if (on_ || auto_on_frames_ > 0) v &= (u16)~0x0040;
      if (off_) v &= (u16)~0x0080;
      return v;
    }
    default: {
      u16 v = (u16)(region_ & 0xF);
      // Port C bit4: intro jumper. 1 = cart plays its branded intro,
      // 0 = skip straight to the game menu (verified empirically).
      if (vtech_logo_) v |= 0x0010;
      v |= 0x0020;
      if (cts0_) v |= 0x0100;
      if (cts1_) v |= 0x0200;
      if (joy_.RtsIdle()) v |= 0x0400;   // controller 1 RTS (1 = idle)
      if (joy2_.RtsIdle()) v |= 0x1000;  // controller 2 RTS (1 = idle) - stays
                                         // idle forever if SetInput2() is
                                         // never called, same as an empty port
      v |= 0x6000;
      return v;
    }
  }
}

void VSmile::GpioOut(int port, u16 data, u16 mask) {
  if (port == 1) {
    if (mask & 0x0002) spg_.SetCs2(!(data & 0x0002));
  } else if (port == 2) {
    DTRACE("[gpio] portC out data=%04x mask=%04x\n", data, mask);
    if (mask & 0x0100) {
      cts0_ = data & 0x0100;
      joy_.SetCts(cts0_);
    }
    if (mask & 0x0200) {
      cts1_ = data & 0x0200;
      joy2_.SetCts(cts1_);
    }
  }
}

void VSmile::UartTx(u8 byte) {
  if (cts0_) joy_.Rx(byte);
  if (cts1_) joy2_.Rx(byte);
}

u16 VSmile::AdcIn(int ch) {
  return ch == 1 ? 0x03FF : 0;  // full battery
}

void VSmile::RunCycles(int cycles) {
  joy_.RunCycles(cycles);
  if (joy2_connected_) joy2_.RunCycles(cycles);
}

}  // namespace dsmile
