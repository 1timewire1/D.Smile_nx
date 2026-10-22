#pragma once
#include <algorithm>

#include "common.h"

// Wait-loop replay (Spg200::RunFrame) skips CPU steps, which the trace and
// test-hook builds watch one by one, so it's off in those.
#if !defined(DSMILE_TRACE) && !defined(DSMILE_TEST_HOOKS)
#define DSMILE_IDLE_REPLAY 1
#endif
#include "unsp.h"
#include "ppu.h"
#include "spu.h"

namespace dsmile {

#ifdef DSMILE_TEST_HOOKS
extern int g_test_rwatch_lo, g_test_rwatch_hi;
extern int g_test_wwatch_lo, g_test_wwatch_hi;
#endif

// Machine-side hooks (implemented by VSmile).
struct MachineIo {
  virtual u16 GpioIn(int port) = 0;                       // external pin state
  virtual void GpioOut(int port, u16 data, u16 mask) = 0; // driven outputs
  virtual void UartTx(u8 byte) = 0;                       // console -> controllers
  virtual void UartRxDone() = 0;                          // console finished receiving a byte
  virtual u16 AdcIn(int ch) = 0;
  virtual void RunCycles(int cycles) = 0;                 // machine timers
  // How many cycles RunCycles() can be put off for: its next counter
  // deadline. 1 = run it after every instruction.
  virtual s64 NextEvent() { return 1; }
  virtual ~MachineIo() = default;
};

// SPG200 SoC: CPU + PPU + SPU + RAM + IO block + external memory decode.
class Spg200 {
 public:
  explicit Spg200(MachineIo& io);

  // cart: pointer to word data whose size is a power of two (mask = size-1).
  void SetCart(const u16* cart, u32 cart_mask, u16* art_nvram);
  void SetSysrom(const u16* sysrom) {
    sysrom_ = sysrom;
    RebuildReadMaps();
  }
  void SetCs2(bool active) { cs2_offset_ = active ? 0x400000u : 0u; }

  // keep_ram: a /RST pulse (the RESTART / ON-RESTART button) resets the
  // chip but leaves the SRAM contents alone, unlike a power-on.
  void Reset(bool pal, bool keep_ram = false);
  void RunFrame();

  // Bus read, used by the CPU (every instruction fetch), the PPU (tile and
  // sprite data) and the SPU. RAM, cartridge, BIOS and Art Studio NVRAM
  // pages read straight through read_maps_; everything else - PPU, SPU and
  // I/O registers, and anything unmapped - goes through ReadSlow().
  u16 Read(u32 addr) {
    addr &= 0x3FFFFF;
#ifndef DSMILE_TEST_HOOKS  // the hooks' read watch lives in ReadSlow()
    if (const u16* p = read_maps_[cs2_offset_ >> 22][addr >> 10]) return p[addr & 0x3FF];
#endif
    return ReadSlow(addr);
  }
  // `n` words from addr as a plain pointer, when they all sit in one
  // read_maps_ page (so each is what Read() would return), else null. For
  // the PPU's tile rows: nothing writes memory while a scanline is drawn.
  const u16* ReadRun(u32 addr, u32 n) const {
#ifdef DSMILE_TEST_HOOKS
    return nullptr;
#else
    addr &= 0x3FFFFF;
    if (n == 0 || (addr & 0x3FF) + n > 0x400) return nullptr;
    const u16* p = read_maps_[cs2_offset_ >> 22][addr >> 10];
    return p ? p + (addr & 0x3FF) : nullptr;
#endif
  }
  void Write(u32 addr, u16 val);

  // From machine:
  void UartRxStart(u8 byte);            // controller -> console byte
  void RaiseExtIrq(int which);          // 0 -> EXT1 (bit9), 1 -> EXT2 (bit12)
  // From PPU/SPU:
  void SetPpuIrqLine(bool state);
  void SetSpuChannelIrqLine(bool state);
  void SetSpuBeatIrqLine(bool state);
  void RaiseIoIrq(u16 bits);            // set IO IRQ status bits, update lines

  Ppu& GetPpu() { return ppu_; }
  Spu& GetSpu() { return spu_; }
  UnSP& GetCpu() { return cpu_; }
  // Work RAM (0x2800 words), read-only, for desktop debugging tools.
  const u16* DebugRam() const { return ram_.data(); }
  bool IsPal() const { return pal_; }
  // Standby (see RunFrame()): the CPU is halted by a 0xAA55 write to 3D28
  // and waits for a wakeup source (3D29) - the console's OFF state.
  bool Sleeping() const { return sleeping_; }

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

