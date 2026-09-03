#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>

#ifdef DSMILE_TRACE
#include <cstdio>
#define DTRACE(...) std::printf(__VA_ARGS__)
#else
#define DTRACE(...) ((void)0)
#endif

namespace dsmile {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// 27 MHz master clock
constexpr u32 kSysClock = 27000000;

constexpr s32 sext(u32 v, int bits) {
  const u32 m = 1u << (bits - 1);
  return (s32)((v ^ m) - m);
}

// Rational countdown clock: fires `mul` times every `add` master cycles
// (i.e. at 27MHz * mul / add Hz), with exact accounting.
struct CycleClock {
  s64 counter = 0;
  s32 add;
  s32 mul;
  explicit CycleClock(s32 add_, s32 mul_ = 1) : add(add_), mul(mul_) { counter = add; }
  // Returns number of times the clock fired during `cycles`.
  int Tick(int cycles) {
    counter -= (s64)cycles * mul;
    int fired = 0;
    while (counter <= 0) { counter += add; fired++; }
    return fired;
  }
  // Subtract-per-call variant: `cycles` is charged on every call, including
  // the re-check after a fire. The SPG200 timebase behaves this way relative
  // to the CPU clock; the divided ticks it feeds (timers, TMB, 4-4096 Hz)
  // depend on that phase, so exact accounting here breaks timer-paced games.
  bool TickOnce(int cycles) {
    counter -= (s64)cycles * mul;
    if (counter <= 0) { counter += add; return true; }
    return false;
  }
  void Reset() { counter = add; }
};

}  // namespace dsmile
