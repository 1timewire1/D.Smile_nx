#pragma once
#include "common.h"

namespace dsmile {

// Explicit field-order serialization: stable across builds/platforms.
struct StateWriter {
  std::vector<u8>& out;
  explicit StateWriter(std::vector<u8>& o) : out(o) {}
  void U8(u8 v) { out.push_back(v); }
  void U16(u16 v) { out.push_back(v & 0xFF); out.push_back(v >> 8); }
  void U32(u32 v) { U16(v & 0xFFFF); U16(v >> 16); }
  void S64(s64 v) { U32((u32)((u64)v & 0xFFFFFFFF)); U32((u32)((u64)v >> 32)); }
  void B(bool v) { U8(v ? 1 : 0); }
  void Arr16(const u16* p, size_t n) { for (size_t i = 0; i < n; i++) U16(p[i]); }
  void Arr8(const u8* p, size_t n) { out.insert(out.end(), p, p + n); }
};

struct StateReader {
  const u8* p;
  const u8* end;
  bool ok = true;
  u32 version = 0;  // set by VSmile::LoadState so blocks can migrate old layouts
  StateReader(const u8* data, size_t n) : p(data), end(data + n) {}
  u8 U8() { if (p + 1 > end) { ok = false; return 0; } return *p++; }
  u16 U16() { const u16 lo = U8(); return (u16)(lo | (U8() << 8)); }
  u32 U32() { const u32 lo = U16(); return lo | ((u32)U16() << 16); }
  s64 S64() { const u64 lo = U32(); return (s64)(lo | ((u64)U32() << 32)); }
  bool B() { return U8() != 0; }
  void Arr16(u16* dst, size_t n) { for (size_t i = 0; i < n; i++) dst[i] = U16(); }
  void Arr8(u8* dst, size_t n) {
    if (p + n > end) { ok = false; return; }
    std::memcpy(dst, p, n);
    p += n;
  }
};

}  // namespace dsmile
