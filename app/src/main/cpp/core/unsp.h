#pragma once
#include "common.h"

namespace dsmile {

class Spg200;

#ifdef DSMILE_TEST_HOOKS
extern u32 g_test_watch_pc[16];  // desktop experiments: print registers when PC hits these
extern int g_test_watch_count;
extern int g_test_watch_mem;
extern int g_test_pc_history;  // > 0: print the last 64 PCs on that many watch hits
extern int g_test_watch_bpoff;  // >= 0: also print [bp+off], [bp+off+1]  // >= 0: also print 4 words from here
#endif

// SunPlus unSP 16-bit CPU (ISA 1.0/1.1 + a few 1.2 ops), word-addressed 22-bit space.
class UnSP {
 public:
  explicit UnSP(Spg200& bus) : bus_(bus) {}

  void Reset();
  int Step();  // executes one instruction (or takes an interrupt); returns cycles

  void SetIrq(int line, bool state) {  // line 0..7
    if (state) irq_signal_ |= (1u << line); else irq_signal_ &= ~(1u << line);
  }
  void SetFiq(bool state) { fiq_signal_ = state; }

  u16 GetDs() const { return (r_[6] >> 10) & 0x3F; }
  // Current CS:PC, for trace output (points just past the executing instruction).
  u32 DebugPc() const { return CsPc(); }
  void SetDs(u16 v) { r_[6] = (r_[6] & 0x03FF) | ((v & 0x3F) << 10); }

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

  // Everything Step() depends on besides memory and the interrupt lines,
  // for Spg200's wait-loop replay.
  struct State {
    u16 r[8];
    u8 sb[3];
    bool irq, fiq, irq_enable, fiq_enable, fir_mov;
    bool operator==(const State&) const = default;
  };
  State GetState() const {
    State s;
    std::memcpy(s.r, r_, sizeof(r_));
    std::memcpy(s.sb, sb_, sizeof(sb_));
    s.irq = irq_; s.fiq = fiq_;
    s.irq_enable = irq_enable_; s.fiq_enable = fiq_enable_;
    s.fir_mov = fir_mov_;
    return s;
  }
  void SetState(const State& s) {
    std::memcpy(r_, s.r, sizeof(r_));
    std::memcpy(sb_, s.sb, sizeof(sb_));
    irq_ = s.irq; fiq_ = s.fiq;
    irq_enable_ = s.irq_enable; fiq_enable_ = s.fiq_enable;
    fir_mov_ = s.fir_mov;
  }
  // Whether Step() would take an interrupt first thing from state s, given
  // the interrupt lines now (CheckInterrupts()'s conditions).
  bool WouldInterrupt(const State& s) const {
    return (fiq_signal_ && !s.fiq && s.fiq_enable) || (irq_signal_ && !s.irq && s.irq_enable);
  }
  // Set by the last Step() if it jumped back a short way (a branch, or
  // pc = imm16 / goto a22 1-32 words back): the hint that starts Spg200's
  // wait-loop detection. Reading it clears it.
  bool TakeBackJump() {
    const bool b = back_jump_;
    back_jump_ = false;
    return b;
  }

 private:
  enum Reg { SP = 0, R1, R2, R3, R4, BP, SR, PC };
  enum Alu {
    ADD = 0, ADC = 1, SUB = 2, SBC = 3, CMP = 4, NEG = 6,
    XOR = 8, LOAD = 9, OR = 10, AND = 11, TEST = 12, STORE = 13
  };

  u32 CsPc() const { return ((u32)(r_[SR] & 0x3F) << 16) | r_[PC]; }
  void SetCsPc(u32 v) {
    r_[PC] = v & 0xFFFF;
    r_[SR] = (r_[SR] & 0xFFC0) | ((v >> 16) & 0x3F);
  }
  u16 Fetch();
  void Push(u16& sp, u16 val);
  u16 Pop(u16& sp);
  bool CheckInterrupts();
  bool BranchTaken(int op0) const;
  void AluOp(u16& save, u16 val1, u16 val2, int op, bool flags);
  void UpdateNz(u32 res);
  void UpdateNzsc(u32 res, s32 res_signed);

  Spg200& bus_;
  u16 r_[8]{};
  u8 sb_[3]{};  // shift buffer, banked: [0] normal, [1] IRQ, [2] FIQ
  bool irq_ = false, fiq_ = false;
  bool irq_enable_ = false, fiq_enable_ = false;
  bool fir_mov_ = true;
  u16 irq_signal_ = 0;  // level state of IRQ0..7
  bool fiq_signal_ = false;
  bool back_jump_ = false;  // see TakeBackJump()
};

}  // namespace dsmile
