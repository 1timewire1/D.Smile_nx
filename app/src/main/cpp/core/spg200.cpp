#include "spg200.h"

#include "state.h"

#ifdef DSMILE_TRACE
#include <cstdio>
extern FILE* g_unsp_trace;
// Instruction index for trace tags: one 16-byte record is written per Step.
static long InstrIdx() { return g_unsp_trace ? ftell(g_unsp_trace) / 16 : -1; }
#endif

namespace dsmile {

Spg200::Spg200(MachineIo& io) : io_(io), cpu_(*this), ppu_(*this), spu_(*this) {}

void Spg200::SetCart(const u16* cart, u32 cart_mask, u16* art_nvram) {
  cart_ = cart;
  cart_mask_ = cart_mask;
  art_nvram_ = art_nvram;
}

void Spg200::Reset(bool pal) {
  pal_ = pal;
  ram_.fill(0);
  gpio_mode_ = 0;
  std::memset(gpio_, 0, sizeof(gpio_));
  io_irq_ctrl_ = 0;
  io_irq_status_ = 0;
  ppu_irq_state_ = spu_ch_irq_state_ = spu_beat_irq_state_ = false;
  fiq_select_ = 7;
  timebase_clock_.Reset();
  timebase_tick_ = 0;
  timebase_setup_ = 0;
  timer_a_data_ = timer_a_preload_ = timer_a_ctrl_ = timer_a_on_ = 0;
  timer_b_data_ = timer_b_preload_ = timer_b_ctrl_ = timer_b_on_ = 0;
  system_ctrl_ = 0;
  extmem_ctrl_ = 0x0028;
  adc_ctrl_ = 0;
  adc_status_ = 0;
  adc_data_ = 0;
  adc_active_ch_ = -1;
  adc_clock_.Reset();
  adc_div_ = 0;
  prng_[0] = 0x1418;
  prng_[1] = 0x1658;
  watchdog_counter_ = 20250000;
  uart_ctrl_ = 0;
  uart_status_ = 0x0022;
  uart_baud_lo_ = uart_baud_hi_ = 0xFF;
  uart_tx_busy_ = false;
  uart_rx_pending_ = false;
  cs2_offset_ = 0;
  dma_src_lo_ = dma_src_hi_ = dma_dst_ = 0;

  ppu_.Reset(pal);
  spu_.Reset();
  cpu_.Reset();  // reads reset vector via bus: must be last
}

void Spg200::SaveState(StateWriter& w) const {
  cpu_.SaveState(w);
  ppu_.SaveState(w);
  spu_.SaveState(w);
  w.Arr16(ram_.data(), ram_.size());
  w.U16(gpio_mode_);
  for (int p = 0; p < 3; p++) for (int i = 0; i < 4; i++) w.U16(gpio_[p][i]);
  w.U16(io_irq_ctrl_); w.U16(io_irq_status_);
  w.B(ppu_irq_state_); w.B(spu_ch_irq_state_); w.B(spu_beat_irq_state_);
  w.U16(fiq_select_);
  w.S64(timebase_clock_.counter);
  w.U32(timebase_tick_);
  w.U16(timebase_setup_);
  w.U16(timer_a_data_); w.U16(timer_a_preload_); w.U16(timer_a_ctrl_); w.U16(timer_a_on_);
  w.U16(timer_b_data_); w.U16(timer_b_preload_); w.U16(timer_b_ctrl_); w.U16(timer_b_on_);
  w.U16(system_ctrl_); w.U16(extmem_ctrl_);
  w.U16(adc_ctrl_); w.U16(adc_status_); w.U16(adc_data_);
  w.U32((u32)adc_active_ch_); w.S64(adc_clock_.counter); w.U32(adc_div_);
  w.U16(prng_[0]); w.U16(prng_[1]);
  w.U32((u32)watchdog_counter_);
  w.U16(uart_ctrl_); w.U16(uart_status_);
  w.U16(uart_baud_lo_); w.U16(uart_baud_hi_);
  w.U8(uart_tx_byte_); w.B(uart_tx_busy_); w.U32((u32)uart_tx_counter_);
  w.U8(uart_rx_buf_); w.U8(uart_rx_incoming_); w.B(uart_rx_pending_); w.U32((u32)uart_rx_counter_);
  w.U32(cs2_offset_);
  w.U16(dma_src_lo_); w.U16(dma_src_hi_); w.U16(dma_dst_);
  w.B(pal_);
}

void Spg200::LoadState(StateReader& r) {
  cpu_.LoadState(r);
  ppu_.LoadState(r);
  spu_.LoadState(r);
  r.Arr16(ram_.data(), ram_.size());
  gpio_mode_ = r.U16();
  for (int p = 0; p < 3; p++) for (int i = 0; i < 4; i++) gpio_[p][i] = r.U16();
  io_irq_ctrl_ = r.U16(); io_irq_status_ = r.U16();
  ppu_irq_state_ = r.B(); spu_ch_irq_state_ = r.B(); spu_beat_irq_state_ = r.B();
  fiq_select_ = r.U16();
  timebase_clock_.counter = r.S64();
  timebase_tick_ = r.U32();
  timebase_setup_ = r.U16();
  timer_a_data_ = r.U16(); timer_a_preload_ = r.U16(); timer_a_ctrl_ = r.U16(); timer_a_on_ = r.U16();
  timer_b_data_ = r.U16(); timer_b_preload_ = r.U16(); timer_b_ctrl_ = r.U16(); timer_b_on_ = r.U16();
  system_ctrl_ = r.U16(); extmem_ctrl_ = r.U16();
  if (r.version >= 4) {
    adc_ctrl_ = r.U16(); adc_status_ = r.U16(); adc_data_ = r.U16();
    adc_active_ch_ = (s32)r.U32(); adc_clock_.counter = r.S64(); adc_div_ = r.U32();
  } else {  // v3 layout: ctrl carried the status bit; a mid-conversion counter is dropped
    const u16 old_ctrl = r.U16();
    adc_data_ = r.U16();
    const u32 old_counter = r.U32();
    adc_ctrl_ = old_ctrl & 0x177F;
    adc_status_ = old_ctrl & 0x2000;
    adc_active_ch_ = old_counter > 0 ? (s32)((old_ctrl >> 4) & 3) : -1;
    adc_clock_.Reset();
    adc_div_ = 0;
  }
  prng_[0] = r.U16(); prng_[1] = r.U16();
  watchdog_counter_ = (s32)r.U32();
  uart_ctrl_ = r.U16(); uart_status_ = r.U16();
  uart_baud_lo_ = r.U16(); uart_baud_hi_ = r.U16();
  uart_tx_byte_ = r.U8(); uart_tx_busy_ = r.B(); uart_tx_counter_ = (s32)r.U32();
  uart_rx_buf_ = r.U8(); uart_rx_incoming_ = r.U8(); uart_rx_pending_ = r.B();
  uart_rx_counter_ = (s32)r.U32();
  cs2_offset_ = r.U32();
  dma_src_lo_ = r.U16(); dma_src_hi_ = r.U16(); dma_dst_ = r.U16();
  pal_ = r.B();
  UpdateIoIrqLines();
}

void Spg200::RunFrame() {
  int frame_cycles = 0;
  for (;;) {
    const int cycles = cpu_.Step();
    frame_cycles += cycles;
    io_.RunCycles(cycles);
    TickUart(cycles);
    TickAdc(cycles);
    TickTimers(cycles);
    spu_.RunCycles(cycles);
    if (ppu_.RunCycles(cycles)) break;
  }
  // Watchdog checked once per frame.
  if (system_ctrl_ & 0x8000) {
    watchdog_counter_ -= frame_cycles;
    if (watchdog_counter_ <= 0) {
      watchdog_counter_ = 20250000;
      cpu_.Reset();
    }
  }
}

u16 Spg200::Read(u32 addr) {
  addr &= 0x3FFFFF;
  if (addr < 0x2800) return ram_[addr];
  if (addr >= 0x4000) return ExtRead(addr);
  if (addr < 0x3000) return ppu_.Read(addr);
  if (addr < 0x3800) return spu_.Read(addr);
  if ((addr & 0xFF00) == 0x3D00) return IoRead(addr);
  if ((addr & 0xFFFC) == 0x3E00) {
    switch (addr & 3) {
      case 0: return dma_src_lo_;
      case 1: return dma_src_hi_;
      case 2: return 0;  // remaining length
      default: return dma_dst_;
    }
  }
  return 0;
}

void Spg200::Write(u32 addr, u16 val) {
  addr &= 0x3FFFFF;
  if (addr < 0x2800) { ram_[addr] = val; return; }
  if (addr >= 0x4000) { ExtWrite(addr, val); return; }
  if (addr < 0x3000) { ppu_.Write(addr, val); return; }
  if (addr < 0x3800) { spu_.Write(addr, val); return; }
  if ((addr & 0xFF00) == 0x3D00) { IoWrite(addr, val); return; }
  if ((addr & 0xFFFC) == 0x3E00) {
    switch (addr & 3) {
      case 0: dma_src_lo_ = val; return;
      case 1: dma_src_hi_ = val & 0x3F; return;
      case 2: RunDma(val); return;
      default: dma_dst_ = val & 0x3FFF; return;
    }
  }
}

u16 Spg200::ExtRead(u32 addr) {
  const int decode = (extmem_ctrl_ >> 6) & 3;
  switch (decode) {
    case 0:
      return cart_ ? cart_[(addr + cs2_offset_) & cart_mask_] : 0;
    case 1: {
      const u32 a = addr & 0x1FFFFF;
      const u32 base = (addr >> 21) ? 0x100000u : 0u;
      return cart_ ? cart_[(base + a + cs2_offset_) & cart_mask_] : 0;
    }
    default: {
      const u32 a = addr & 0xFFFFF;
      switch (addr >> 20) {
        case 0: return cart_ ? cart_[(a + cs2_offset_) & cart_mask_] : 0;
        case 1: return cart_ ? cart_[(a + 0x100000 + cs2_offset_) & cart_mask_] : 0;
        case 2:
          if (art_nvram_) return art_nvram_[a & 0x1FFFF];
          return cart_ ? cart_[(a + 0x200000 + cs2_offset_) & cart_mask_] : 0;
        default: return sysrom_ ? sysrom_[a] : 0;
      }
    }
  }
}

void Spg200::ExtWrite(u32 addr, u16 val) {
  const int decode = (extmem_ctrl_ >> 6) & 3;
  if (decode >= 2 && (addr >> 20) == 2 && art_nvram_) {
    art_nvram_[addr & 0x1FFFF] = val;
  }
}

// ---------------- GPIO ----------------

u16 Spg200::GpioReadData(int port) {
  const u16 buffer = gpio_[port][0], dir = gpio_[port][1];
  const u16 attrib = gpio_[port][2], mask = gpio_[port][3];
  const u16 buf = buffer ^ (dir & (u16)~attrib);
  return (buf & dir & (u16)~mask) | (io_.GpioIn(port) & (u16)~dir & (u16)~mask);
}

void Spg200::GpioResolveOut(int port) {
  const u16 buffer = gpio_[port][0], dir = gpio_[port][1];
  const u16 attrib = gpio_[port][2], mask = gpio_[port][3];
  const u16 buf = buffer ^ (dir & (u16)~attrib);
  io_.GpioOut(port, buf & (u16)~mask, dir & (u16)~mask);
}

void Spg200::GpioWrite(int port, int reg, u16 val) {
  gpio_[port][reg] = val;
  GpioResolveOut(port);
}

// ---------------- IO block ----------------

static int rtrace_n = 0;
u16 Spg200::IoRead(u32 addr) {
#ifdef DSMILE_TRACE
  if (rtrace_n < 400) { const u16 v = IoReadInner(addr); std::printf("[rd] %04x=%04x\n", (unsigned)addr, v); rtrace_n++; return v; }
#endif
  return IoReadInner(addr);
}
u16 Spg200::IoReadInner(u32 addr) {
  switch (addr & 0xFF) {
    case 0x00: return gpio_mode_;
    case 0x01: return GpioReadData(0);
    case 0x02: case 0x03: case 0x04: case 0x05: return gpio_[0][(addr & 0xFF) - 0x02];
    case 0x06: return GpioReadData(1);
    case 0x07: case 0x08: case 0x09: case 0x0A: return gpio_[1][(addr & 0xFF) - 0x07];
    case 0x0B: return GpioReadData(2);
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: return gpio_[2][(addr & 0xFF) - 0x0C];
    case 0x10: return timebase_setup_;
    case 0x12: return timer_a_data_;
    case 0x13: return timer_a_ctrl_;
    case 0x14: return timer_a_on_;
    case 0x16: return timer_b_data_;
    case 0x17: return timer_b_ctrl_;
    case 0x18: return timer_b_on_;
    case 0x1C: return (u16)ppu_.CurrentLine();
    case 0x20: return system_ctrl_;
    case 0x21: return io_irq_ctrl_;
    case 0x22: return io_irq_status_;
    case 0x23: return extmem_ctrl_;
    case 0x25: return (u16)(adc_ctrl_ | adc_status_);
    case 0x27: return adc_data_;
    case 0x2B: return pal_ ? 1 : 0;
    case 0x2C: {
      const u16 v = prng_[0];
      prng_[0] = (u16)(((prng_[0] << 1) | (((prng_[0] >> 14) ^ (prng_[0] >> 13)) & 1)) & 0x7FFF);
      return v;
    }
    case 0x2D: {
      const u16 v = prng_[1];
      prng_[1] = (u16)(((prng_[1] << 1) | (((prng_[1] >> 14) ^ (prng_[1] >> 13)) & 1)) & 0x7FFF);
      return v;
    }
    case 0x2E: return fiq_select_;
    case 0x2F: return cpu_.GetDs();
    case 0x30: return uart_ctrl_;
    case 0x31: return uart_status_;
    case 0x33: return uart_baud_lo_;
    case 0x34: return uart_baud_hi_;
    case 0x36: {
      DTRACE("[uart] rxbuf-read %02x\n", uart_rx_buf_);
      uart_status_ &= (u16)~0x0080;  // clear RX full
      return uart_rx_buf_;
    }
    default: return 0;
  }
}

void Spg200::IoWrite(u32 addr, u16 val) {
  switch (addr & 0xFF) {
    case 0x00: gpio_mode_ = val & 0x1F; return;
    case 0x01: case 0x02: GpioWrite(0, 0, val); return;  // data writes go to buffer
    case 0x03: GpioWrite(0, 1, val); return;
    case 0x04: GpioWrite(0, 2, val); return;
    case 0x05: GpioWrite(0, 3, val); return;
    case 0x06: case 0x07: GpioWrite(1, 0, val); return;
    case 0x08: GpioWrite(1, 1, val); return;
    case 0x09: GpioWrite(1, 2, val); return;
    case 0x0A: GpioWrite(1, 3, val); return;
    case 0x0B: case 0x0C: GpioWrite(2, 0, val); return;
    case 0x0D: GpioWrite(2, 1, val); return;
    case 0x0E: GpioWrite(2, 2, val); return;
    case 0x0F: GpioWrite(2, 3, val); return;
    case 0x10: DTRACE("[tw@%ld] setup=%04x\n", InstrIdx(), val); timebase_setup_ = val & 0x0F; return;
    case 0x11: DTRACE("[tw@%ld] tbclear tick=%u\n", InstrIdx(), timebase_tick_); timebase_tick_ = 0; return;
    case 0x12: DTRACE("[tw@%ld] a_data=%04x tick=%u\n", InstrIdx(), val, timebase_tick_); timer_a_data_ = timer_a_preload_ = val; return;
    case 0x13: DTRACE("[tw@%ld] a_ctrl=%04x\n", InstrIdx(), val); timer_a_ctrl_ = val; return;
    case 0x14: DTRACE("[tw@%ld] a_on=%04x tick=%u\n", InstrIdx(), val, timebase_tick_); timer_a_on_ = val & 1; return;
    case 0x15:
      io_irq_status_ &= (u16)~0x0800;
      UpdateIoIrqLines();
      return;
    case 0x16: DTRACE("[tw] b_data=%04x\n", val); timer_b_data_ = timer_b_preload_ = val; return;
    case 0x17: DTRACE("[tw] b_ctrl=%04x\n", val); timer_b_ctrl_ = val; return;
    case 0x18: DTRACE("[tw] b_on=%04x\n", val); timer_b_on_ = val & 1; return;
    case 0x19:
      io_irq_status_ &= (u16)~0x0400;
      UpdateIoIrqLines();
      return;
    case 0x20: system_ctrl_ = val & 0xC3F6; return;
    case 0x21:
      DTRACE("[irq] ctrl=%04x (status=%04x)\n", val, io_irq_status_);
      io_irq_ctrl_ = val;
      UpdateIoIrqLines();
      return;
    case 0x22:
      if (val & 0x1200) DTRACE("[irq] w1c %04x (status=%04x)\n", val, io_irq_status_);
      io_irq_status_ &= (u16)~val;
      UpdateIoIrqLines();
      return;
    case 0x23: extmem_ctrl_ = val & 0x0FFE; return;
    case 0x24:
      if (val == 0x55AA) watchdog_counter_ = 20250000;
      return;
    case 0x25: {
      adc_ctrl_ = val & 0x177F;
      adc_status_ &= (u16)~(val & 0x2000);  // W1C irq/ready flag
      if (!(adc_status_ & 0x2000)) {
        io_irq_status_ &= (u16)~0x2000;
        UpdateIoIrqLines();
      }
      if (adc_ctrl_ & 0x0001) {  // enabled: flags ready even with no request
        adc_status_ |= 0x2000;
        if (adc_ctrl_ & 0x1000) {  // request (self-clearing): start a conversion
          adc_status_ &= (u16)~0x2000;
          adc_ctrl_ &= (u16)~0x1000;
          adc_active_ch_ = (adc_ctrl_ >> 4) & 3;
          adc_data_ &= (u16)~0x8000;
        }
      } else {
        adc_active_ch_ = -1;
      }
      return;
    }
    case 0x28: case 0x29: case 0x2A: return;  // sleep/wakeup: ignored
    case 0x2C: prng_[0] = val & 0x7FFF; return;
    case 0x2D: prng_[1] = val & 0x7FFF; return;
    case 0x2E:
      fiq_select_ = val & 7;
      UpdateFiqLine();
      return;
    case 0x2F: cpu_.SetDs(val); return;
    case 0x30: DTRACE("[uart] ctrl=%04x\n", val); uart_ctrl_ = val; return;
    case 0x31:
      uart_status_ &= (u16)~(val & 0x0003);  // W1C RxReady/TxReady
      return;
    case 0x32:  // UART soft reset
      uart_tx_busy_ = false;
      uart_rx_pending_ = false;
      uart_status_ = 0x0022;
      return;
    case 0x33: uart_baud_lo_ = val & 0xFF; return;
    case 0x34: uart_baud_hi_ = val & 0xFF; return;
    case 0x35:
      DTRACE("[uart] tx %02x (ctrl=%04x busy=%d)\n", val & 0xFF, uart_ctrl_, uart_tx_busy_);
      if ((uart_ctrl_ & 0x0080) && !uart_tx_busy_) {
        uart_tx_busy_ = true;
        uart_tx_byte_ = val & 0xFF;
        uart_status_ = (uart_status_ | 0x0040) & (u16)~0x0002;  // busy, !tx_ready
        uart_tx_counter_ = UartByteTime();
      }
      return;
    default: return;
  }
}

int Spg200::UartByteTime() const {
  const int div = 0x10000 - (((int)uart_baud_hi_ << 8) | uart_baud_lo_);
  const int bits = (uart_ctrl_ & 0x0020) ? 11 : 10;
  return 16 * div * bits;
}

void Spg200::TickUart(int cycles) {
  if (uart_tx_busy_) {
    uart_tx_counter_ -= cycles;
    if (uart_tx_counter_ <= 0) {
      uart_tx_busy_ = false;
      uart_status_ = (uart_status_ | 0x0002) & (u16)~0x0040;  // tx_ready, !busy
      io_.UartTx(uart_tx_byte_);
      if (uart_ctrl_ & 0x0002) RaiseIoIrq(0x0100);
    }
  }
  if (uart_rx_pending_) {
    uart_rx_counter_ -= cycles;
    if (uart_rx_counter_ <= 0) {
      uart_rx_pending_ = false;
      uart_rx_buf_ = uart_rx_incoming_;
      uart_status_ |= 0x0081;  // rx_full | rx_ready
      if (uart_ctrl_ & 0x0001) RaiseIoIrq(0x0100);
      io_.UartRxDone();  // flow-control feedback to the controller
    }
  }
}

void Spg200::UartRxStart(u8 byte) {
  DTRACE("[uart] rx-start %02x (ctrl=%04x pending=%d)\n", byte, uart_ctrl_, uart_rx_pending_);
  if (!(uart_ctrl_ & 0x0040) || uart_rx_pending_) return;
  uart_rx_pending_ = true;
  uart_rx_incoming_ = byte;
  uart_rx_counter_ = UartByteTime();
}

void Spg200::TickAdc(int cycles) {
  if (adc_active_ch_ < 0) return;  // conversion clock only runs while active
  if (adc_clock_.TickOnce(cycles)) {
    adc_div_++;
    const u32 div_mask = (1u << ((adc_ctrl_ >> 2) & 3)) - 1;
    if ((adc_div_ & div_mask) == 0) {
      adc_data_ = (io_.AdcIn(adc_active_ch_) & 0x03FF) | 0x8000;
      adc_status_ |= 0x2000;
      adc_active_ch_ = -1;
      if (adc_ctrl_ & 0x0200) RaiseIoIrq(0x2000);
    }
  }
}

void Spg200::TickTimers(int cycles) {
  while (timebase_clock_.TickOnce(cycles)) {
    timebase_tick_++;
    u16 bits = 0;
    if ((timebase_tick_ & 7) == 0) bits |= 0x0040;      // 4096 Hz
    if ((timebase_tick_ & 15) == 0) bits |= 0x0020;     // 2048 Hz
    if ((timebase_tick_ & 31) == 0) bits |= 0x0010;     // 1024 Hz
    if ((timebase_tick_ & 8191) == 0) bits |= 0x0008;   // 4 Hz
    const int tmb1_sel = timebase_setup_ & 3;           // 8<<n Hz
    const int tmb2_sel = (timebase_setup_ >> 2) & 3;    // 128<<n Hz
    const u32 tmb1_mask = (4096u >> tmb1_sel) - 1;
    const u32 tmb2_mask = (256u >> tmb2_sel) - 1;
    if ((timebase_tick_ & tmb1_mask) == 0) bits |= 0x0001;
    if ((timebase_tick_ & tmb2_mask) == 0) bits |= 0x0002;

    // Timer A source frequency (Hz, derived from the 32768 Hz base tick)
    if (timer_a_on_) {
      const int src_a = timer_a_ctrl_ & 7;
      const int src_b = (timer_a_ctrl_ >> 3) & 7;
      int rate = 0;
      if (src_a == 5) {
        switch (src_b) {
          case 0: rate = 2048; break;
          case 1: rate = 1024; break;
          case 2: rate = 256; break;
          case 3: rate = 8 << tmb1_sel; break;
          case 4: rate = 4; break;
          case 5: rate = 2; break;
          default: rate = 0; break;
        }
      } else {
        switch (src_a) {
          case 2: rate = 32768; break;
          case 3: rate = 8192; break;
          case 4: rate = 4096; break;
          default: rate = 0; break;
        }
      }
      if (rate && (timebase_tick_ & (u32)(32768 / rate - 1)) == 0) {
        if (++timer_a_data_ == 0) {
          timer_a_data_ = timer_a_preload_;
          bits |= 0x0800;
          DTRACE("[ta@%ld] overflow reload=%04x rate=%d tick=%u\n", InstrIdx(), timer_a_preload_,
                 rate, timebase_tick_);
        }
      }
    }
    if (timer_b_on_) {
      int rate = 0;
      switch (timer_b_ctrl_ & 7) {
        case 2: rate = 32768; break;
        case 3: rate = 8192; break;
        case 4: rate = 4096; break;
        default: rate = 0; break;
      }
      if (rate && (timebase_tick_ & (u32)(32768 / rate - 1)) == 0) {
        if (++timer_b_data_ == 0) {
          timer_b_data_ = timer_b_preload_;
          bits |= 0x0400;
        }
      }
    }
    if (bits) RaiseIoIrq(bits);
  }
}

void Spg200::RunDma(u16 len) {
  if (len & 0xC000) return;
  u32 src = ((u32)dma_src_hi_ << 16) | dma_src_lo_;
  u16 dst = dma_dst_;
  for (u16 i = 0; i < len; i++) {
    Write(dst, Read(src));
    src = (src + 1) & 0x3FFFFF;
    dst = (dst + 1) & 0x3FFF;
  }
  dma_src_lo_ = src & 0xFFFF;
  dma_src_hi_ = (src >> 16) & 0x3F;
  dma_dst_ = dst;
}

// ---------------- IRQ wiring ----------------

void Spg200::RaiseIoIrq(u16 bits) {
  DTRACE("[irq] raise %04x (ctrl=%04x)\n", bits, io_irq_ctrl_);
  io_irq_status_ |= bits;
  UpdateIoIrqLines();
}

void Spg200::RaiseExtIrq(int which) {
  RaiseIoIrq(which == 0 ? 0x0200 : 0x1000);
}

void Spg200::SetPpuIrqLine(bool state) {
  ppu_irq_state_ = state;
  cpu_.SetIrq(0, state);
  UpdateFiqLine();
}

void Spg200::SetSpuChannelIrqLine(bool state) {
  spu_ch_irq_state_ = state;
  cpu_.SetIrq(1, state);
  UpdateFiqLine();
}

void Spg200::SetSpuBeatIrqLine(bool state) {
  spu_beat_irq_state_ = state;
  cpu_.SetIrq(4, state);
}

void Spg200::UpdateIoIrqLines() {
  const u16 p = io_irq_status_ & io_irq_ctrl_;
  cpu_.SetIrq(2, (p & 0x0C00) != 0);
  cpu_.SetIrq(3, (p & 0x6100) != 0);
  cpu_.SetIrq(5, (p & 0x1200) != 0);
  cpu_.SetIrq(6, (p & 0x0070) != 0);
  cpu_.SetIrq(7, (p & 0x008B) != 0);
  UpdateFiqLine();
}

void Spg200::UpdateFiqLine() {
  const u16 p = io_irq_status_ & io_irq_ctrl_;
  bool state = false;
  switch (fiq_select_) {
    case 0: state = ppu_irq_state_; break;
    case 1: state = spu_ch_irq_state_; break;
    case 2: state = (p & 0x0800) != 0; break;
    case 3: state = (p & 0x0400) != 0; break;
    case 4: state = (p & 0x0100) != 0; break;
    case 5: state = (p & 0x1200) != 0; break;
    case 6: state = (p & 0x2000) != 0; break;
    default: state = false; break;
  }
  cpu_.SetFiq(state);
}

}  // namespace dsmile
