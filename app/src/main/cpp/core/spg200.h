#pragma once
#include "common.h"
#include "unsp.h"
#include "ppu.h"
#include "spu.h"

namespace dsmile {

// Machine-side hooks (implemented by VSmile).
struct MachineIo {
  virtual u16 GpioIn(int port) = 0;                       // external pin state
  virtual void GpioOut(int port, u16 data, u16 mask) = 0; // driven outputs
  virtual void UartTx(u8 byte) = 0;                       // console -> controllers
  virtual void UartRxDone() = 0;                          // console finished receiving a byte
  virtual u16 AdcIn(int ch) = 0;
  virtual void RunCycles(int cycles) = 0;                 // machine timers
  virtual ~MachineIo() = default;
};

// SPG200 SoC: CPU + PPU + SPU + RAM + IO block + external memory decode.
class Spg200 {
 public:
  explicit Spg200(MachineIo& io);

  // cart: pointer to word data whose size is a power of two (mask = size-1).
  void SetCart(const u16* cart, u32 cart_mask, u16* art_nvram);
  void SetSysrom(const u16* sysrom) { sysrom_ = sysrom; }
  void SetCs2(bool active) { cs2_offset_ = active ? 0x400000u : 0u; }

  void Reset(bool pal);
  void RunFrame();

  u16 Read(u32 addr);
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
  bool IsPal() const { return pal_; }

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

 private:
  u16 IoRead(u32 addr);
  u16 IoReadInner(u32 addr);
  void IoWrite(u32 addr, u16 val);
  u16 ExtRead(u32 addr);
  void ExtWrite(u32 addr, u16 val);
  void UpdateIoIrqLines();
  void UpdateFiqLine();
  void GpioWrite(int port, int reg, u16 val);
  u16 GpioReadData(int port);
  void GpioResolveOut(int port);
  void TickTimers(int cycles);
  void TickUart(int cycles);
  void TickAdc(int cycles);
  int UartByteTime() const;
  void RunDma(u16 len);

  MachineIo& io_;
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
};

}  // namespace dsmile
