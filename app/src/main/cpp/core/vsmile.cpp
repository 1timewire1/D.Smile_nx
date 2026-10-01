#include "vsmile.h"

#include <algorithm>

#include "state.h"

namespace dsmile {

namespace {
constexpr u32 kStateMagic = 0x44534D53;  // "DSMS"
constexpr u32 kStateVersion = 10;  // v6 joy2_/nvram_; v7 port device/keyboard; v8 motion; v9 standby; v10 console config
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



#ifdef DSMILE_TEST_HOOKS
s64 g_test_motion_period = 0;  // desktop experiments: override the report period (cycles)
#endif

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
  kb_state_ = kKbHello;
  kb_handshake_rx_ = 0;
  kb_hello_window_ = false;
  kb_hello_counter_ = kKbHelloPeriod;
  kb_pending_.clear();
  motion_mode_ = 0;
  motion_counter_ = kMotionPeriod;
  motion_id_counter_ = motion_id_period_;
  motion_pause_ = 0;
  motion_packet_left_ = 0;
}

void VSmileJoy::SetDevice(PortDevice device, u8 kb_language) {
  if (device == device_ && kb_language == kb_language_) return;
  device_ = device;
  kb_language_ = kb_language;
  Reset();
}

// The keep-alive byte doubles as the device ID: 55 joystick, 56 Motion
// controller (games map it to "device type 7" - Action Mania's 0x43EA5).
u8 VSmileJoy::IdleByte() const { return device_ == PortDevice::Motion ? 0x56 : 0x55; }

void VSmileJoy::UpdateMotion(int x, int y, int z) {
  motion_[0] = (u8)std::max(0, std::min(255, x));
  motion_[1] = (u8)std::max(0, std::min(255, y));
  motion_[2] = (u8)std::max(0, std::min(255, z));
}

// One accelerometer report. Modes 1/2: "43" then the three axis bytes as-is.
// Modes 3/4: "43" then four 6-bit bytes packing the same 24 bits, with the
// first and third axes stored inverted (the game's unpacking at 0x8309).
// Axis order as the game reads it: first byte = left/right tilt (80 level,
// higher = tilted right; Rapid Race steers on it), 1g is about 0x40.
// Removes the unsent part of the current report (at the fifo head) and any
// queued 56 ID bytes, keeping everything else in order. 56 is never a
// button/stick byte, so it identifies the ID unambiguously.
void VSmileJoy::DropMotionBytes() {
  u8 keep[16];
  int n = 0;
  for (int i = 0; i < fifo_len_; i++) {
    const u8 b = fifo_[(fifo_head_ + i) & 15];
    if (i < motion_packet_left_ || b == 0x56) continue;
    keep[n++] = b;
  }
  for (int i = 0; i < n; i++) fifo_[i] = keep[i];
  fifo_head_ = 0;
  fifo_len_ = n;
  motion_packet_left_ = 0;
}

void VSmileJoy::QueueMotionPacket() {
  const int before = fifo_len_;
  QueueTx(0x43);
  if (motion_mode_ <= 2) {
    for (u8 v : motion_) QueueTx(v);
  } else {
    const u32 bits =
        ((u32)(0xFF - motion_[0]) << 16) | ((u32)motion_[1] << 8) | (u32)(0xFF - motion_[2]);
    for (int shift = 18; shift >= 0; shift -= 6) QueueTx((u8)((bits >> shift) & 0x3F));
  }
  motion_packet_left_ = (u8)(fifo_len_ - before);
}

void VSmileJoy::SetRtsActive(bool active) {
  if (rts_idle_ == !active) return;  // no change
  rts_idle_ = !active;
  if (active) machine_.Soc().RaiseExtIrq(ext_irq_line_);  // falling edge -> EXT1/EXT2
}

void VSmileJoy::QueueTx(u8 b) {
  DTRACE("[joy%d] queue %02x (cts=%d busy=%d)\n", ext_irq_line_ + 1, b, cts_, tx_busy_);
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
  if (motion_packet_left_ > 0) motion_packet_left_--;  // reports sit at the fifo head
  if (fifo_len_ == 0) SetRtsActive(false);
  DTRACE("[joy%d] tx %02x\n", ext_irq_line_ + 1, b);
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

void VSmileJoy::Rx(u8 byte, bool selected) {
  if (device_ == PortDevice::SmartKeyboard) {
    KbRx(byte, selected);
    return;
  }
  if (!selected) return;
  DTRACE("[joy] rx %02x\n", byte);
  if (device_ == PortDevice::Motion && byte >= 0x80 && byte <= 0x84) {
    // Motion controller: "8n" selects report mode n (0 = off); acknowledge
    // with "Dn" or the game keeps re-sending it (library at 0x44B5B). Until
    // the ack arrives the game parses everything as ordinary pad bytes, so a
    // report still waiting to go out is dropped rather than sent first.
    if (motion_packet_left_ > 0 && fifo_len_ >= motion_packet_left_) {
      fifo_head_ = (fifo_head_ + motion_packet_left_) & 15;
      fifo_len_ -= motion_packet_left_;
      motion_packet_left_ = 0;
      if (fifo_len_ == 0) SetRtsActive(false);
    }
    motion_mode_ = byte & 0x0F;
    QueueTx((u8)(0xD0 | motion_mode_));
    return;
  }
  if (device_ == PortDevice::Motion && byte == 0x05) {
    // (Re)initialisation from the console: hold reports briefly so none
    // lands while it re-sends its mode/setting commands.
    motion_pause_ = kMotionPause;
  }
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
  if (device_ == PortDevice::Mat) {
    QueueMatUpdates();
    return;
  }
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

// The Jammin' Gym Class dance mat (MAME mat.cpp; matches the wiki's
// captures). Its 3x3 grid of panels plus Enter/Exit/Help reuse joystick
// bytes that mean something else on a pad, so it can't be driven by pad
// input as-is. The pad's inputs map onto the mat one to one:
//   stick/D-Pad up, down  -> Up / Down arrows  (colour bits 2 / 4)
//   stick/D-Pad right     -> Right arrow       (X byte CD)
//   stick/D-Pad left      -> Left arrow        (Y byte 8D)
//   Yellow, Red           -> those footprints  (X byte CB, Y byte 8B)
//   Green                 -> Green footprint   (colour bit 8)
//   ABC                   -> centre panel      (colour bit 1)
//   Blue                  -> Blue footprint    (A4)
//   Enter / Exit / Help   -> A1 / A2 / A3
// Yellow wins over Right and Red over Left when both are down, as in MAME.
// Only the parts that changed are sent; a resync (sent_x_ == kUnsent)
// resends everything.
namespace {
u8 MatXByte(int x, u32 b) { return (b & (1u << 5)) ? 0xCB : x > 0 ? 0xCD : 0xC0; }
u8 MatYByte(int x, u32 b) { return (b & (1u << 4)) ? 0x8B : x < 0 ? 0x8D : 0x80; }
u8 MatColours(int y, u32 b) {
  u8 c = 0x90;
  if (b & 8) c |= 1;          // ABC -> centre
  if (y > 0) c |= 2;          // up arrow
  if (y < 0) c |= 4;          // down arrow
  if (b & (1u << 7)) c |= 8;  // green footprint
  return c;
}
u32 MatFn(u32 b) { return (b & 7) | (((b >> 6) & 1) << 3); }  // enter, exit, help, blue
}  // namespace

void VSmileJoy::QueueMatUpdates() {
  const bool resend = sent_x_ == kUnsent;
  const u8 xb = MatXByte(cur_x_, cur_buttons_), yb = MatYByte(cur_x_, cur_buttons_);
  const u8 cb = MatColours(cur_y_, cur_buttons_);
  if (resend || xb != MatXByte(sent_x_, sent_buttons_)) QueueTx(xb);
  if (resend || yb != MatYByte(sent_x_, sent_buttons_)) QueueTx(yb);
  if (resend || cb != MatColours(sent_y_, sent_buttons_)) QueueTx(cb);
  const u32 fn = MatFn(cur_buttons_), sent_fn = resend ? 0 : MatFn(sent_buttons_);
  if (resend || fn != sent_fn) {
    const u32 pressed = fn & ~sent_fn;
    for (int i = 0; i < 4; i++) {
      if (pressed & (1u << i)) QueueTx((u8)(0xA1 + i));
    }
    if (fn == 0) QueueTx(0xA0);
  }
  sent_x_ = cur_x_;
  sent_y_ = cur_y_;
  sent_buttons_ = cur_buttons_;
  input_dirty_ = false;
}

void VSmileJoy::RunCycles(int cycles) {
  if (device_ == PortDevice::SmartKeyboard) {
    KbRunCycles(cycles);
    return;
  }
  if (device_ == PortDevice::Motion && motion_mode_ != 0) {
    // Reports go out only after the line has been idle for kMotionPeriod:
    // the console can only send its own commands while our RTS is idle, and
    // starving it of that makes the game time the controller out (its
    // disconnect check at 0x88FB), after which every report byte is misread
    // as ordinary pad input - including 5x bytes taken as a new device ID.
    motion_id_counter_ -= cycles;
    if (motion_pause_ > 0) {
      motion_pause_ -= cycles;
    } else if (rts_idle_ && fifo_len_ == 0 && !tx_busy_ && !tx_starting_) {
      if (motion_counter_ > 0) motion_counter_ -= cycles;
      if (motion_counter_ <= 0) {
        motion_counter_ = motion_period_;
#ifdef DSMILE_TEST_HOOKS
        if (g_test_motion_period > 0) motion_counter_ = g_test_motion_period;
#endif
        QueueMotionPacket();
        // Keep re-announcing the ID while streaming (the idle keep-alive
        // never fires then). Games have a race in their disconnect check
        // (Action Mania 0x88F3: "now - last_seen" read across an interrupt
        // that stamps a newer time, going negative), which a long report can
        // trigger now and then; after such a false disconnect the game
        // misreads reports as pad bytes until it re-identifies us, so the
        // ID goes out often to keep that window short. Sent after the report,
        // where the game's parser is back to plain bytes.
        if (motion_id_counter_ <= 0) {
          motion_id_counter_ = motion_id_period_;
          QueueTx(0x56);
        }
      }
    }
  }
  if (!tx_busy_) {
    idle_counter_ -= cycles;
    if (idle_counter_ <= 0) {
      idle_counter_ = kIdlePeriod;
      QueueTx(IdleByte());
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
      DTRACE("[joy%d] RTS timeout, dropping %d queued bytes\n", ext_irq_line_ + 1, fifo_len_);
      SetRtsActive(false);
      if (device_ == PortDevice::Motion && motion_mode_ != 0) {
        // A streaming Motion controller hits this whenever the console pauses
        // servicing (screen loads, its own 05/8n/9n commands). Drop only the
        // stale report and ID bytes - a fresh report follows - but keep
        // button/stick events queued, in order, and ask for the line again:
        // flushing them lost quick taps outright (press and release both
        // gone), which made menus ignore the player.
        DropMotionBytes();
        if (fifo_len_ > 0) SetRtsActive(true);
        return;
      }
      if (active_) {
        // The flushed bytes may have included a press or release, so don't
        // assume the console saw anything: mark every input as unsent and
        // re-send the full current state (buttons, colours, stick) once the
        // console takes us again. Resetting to "nothing pressed" instead
        // left a dropped release stuck forever (the console kept seeing the
        // button held) - rare with a joystick, routine with two streaming
        // Motion controllers.
        sent_x_ = sent_y_ = kUnsent;
        sent_buttons_ = ~0u;
        input_dirty_ = true;
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
      motion_packet_left_ = 0;
      // A streaming Motion controller hits this whenever the console holds
      // CTS off to send its own 05/8n/9n commands. Just let go of the line:
      // re-announcing with 56 here would land in the middle of the dropped
      // packet, and the next report realigns the game's parser anyway.
      if (!(device_ == PortDevice::Motion && motion_mode_ != 0)) QueueTx(IdleByte());
    }
  }

  if (active_ && input_dirty_) QueueJoyUpdates();
}

// ---------------- Smart Keyboard ----------------
// Same UART plumbing as the joystick above (fifo_, RTS/CTS, TxDone pacing);
// only the power-up handshake and the bytes sent differ.

void VSmileJoy::KbRunCycles(int cycles) {
  if (kb_state_ == kKbHello) {
    kb_hello_counter_ -= cycles;
    if (kb_hello_counter_ > 0) return;
    if (!kb_hello_window_) {
      // Request the line and give the console 12.2 ms to select us.
      kb_hello_window_ = true;
      kb_hello_counter_ = kKbHelloWindow;
      SetRtsActive(true);
    } else if (cts_) {
      kb_hello_window_ = false;
      DTRACE("[kbd] selected, sending hello\n");
      QueueTx(0x52);
      QueueTx(0x52);
      QueueTx(0x52);
      kb_state_ = kKbHandshake;
      kb_handshake_rx_ = 0;
    } else {
      kb_hello_window_ = false;
      kb_hello_counter_ = kKbHelloPeriod;
      SetRtsActive(false);
    }
    return;
  }

  if (kb_state_ == kKbRunning) {
    while (!kb_pending_.empty() && fifo_len_ < 16) {
      QueueTx(kb_pending_.front());
      kb_pending_.pop_front();
    }
    if (!tx_busy_) {
      idle_counter_ -= cycles;
      if (idle_counter_ <= 0) {
        idle_counter_ = kIdlePeriod;
        QueueTx(0x55);
      }
    }
  }

  if (tx_starting_) {
    tx_start_counter_ -= cycles;
    if (tx_start_counter_ <= 0) {
      tx_starting_ = false;
      StartTx();
    }
  }

  if (kb_state_ != kKbRunning) return;

  // Waiting for a CTS grant that never comes: drop out of the active state
  // and fall back to keep-alives, same as the joystick (and MAME's
  // tx_timeout()).
  if (!rts_idle_ && !cts_ && !tx_starting_ && !tx_busy_) {
    rts_timeout_ -= cycles;
    if (rts_timeout_ <= 0) {
      rts_timeout_ = kRtsTimeout;
      SetRtsActive(false);
      active_ = false;
      probe_history_[0] = probe_history_[1] = 0;
      idle_counter_ = kIdlePeriod;
      fifo_len_ = fifo_head_ = 0;
      QueueTx(0x55);
    }
  }

  if (active_ && input_dirty_) QueueKbUpdates();
}

void VSmileJoy::KbRx(u8 byte, bool selected) {
  DTRACE("[kbd] rx %02x (sel=%d state=%d)\n", byte, selected, kb_state_);
  if (kb_state_ == kKbHandshake) {
    // The console's reply to our hello (02 02 E6 D6 60). Like MAME, count
    // any 5 bytes regardless of select, then answer with our language.
    if (++kb_handshake_rx_ >= 5) {
      kb_state_ = kKbRunning;
      active_ = true;
      idle_counter_ = kIdlePeriod;
      QueueTx(kb_language_);
    }
    return;
  }
  if (kb_state_ != kKbRunning || !selected) return;
  // Keep-alive probes: same checksum reply as the joystick (0x70 -> 0xBA).
  const u8 hi = byte & 0xF0;
  if (hi == 0x70 || hi == 0xB0) {
    probe_history_[0] = (hi == 0x70) ? 0 : probe_history_[1];
    probe_history_[1] = byte & 0x0F;
    QueueTx(0xB0 | (((0u - probe_history_[0] - probe_history_[1]) ^ 0xA) & 0xF));
    probed_ = true;
  }
  // 6x (lights) and Dx (report mode) mean nothing to the keyboard.
}

void VSmileJoy::KeyEvent(u8 code, bool pressed) {
  if (device_ != PortDevice::SmartKeyboard || kb_state_ != kKbRunning) return;
  if (kb_pending_.size() >= kKbPendingMax) return;
  u8 b;
  if (code == 0xA9) b = pressed ? 0xA9 : 0xAA;  // Shift has its own release code
  else b = pressed ? code : (u8)(code | 0xC0);
  kb_pending_.push_back(b);
}

// The keyboard's OK/Quit/Help/ABC keys and its small digital joystick
// (MAME's button_changed()/joy_changed()): A1/A2/A3/A4 on press, A0 once
// none are held; Y byte 87 up / 8F down / 80 centre, X byte 7F left / 77
// right / 70 centre - the 7x range keeps it clear of the key release codes.
// Colour buttons (the real keyboard has all four, plus ABC) are sent as the
// joystick's 9x byte: MAME doesn't model them, but the wiki reserves the
// 90-AF range for the buttons shared with the joystick, and the Smart
// Keyboard cart accepts them (red skips Cyber Adventure's intro, exactly as
// it does from a joystick).
void VSmileJoy::QueueKbUpdates() {
  const u32 fn = cur_buttons_ & 0xF, sent_fn = sent_buttons_ & 0xF;
  if (fn != sent_fn) {
    const u32 pressed = fn & ~sent_fn;
    if (pressed & 1) QueueTx(0xA1);
    if (pressed & 2) QueueTx(0xA2);
    if (pressed & 4) QueueTx(0xA3);
    if (pressed & 8) QueueTx(0xA4);
    if (fn == 0) QueueTx(0xA0);
  }
  if ((cur_buttons_ >> 4) != (sent_buttons_ >> 4)) {
    u8 b = 0x90;  // bit0 green, bit1 blue, bit2 yellow, bit3 red (as QueueJoyUpdates)
    if (cur_buttons_ & (1u << 7)) b |= 1;
    if (cur_buttons_ & (1u << 6)) b |= 2;
    if (cur_buttons_ & (1u << 5)) b |= 4;
    if (cur_buttons_ & (1u << 4)) b |= 8;
    QueueTx(b);
  }
  const int y = (cur_y_ > 0) - (cur_y_ < 0), sent_y = (sent_y_ > 0) - (sent_y_ < 0);
  const int x = (cur_x_ > 0) - (cur_x_ < 0), sent_x = (sent_x_ > 0) - (sent_x_ < 0);
  if (y != sent_y) QueueTx(y > 0 ? 0x87 : y < 0 ? 0x8F : 0x80);
  if (x != sent_x) QueueTx(x < 0 ? 0x7F : x > 0 ? 0x77 : 0x70);
  sent_x_ = cur_x_;
  sent_y_ = cur_y_;
  sent_buttons_ = cur_buttons_;
  input_dirty_ = false;
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
  w.U8((u8)device_); w.U8(kb_language_); w.U8(kb_state_); w.U8(kb_handshake_rx_);
  w.B(kb_hello_window_); w.S64(kb_hello_counter_);
  w.U8(motion_mode_); w.Arr8(motion_, 3); w.S64(motion_counter_);
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
  kb_pending_.clear();
  if (r.version >= 7) {
    const u8 dev = r.U8();
    device_ = dev == (u8)PortDevice::SmartKeyboard ? PortDevice::SmartKeyboard
              : (r.version >= 8 && dev == (u8)PortDevice::Motion) ? PortDevice::Motion
              : (r.version >= 8 && dev == (u8)PortDevice::Mat)    ? PortDevice::Mat
                                                                   : PortDevice::Joystick;
    kb_language_ = r.U8(); kb_state_ = r.U8(); kb_handshake_rx_ = r.U8();
    kb_hello_window_ = r.B(); kb_hello_counter_ = r.S64();
    motion_packet_left_ = 0;
    motion_pause_ = 0;
    motion_id_counter_ = motion_id_period_;
    if (r.version >= 8) {
      motion_mode_ = r.U8(); r.Arr8(motion_, 3); motion_counter_ = r.S64();
    } else {
      motion_mode_ = 0;
      motion_counter_ = kMotionPeriod;
    }
  } else {  // saved before the keyboard existed: always a joystick
    device_ = PortDevice::Joystick;
    kb_state_ = kKbHello;
    kb_handshake_rx_ = 0;
    kb_hello_window_ = false;
    kb_hello_counter_ = kKbHelloPeriod;
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
  // The console itself, so a state resumes on the board it was saved on:
  // changing the console type under a running game makes it restart
  // (Smart Keyboard, harness), and region/logo are boot-time straps.
  w.U8((u8)console_type_); w.U8((u8)region_); w.B(vtech_logo_);
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
  if (r.version >= 10) {
    const u8 type = r.U8();
    console_type_ = type <= (u8)ConsoleType::CyberPocket ? (ConsoleType)type : ConsoleType::Standard;
    region_ = r.U8() & 0xF;
    vtech_logo_ = r.B();
  }
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

void VSmile::Reset(bool pal) { ResetMachine(pal, false); }

void VSmile::Restart() { ResetMachine(spg_.IsPal(), true); }

void VSmile::ResetMachine(bool pal, bool keep_ram) {
  cts0_ = cts1_ = false;
  on_ = off_ = restart_ = false;
  auto_on_frames_ = 20;  // hold the power button through early boot polling
  joy_.Reset();
  joy2_.Reset();
  spg_.Reset(pal, keep_ram);
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
      // Port A: the console-type straps IOA14/IOA15 (see ConsoleType). The
      // V.Smile Motion board reads 1/0 (0x4000) per the wiki's table from
      // its schematics; 1/1 (0xC000, which MAME's vsmilem returns) is the
      // Cyber Pocket. Motion carts read these to decide whether motion
      // controls are offered at all (Action Mania's library routine at
      // 0x5608C), and the Motion BIOS picks its boot animation from them.
      switch (console_type_) {
        case ConsoleType::Motion: return 0x4000;
        case ConsoleType::CyberPocket: return 0xC000;
        default: return 0;
      }
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

// The console's UART output reaches both ports (like MAME's uart_rx()); each
// device decides what to do with a byte it wasn't selected for - the
// joystick ignores it, the keyboard needs it during its handshake.
void VSmile::UartTx(u8 byte) {
  DTRACE("[uart] console tx %02x (cts0=%d cts1=%d)\n", byte, cts0_, cts1_);
  joy_.Rx(byte, cts0_);
  joy2_.Rx(byte, cts1_);
}

u16 VSmile::AdcIn(int ch) {
  return ch == 1 ? 0x03FF : 0;  // full battery
}

void VSmile::RunCycles(int cycles) {
  // Two Motion controllers share the console's limited controller service:
  // both report half as often, and re-send their ID (each one makes the
  // console repeat its Motion setup: 05, 9n, E6 D6 60) less often, so
  // button presses still get through promptly. Tested with Shrek Forever
  // After's two-player menus: this matches a joystick on port 2.
  const bool shared = joy2_connected_ && joy_.Device() == PortDevice::Motion &&
                      joy2_.Device() == PortDevice::Motion;
  joy_.SetMotionShared(shared);
  joy2_.SetMotionShared(shared);
  joy_.RunCycles(cycles);
  if (joy2_connected_) joy2_.RunCycles(cycles);
}

}  // namespace dsmile