 private:
  u16 IoRead(u32 addr);
  u16 IoReadInner(u32 addr);
  void IoWrite(u32 addr, u16 val);
  u16 ReadSlow(u32 addr);
  u16 ExtRead(u32 addr);
  // read_maps_ upkeep: rebuilt whenever what a page holds can change -
  // cart/BIOS buffers, the external memory decode (3D23), reset, load state.
  void RebuildReadMaps();
  const u16* ExtPage(u32 addr, u32 cs2_offset) const;
  void ExtWrite(u32 addr, u16 val);
  void UpdateIoIrqLines();
  void UpdateFiqLine();
  void GpioWrite(int port, int reg, u16 val);
  u16 GpioReadData(int port);
  void GpioResolveOut(int port);
  // The per-instruction ticks. Each keeps its common "nothing due" case
  // inline and calls the out-of-line half only when something happens.
  void TickTimers(int cycles) {
    if (timebase_clock_.TickOnce(cycles)) TimebaseFired(cycles);
  }
  void TickUart(int cycles) {
    if (uart_tx_busy_ || uart_rx_pending_) TickUartBusy(cycles);
  }
  void TickAdc(int cycles) {
    if (adc_active_ch_ >= 0) TickAdcActive(cycles);
  }
  // The controller ports (MachineIo::RunCycles) only need running when one
  // of their counters runs out (MachineIo::NextEvent()), so their cycles are
  // banked in io_pending_ until then. SyncIo() runs the banked cycles before
  // anything talks to the ports, and IoChanged() asks for a fresh NextEvent()
  // after. The instruction that reaches a deadline gets a RunCycles() call of
  // its own: a counter running out can change what the rest of that same
  // call charges (the RTS re-arm restarts the retry counter, which is then
  // charged the call's cycles), so only the event-free cycles before it may
  // be lumped together. Run that way, the result is the same as running the
  // ports after every instruction.
  void TickIo(int cycles) {
    io_pending_ += cycles;
    if (io_pending_ >= io_next_) RunIo(cycles);
  }
  void RunIo(int last) {
    const s64 before = io_pending_ - last;
    io_pending_ = 0;
    if (before > 0) io_.RunCycles((int)before);
    io_.RunCycles(last);
    io_next_ = io_.NextEvent();
  }
  void SyncIo() {
    if (io_pending_ > 0) {
      const s64 c = io_pending_;
      io_pending_ = 0;
      io_.RunCycles((int)c);
      io_next_ = std::max<s64>(io_next_ - c, 1);  // the deadline is that much nearer
    }
  }
  void IoChanged() { io_next_ = 1; }  // re-plan at the next instruction

  // All of the above, as run after each CPU instruction until now: true when
  // the PPU finished the visible frame.
  bool TickAll(int cycles) {
    TickIo(cycles);
    TickUart(cycles);
    TickAdc(cycles);
    TickTimers(cycles);
    spu_.RunCycles(cycles);
    return ppu_.RunCycles(cycles);
  }
  // The same idea as TickIo, for every device at once: RunFrame() banks
  // each instruction's cycles in dev_pending_ and only runs TickAll() once
  // dev_next_ - the nearest deadline of any device - is reached, as an
  // event-free lump plus that last instruction on its own (the split that
  // keeps it exact; see TickIo). Anything that reads or writes a device
  // register first runs the banked cycles (SyncDevices) and then re-plans
  // (DevicesChanged), so the CPU sees each device as it would have been.
  bool RunDevices(int last) {
    const s64 before = dev_pending_ - last;
    dev_pending_ = 0;
    if (before > 0) TickAll((int)before);  // reaches no deadline, so it can't end the frame
    const bool frame_done = TickAll(last);
    dev_next_ = NextDeviceEvent();
    return frame_done;
  }
  void SyncDevices() {
    if (dev_pending_ > 0) {
      const s64 c = dev_pending_;
      dev_pending_ = 0;
      TickAll((int)c);
    }
  }
  void DevicesChanged() { dev_next_ = 1; }  // re-plan at the next instruction
  s64 NextDeviceEvent() const;
  void TimebaseFired(int cycles);
  void TickUartBusy(int cycles);
  void TickAdcActive(int cycles);
  int UartByteTime() const;
  void RunDma(u16 len);

  MachineIo& io_;
  s64 io_pending_ = 0;  // cycles not yet given to io_.RunCycles() (see TickIo)
  s64 io_next_ = 1;     // run them once this many have built up
  s64 dev_pending_ = 0;  // cycles not yet given to TickAll() (see RunDevices)
  s64 dev_next_ = 1;     // run them once this many have built up
  UnSP cpu_;
  Ppu ppu_;
  Spu spu_;
  bool pal_ = false;

  std::array<u16, 0x2800> ram_{};

