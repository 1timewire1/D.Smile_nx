#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Identifies a V.Smile system ROM by CRC-32 against the known dumps (the
// same CRCs MAME's vsmile/vsmilem drivers list), including byte-swapped
// copies of each - a 16-bit ROM dumped with the wrong byte order is a
// common way these files circulate, and the core expects little-endian
// words, so a swapped file has to be un-swapped before it will boot.
// Which BIOS version is loaded matters for more than trivia: the Region
// setting's codes mean different things on v1.00, v1.02 and v1.03 (see
// switch/README.md's "Region labels per BIOS version").

enum class BiosFamily { Unknown, Standard, Motion };

struct BiosInfo {
  BiosFamily family = BiosFamily::Unknown;
  // The standard BIOS code inside: 100, 102 or 103. 0 for the Spanish Motion
  // dump, whose code is its own. The US Motion dump is v1.03 word for word
  // (bar a "078800VER0" tag in the unreachable first 10 words - 0788 is the
  // unit's FCC ID/PCB code), so it's 103: v1.03 region labels, test screen,
  // and the plain V.Smile intro, as on that console (MAME: "doesn't show the
  // 'Motion' logo at all, but was dumped from a Motion unit").
  int version = 0;
  const char* label = "";   // e.g. "v1.03", "Motion (US unit)". "" if unrecognized.
  bool byteswapped = false;
};

uint32_t switch_bios_crc32(const uint8_t* data, size_t size);
BiosInfo switch_bios_identify_crc(uint32_t crc);

// Reads and CRCs the whole file (2MB), cached by path for the process
// lifetime - only meant for settings screens, which redraw every frame.
BiosInfo switch_bios_identify_file(const std::string& path);

// Swaps the two bytes of every 16-bit word in place when info.byteswapped.
void switch_bios_fix_byte_order(std::vector<uint8_t>& data, const BiosInfo& info);
