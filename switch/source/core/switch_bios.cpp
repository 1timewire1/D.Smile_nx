#include "switch_bios.h"

#include <cstdio>
#include <map>
#include <utility>

namespace {

struct KnownBios {
  uint32_t crc;
  BiosFamily family;
  int version;
  const char* label;
  bool byteswapped;
};

// Normal-order CRCs are MAME's (src/mame/vtech/vsmile.cpp ROM_START(vsmile)
// and ROM_START(vsmilem)); the swapped ones were computed from the same
// dumps with each 16-bit word's bytes exchanged.
constexpr KnownBios kKnown[] = {
    {0x205c5296u, BiosFamily::Standard, 100, "v1.00", false},
    {0x6a895ae0u, BiosFamily::Standard, 100, "v1.00", true},
    {0x0cd0bdf5u, BiosFamily::Standard, 102, "v1.02", false},
    {0x11f1b416u, BiosFamily::Standard, 102, "v1.02", true},
    {0x387fbc24u, BiosFamily::Standard, 103, "v1.03", false},
    {0xdceadd42u, BiosFamily::Standard, 103, "v1.03", true},
    {0x60fa5426u, BiosFamily::Motion, 0, "Motion (Spanish unit)", false},
    {0xe4923747u, BiosFamily::Motion, 0, "Motion (Spanish unit)", true},
    {0x427087eau, BiosFamily::Motion, 103, "Motion (US unit, v1.03)", false},
    {0xb98bbbc9u, BiosFamily::Motion, 103, "Motion (US unit, v1.03)", true},
};

const uint32_t* CrcTable() {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    ready = true;
  }
  return table;
}

uint32_t CrcUpdate(uint32_t crc, const uint8_t* data, size_t size) {
  const uint32_t* table = CrcTable();
  for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}

}  // namespace

uint32_t switch_bios_crc32(const uint8_t* data, size_t size) {
  return CrcUpdate(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

BiosInfo switch_bios_identify_crc(uint32_t crc) {
  for (const KnownBios& k : kKnown) {
    if (k.crc == crc) return BiosInfo{k.family, k.version, k.label, k.byteswapped};
  }
  return BiosInfo{};
}

BiosInfo switch_bios_identify_file(const std::string& path) {
  static std::map<std::string, BiosInfo> cache;
  auto it = cache.find(path);
  if (it != cache.end()) return it->second;

  BiosInfo info;
  if (FILE* f = fopen(path.c_str(), "rb")) {
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) crc = CrcUpdate(crc, buf, n);
    fclose(f);
    info = switch_bios_identify_crc(crc ^ 0xFFFFFFFFu);
  }
  cache.emplace(path, info);
  return info;
}

void switch_bios_fix_byte_order(std::vector<uint8_t>& data, const BiosInfo& info) {
  if (!info.byteswapped) return;
  for (size_t i = 0; i + 1 < data.size(); i += 2) std::swap(data[i], data[i + 1]);
}
