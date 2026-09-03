#include "unsp.h"

#include <bit>

#include "spg200.h"
#include "state.h"

#ifdef DSMILE_TRACE
#include <cstdio>
FILE* g_unsp_trace = nullptr;  // set by host trace tools; one 16-byte record per Step
#endif

namespace dsmile {

namespace {
constexpr s32 Sext20(u32 v) { return sext(v & 0xFFFFF, 20); }
constexpr u32 Rot20L(u32 v, int n) { v &= 0xFFFFF; return ((v << n) | (v >> (20 - n))) & 0xFFFFF; }
constexpr u32 Rot20R(u32 v, int n) { v &= 0xFFFFF; return ((v >> n) | (v << (20 - n))) & 0xFFFFF; }
}  // namespace

void UnSP::SaveState(StateWriter& w) const {
  w.Arr16(r_, 8);
  w.Arr8(sb_, 3);
  w.B(irq_); w.B(fiq_);
  w.B(irq_enable_); w.B(fiq_enable_);
  w.B(fir_mov_);
  w.U16(irq_signal_);
  w.B(fiq_signal_);
}

void UnSP::LoadState(StateReader& r) {
  r.Arr16(r_, 8);
  r.Arr8(sb_, 3);
  irq_ = r.B(); fiq_ = r.B();
  irq_enable_ = r.B(); fiq_enable_ = r.B();
  fir_mov_ = r.B();
  irq_signal_ = r.U16();
  fiq_signal_ = r.B();
}

void UnSP::Reset() {
  for (auto& r : r_) r = 0;
  sb_[0] = sb_[1] = sb_[2] = 0;
  irq_ = fiq_ = false;
  irq_enable_ = fiq_enable_ = false;
  fir_mov_ = true;
  irq_signal_ = 0;
  fiq_signal_ = false;
  r_[PC] = bus_.Read(0xFFF7);
}

inline u16 UnSP::Fetch() {
  const u32 a = CsPc();
  const u16 v = bus_.Read(a);
  SetCsPc(a + 1);
  return v;
}

inline void UnSP::Push(u16& sp, u16 val) { bus_.Write(sp--, val); }
inline u16 UnSP::Pop(u16& sp) { return bus_.Read(++sp); }

bool UnSP::CheckInterrupts() {
  if (fiq_signal_ && !fiq_ && fiq_enable_) {
    fiq_ = true;
    Push(r_[SP], r_[PC]);
    Push(r_[SP], r_[SR]);
    r_[PC] = bus_.Read(0xFFF6);
    r_[SR] = 0;
    return true;
  }
  if (irq_signal_ && !irq_ && irq_enable_) {
    const int line = std::countr_zero(irq_signal_);
    DTRACE("[cpu] take irq%d\n", line);
    irq_ = true;
    Push(r_[SP], r_[PC]);
    Push(r_[SP], r_[SR]);
    r_[PC] = bus_.Read(0xFFF8 + line);
    r_[SR] = 0;
    return true;
  }
  return false;
}

void UnSP::UpdateNz(u32 res) {
  r_[SR] = (r_[SR] & ~0x0300u)
         | ((res & 0x8000) ? 0x0200 : 0)
         | (((res & 0xFFFF) == 0) ? 0x0100 : 0);
}

void UnSP::UpdateNzsc(u32 res, s32 res_signed) {
  r_[SR] = (r_[SR] & ~0x03C0u)
         | ((res & 0x8000) ? 0x0200 : 0)
         | (((res & 0xFFFF) == 0) ? 0x0100 : 0)
         | ((res_signed < 0) ? 0x0080 : 0)
         | ((res & 0x10000) ? 0x0040 : 0);
}

void UnSP::AluOp(u16& save, u16 val1, u16 val2, int op, bool flags) {
  switch (op) {
    case ADD:
    case ADC: {
      const u32 carry = (op == ADC) ? ((r_[SR] >> 6) & 1) : 0;
      const u32 res = (u32)val1 + val2 + carry;
      const s32 rs = (s16)val1 + (s16)val2 + (s32)carry;
      if (flags) UpdateNzsc(res, rs);
      save = (u16)res;
      return;
    }
    case SUB:
    case SBC:
    case CMP: {
      const u32 carry = (op == SBC) ? ((r_[SR] >> 6) & 1) : 1;
      const u32 res = (u32)val1 + (u16)~val2 + carry;
      const s32 rs = (s16)val1 + (s16)~val2 + (s32)carry;
      if (flags) UpdateNzsc(res, rs);
      if (op != CMP) save = (u16)res;
      return;
    }
    case NEG: {
      const u32 res = (u32)(u16)~val2 + 1;
      if (flags) UpdateNz(res);
      save = (u16)res;
      return;
    }
    case XOR: { const u16 res = val1 ^ val2; if (flags) UpdateNz(res); save = res; return; }
    case LOAD: { if (flags) UpdateNz(val2); save = val2; return; }
    case OR: { const u16 res = val1 | val2; if (flags) UpdateNz(res); save = res; return; }
    case AND:
    case TEST: {
      const u16 res = val1 & val2;
      if (flags) UpdateNz(res);
      if (op != TEST) save = res;
      return;
    }
    default: return;  // invalid alu op: no effect
  }
}

bool UnSP::BranchTaken(int op0) const {
  const bool c = r_[SR] & 0x0040, s = r_[SR] & 0x0080, z = r_[SR] & 0x0100, n = r_[SR] & 0x0200;
  switch (op0) {
    case 0: return !c;           // JB
    case 1: return c;            // JAE
    case 2: return !s;           // JGE
    case 3: return s;            // JL
    case 4: return !z;           // JNE
    case 5: return z;            // JE
    case 6: return !n;           // JPL
    case 7: return n;            // JMI
    case 8: return !(!z && c);   // JBE
    case 9: return !z && c;      // JA
    case 10: return !(!z && !s); // JLE
    case 11: return !z && !s;    // JG
    case 12: return n == s;      // JVC
    case 13: return n != s;      // JVS
    default: return true;        // JMP
  }
}

int UnSP::Step() {
#ifdef DSMILE_TRACE
  if (g_unsp_trace) fwrite(r_, 2, 8, g_unsp_trace);
#endif
  if (CheckInterrupts()) return 10;

  const u16 op = Fetch();
  const int op0 = (op >> 12) & 0xF;
  const int rd = (op >> 9) & 7;
  const int op1 = (op >> 6) & 7;
  const int op1n = (op >> 3) & 0x3F;
  const int muls_n = (op >> 3) & 0xF;
  const int opn = (op >> 3) & 7;
  const int rs = op & 7;
  const int imm6 = op & 0x3F;

  if (op0 == 0xF) {
    switch (op1) {
      case 0:  // MUL us  (also DS-register ops on 1.2)
        if (opn == 1 && rd != PC && rs != PC) {
          const u32 result = (u32)(r_[rd] * (s32)(s16)r_[rs]);
          r_[R3] = result & 0xFFFF;
          r_[R4] = (result >> 16) & 0xFFFF;
          return 12;
        }
        if ((op & 0xF1F8) == 0xF020) { r_[rd] = GetDs(); return 2; }   // Rx = DS
        if ((op & 0xF1F8) == 0xF028) { SetDs(r_[rd]); return 2; }      // DS = Rx
        return 2;  // unknown: treat as nop
      case 1: {  // CALL a22
        const u32 new_pc = ((u32)imm6 << 16) | Fetch();
        Push(r_[SP], r_[PC]);
        Push(r_[SP], r_[SR]);
        SetCsPc(new_pc);
        return 9;
      }
      case 2:  // JMPF (goto a22) when rd==PC, else MULS us
        if (rd == PC) {
          const u32 new_pc = ((u32)imm6 << 16) | Fetch();
          SetCsPc(new_pc);
          return 5;
        }
        [[fallthrough]];
      case 3:  // MULS us (FIR), or JMPR
        if ((op & 0xFFC0) == 0xFEC0) {  // JMPR: goto MR (1.2)
          SetCsPc(((u32)(r_[R4] & 0x3F) << 16) | r_[R3]);
          return 5;
        }
        if (rd != PC && rs != PC) {
          const int n = muls_n ? muls_n : 16;
          s64 sum = 0;
          u16 old_val1 = 0;
          for (int i = 0; i < n; i++) {
            const u16 v1 = bus_.Read(r_[rd]);
            const u16 v2 = bus_.Read(r_[rs]);
            sum += (s64)v1 * (s16)v2;
            if (fir_mov_) {
              if (i > 0) bus_.Write(r_[rd], old_val1);
              old_val1 = v1;
            }
            r_[rd]++;
            r_[rs]++;
          }
          r_[R3] = sum & 0xFFFF;
          r_[R4] = (sum >> 16) & 0xFFFF;
          return 10 * n + 6;
        }
        return 2;
      case 4:  // MUL ss
        if (opn == 1 && rd != PC && rs != PC) {
          const u32 result = (u32)((s32)(s16)r_[rd] * (s32)(s16)r_[rs]);
          r_[R3] = result & 0xFFFF;
          r_[R4] = (result >> 16) & 0xFFFF;
          return 12;
        }
        return 2;
      case 5:  // interrupt control / misc
        switch (imm6) {
          case 0: case 1: case 2: case 3:  // INT SET
            irq_enable_ = imm6 & 1;
            fiq_enable_ = imm6 & 2;
            return 2;
          case 4: case 5:  // FIR_MOV ON/OFF
            fir_mov_ = !(imm6 & 1);
            return 2;
          case 8: case 9:  // IRQ OFF/ON
            irq_enable_ = imm6 & 1;
            return 2;
          case 12: case 14:  // FIQ OFF/ON
            fiq_enable_ = imm6 & 2;
            return 2;
          case 32: case 40: case 48: case 56: {  // BREAK
            Push(r_[SP], r_[PC]);
            Push(r_[SP], r_[SR]);
            r_[PC] = bus_.Read(0xFFF5);
            r_[SR] = 0;
            return 10;
          }
          case 33: case 41: case 49: case 57: {  // CALLR (call MR, 1.2)
            const u32 new_pc = ((u32)(r_[R4] & 0x3F) << 16) | r_[R3];
            Push(r_[SP], r_[PC]);
            Push(r_[SP], r_[SR]);
            SetCsPc(new_pc);
            return 9;
          }
          case 36: case 44: case 52: case 60: {  // EXP R4 (1.2)
            const u16 v = r_[R4];
            r_[R2] = (v & 0x8000) ? (u16)(std::countl_one((u16)v) - 1)
                                  : (u16)(std::countl_zero((u16)v) - 1);
            return 1;
          }
          case 37: case 45: case 53: case 61:  // NOP
            return 2;
          default:
            return 2;  // unimplemented 1.2 (FRACTION/SECBANK/IRQNEST/DIVS/DIVQ): nop
        }
      case 6:
      case 7:  // MULS ss (FIR)
        if (rd != PC && rs != PC) {
          const int n = muls_n ? muls_n : 16;
          s64 sum = 0;
          u16 old_val1 = 0;
          for (int i = 0; i < n; i++) {
            const u16 v1 = bus_.Read(r_[rd]);
            const u16 v2 = bus_.Read(r_[rs]);
            sum += (s64)(s16)v1 * (s16)v2;
            if (fir_mov_) {
              if (i > 0) bus_.Write(r_[rd], old_val1);
              old_val1 = v1;
            }
            r_[rd]++;
            r_[rs]++;
          }
          r_[R3] = sum & 0xFFFF;
          r_[R4] = (sum >> 16) & 0xFFFF;
          return 10 * n + 6;
        }
        return 2;
      default:
        return 2;
    }
  }

  // ALU / branch / push-pop / addressing modes
  switch (op1n >> 3) {
    case 0:  // [BP+imm6], or branch forward when rd==PC
      if (rd == PC) {
        const bool taken = BranchTaken(op0);
        if (taken) SetCsPc(CsPc() + imm6);
        return taken ? 4 : 2;
      } else {
        const u16 addr = (u16)(r_[BP] + imm6);
        if (op0 != STORE) {
          AluOp(r_[rd], r_[rd], bus_.Read(addr), op0, rd != PC);
        } else {
          bus_.Write(addr, r_[rd]);
        }
        return 6;
      }
    case 1:  // imm6, or branch backward when rd==PC
      if (rd == PC) {
        const bool taken = BranchTaken(op0);
        if (taken) SetCsPc(CsPc() - imm6);
        return taken ? 4 : 2;
      } else {
        if (op0 != STORE) AluOp(r_[rd], r_[rd], imm6, op0, rd != PC);
        return 2;
      }
    case 2: {  // push / pop (and RETF/RETI as pop forms)
      int n = opn;
      int reg = rd;
      if (op0 == LOAD) {
        if (rd == BP && opn == 3 && rs == SP) {  // RETI
          if (fiq_) fiq_ = false;
          else if (irq_) irq_ = false;
          n = 2;
        }
        int left = n;
        while (left-- && (reg + 1) <= 7) r_[++reg] = Pop(r_[rs]);
      } else if (op0 == STORE) {
        int left = n;
        while (left-- && reg >= 0) Push(r_[rs], r_[reg--]);
      }
      return 2 * n + 4;
    }
    case 3: {  // indirect [Rs] forms
      u32 addr;
      const u16 ds = GetDs();
      switch (op1n & 7) {
        case 0: addr = r_[rs]; break;
        case 1: addr = r_[rs]--; break;
        case 2: addr = r_[rs]++; break;
        case 3: addr = ++r_[rs]; break;
        case 4: addr = ((u32)ds << 16) | r_[rs]; break;
        case 5:
          addr = ((u32)ds << 16) | r_[rs]--;
          if (r_[rs] == 0xFFFF) SetDs(ds - 1);
          break;
        case 6:
          addr = ((u32)ds << 16) | r_[rs]++;
          if (r_[rs] == 0x0000) SetDs(ds + 1);
          break;
        default:
          ++r_[rs];
          if (r_[rs] == 0x0000) { SetDs(ds + 1); addr = ((u32)GetDs() << 16) | r_[rs]; }
          else addr = ((u32)ds << 16) | r_[rs];
          break;
      }
      if (op0 != STORE) {
        AluOp(r_[rd], r_[rd], bus_.Read(addr), op0, rd != PC);
      } else {
        bus_.Write(addr, r_[rd]);
      }
      return rd == PC ? 7 : 6;
    }
    case 4:
      switch (opn) {
        case 0:  // register
          if (op0 != STORE) AluOp(r_[rd], r_[rd], r_[rs], op0, rd != PC);
          return rd == PC ? 5 : 3;
        case 1: {  // Rd = Rs op imm16
          const u16 rs_val = r_[rs];
          const u16 imm = Fetch();
          if (op0 != STORE) AluOp(r_[rd], rs_val, imm, op0, rd != PC);
          return rd == PC ? 5 : 4;
        }
        case 2: {  // Rd = Rs op [A16]
          const u16 rs_val = r_[rs];
          const u16 addr = Fetch();
          if (op0 != STORE) AluOp(r_[rd], rs_val, bus_.Read(addr), op0, rd != PC);
          return rd == PC ? 8 : 7;
        }
        case 3: {  // [A16] = Rs op Rd
          const u16 rs_val = r_[rs];
          const u16 rd_val = r_[rd];
          const u16 addr = Fetch();
          if (op0 != STORE) {
            u16 result = 0;
            AluOp(result, rs_val, rd_val, op0, rd != PC);
            bus_.Write(addr, result);
          } else {
            bus_.Write(addr, rs_val);
          }
          return rd == PC ? 8 : 7;
        }
        default: {  // Rd = Rd op (Rs ASR n)
          u8& sb = sb_[fiq_ ? 2 : (irq_ ? 1 : 0)];
          const int n = (opn & 3) + 1;
          const s32 shift = Sext20(((u32)r_[rs] << 4) | sb) >> n;
          const u16 value = (shift >> 4) & 0xFFFF;
          sb = shift & 0xF;
          if (op0 != STORE) AluOp(r_[rd], r_[rd], value, op0, rd != PC);
          return rd == PC ? 5 : 3;
        }
      }
    case 5: {  // LSL / LSR
      u8& sb = sb_[fiq_ ? 2 : (irq_ ? 1 : 0)];
      u16 value;
      if (opn < 4) {
        const int n = (opn & 3) + 1;
        const u32 shift = (((u32)sb << 16) | r_[rs]) << n;
        value = shift & 0xFFFF;
        sb = (shift >> 16) & 0xF;
      } else {
        const int n = (opn & 3) + 1;
        const u32 shift = (((u32)r_[rs] << 4) | sb) >> n;
        value = (shift >> 4) & 0xFFFF;
        sb = shift & 0xF;
      }
      if (op0 != STORE) AluOp(r_[rd], r_[rd], value, op0, rd != PC);
      return rd == PC ? 5 : 3;
    }
    case 6: {  // ROL / ROR (20-bit through SB)
      u8& sb = sb_[fiq_ ? 2 : (irq_ ? 1 : 0)];
      const int n = (opn & 3) + 1;
      u16 value;
      if (opn < 4) {
        const u32 shift = Rot20L(((u32)sb << 16) | r_[rs], n);
        value = shift & 0xFFFF;
        sb = (shift >> 16) & 0xF;
      } else {
        const u32 shift = Rot20R(((u32)r_[rs] << 4) | sb, n);
        value = (shift >> 4) & 0xFFFF;
        sb = shift & 0xF;
      }
      if (op0 != STORE) AluOp(r_[rd], r_[rd], value, op0, rd != PC);
      return rd == PC ? 5 : 3;
    }
    default: {  // case 7: [imm6] direct
      if (op0 != STORE) {
        AluOp(r_[rd], r_[rd], bus_.Read(imm6), op0, rd != PC);
      } else {
        bus_.Write(imm6, r_[rd]);
      }
      return rd == PC ? 6 : 5;
    }
  }
}

}  // namespace dsmile