  const u16* cart_ = nullptr;
  u32 cart_mask_ = 0;
  u32 cs2_offset_ = 0;
  u16* art_nvram_ = nullptr;  // 128K words when present
  const u16* sysrom_ = nullptr;
  // Read() shortcuts: per 1K-word page, a pointer to the memory that page
  // reads from, or null for "ask ReadSlow()". One map per CS2 state
  // (cs2_offset_ 0 / 0x400000), so a bank switch costs nothing.
  std::array<std::array<const u16*, 4096>, 2> read_maps_{};

  // GPIO: per port: [0]=buffer, [1]=dir, [2]=attrib, [3]=mask
  u16 gpio_mode_ = 0;
  u16 gpio_[3][4]{};

  // IO IRQ
  u16 io_irq_ctrl_ = 0, io_irq_status_ = 0;
  bool ppu_irq_state_ = false, spu_ch_irq_state_ = false, spu_beat_irq_state_ = false;
  u16 fiq_select_ = 7;

  // Timebase + timers
  CycleClock timebase_clock_{27000000, 32768};  // 32768 Hz
  u32 timebase_tick_ = 0;
  u16 timebase_setup_ = 0;
  u16 timer_a_data_ = 0, timer_a_preload_ = 0, timer_a_ctrl_ = 0, timer_a_on_ = 0;
  u16 timer_b_data_ = 0, timer_b_preload_ = 0, timer_b_ctrl_ = 0, timer_b_on_ = 0;

  // System
  u16 system_ctrl_ = 0;
  u16 extmem_ctrl_ = 0x0028;
  // ADC: readback is ctrl | status. The conversion clock (16-cycle tick,
  // divided by 1 << clock-select) only advances while a conversion is active.
  u16 adc_ctrl_ = 0, adc_status_ = 0, adc_data_ = 0;
  s32 adc_active_ch_ = -1;
  CycleClock adc_clock_{16};
  u32 adc_div_ = 0;
  u16 prng_[2] = {0x1418, 0x1658};
  s32 watchdog_counter_ = 20250000;
  bool sleeping_ = false;
  u16 wakeup_src_ = 0;      // 3D29: bits as the low half of IO_IRQ_STATUS (0x80 key change), bit2 2 Hz
  u16 wakeup_time_ = 0;     // 3D2A (stored; games write 0xC8)
  u16 sleep_keys_ = 0;      // console-button pins last seen asleep: ON wakes on its press edge
  s32 sleep_2hz_ = 0;       // cycles to the next 2 Hz wakeup tick

  // UART
  u16 uart_ctrl_ = 0, uart_status_ = 0x0022;
  u16 uart_baud_lo_ = 0xFF, uart_baud_hi_ = 0xFF;
  u8 uart_tx_byte_ = 0;
  bool uart_tx_busy_ = false;
  s32 uart_tx_counter_ = 0;
  u8 uart_rx_buf_ = 0, uart_rx_incoming_ = 0;
  bool uart_rx_pending_ = false;
  s32 uart_rx_counter_ = 0;

  // CPU DMA
  u16 dma_src_lo_ = 0, dma_src_hi_ = 0, dma_dst_ = 0;

  // Wait-loop replay (see RunFrame). Not saved: it restarts every frame.
  struct IdleStep {
    UnSP::State state;  // the CPU before this instruction
    int cycles;
  };
  static constexpr int kIdleMaxLen = 16;  // longest loop body recognised
  enum class IdleMode : u8 { kSearch, kRecord, kReplay };
  IdleMode idle_mode_ = IdleMode::kSearch;
  IdleStep idle_body_[kIdleMaxLen];
  int idle_len_ = 0, idle_pos_ = 0;
  u32 idle_head_ = 0;            // CS:PC the loop starts at
  // Loops whose recordings failed, and how often, by head (direct-mapped):
  // after kIdleMaxFails a head isn't recorded again. Busy games run many
  // short loops that aren't waits; without this they'd be re-recorded over
  // and over. Forgotten every kIdleFailFrames frames, in case a loop's
  // behaviour changes with what the game is doing.
  static constexpr int kIdleMaxFails = 3;
  static constexpr int kIdleFailFrames = 64;
  u32 idle_fail_head_[64] = {};
  u8 idle_fail_count_[64] = {};
  int idle_fail_age_ = 0;
  static int IdleFailSlot(u32 head) { return (int)((head ^ (head >> 6)) & 63); }
  int idle_retries_ = 0;         // passes re-recorded at this head (see IdleRecord)
  bool bus_wrote_ = false;       // any Write() since cleared
  bool bus_device_ = false;      // any device-register read since cleared
  UnSP::State idle_before_{};    // the CPU before the instruction being recorded
  bool idle_interrupt_due_ = false;
  bool IdleLoop(int& frame_cycles);
  void IdleRecord(int cycles);
  void IdleReject();
  bool IdleReplay(int& frame_cycles);
};

}  // namespace dsmile
