#include "switch_zip.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <zlib.h>

// Minimal ZIP file handling - reads local file headers directly and
// extracts the first .bin entry found. Standard ZIP layout: a local file
// header immediately followed by that entry's (possibly compressed) data,
// repeated per entry, with a central directory afterward that this doesn't
// need to touch. Local file header signature: 0x04034b50.
//
// Supports the two compression methods essentially every desktop zip tool
// actually produces: 0 (stored) and 8 (deflate, via zlib's raw inflate -
// the same library switch/Makefile already links for libpng, so no new
// dependency). Deliberately does NOT support the streamed case where a
// tool omits sizes from the local header and appends a data descriptor
// after the entry instead (general-purpose flag bit 3) - that needs the
// central directory to know how much data to read at all, which is more
// machinery than a single-cart-per-zip reader needs; such a zip is
// rejected rather than misread.

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

uint16_t ReadU16LE(const uint8_t* p) {
  return p[0] | (p[1] << 8);
}

// Raw DEFLATE (no zlib/gzip wrapper - that's what's actually inside a ZIP
// entry) via zlib's inflate, windowBits=-15 selecting the raw variant.
bool InflateRaw(const std::vector<uint8_t>& compressed, uint32_t uncompressed_size,
                 std::vector<uint8_t>& out_data) {
  out_data.assign(uncompressed_size, 0);
  if (uncompressed_size == 0) return true;

  z_stream strm{};
  if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;

  strm.next_in = reinterpret_cast<Bytef*>(const_cast<uint8_t*>(compressed.data()));
  strm.avail_in = (uInt)compressed.size();
  strm.next_out = reinterpret_cast<Bytef*>(out_data.data());
  strm.avail_out = (uInt)out_data.size();

  const int ret = inflate(&strm, Z_FINISH);
  inflateEnd(&strm);
  return ret == Z_STREAM_END;
}

// Extracts one entry given the file offset of its local file header.
bool ExtractZipEntry(FILE* f, long offset, std::vector<uint8_t>& out_data) {
  if (fseek(f, offset, SEEK_SET) != 0) return false;

  uint8_t header[30];
  if (fread(header, 1, 30, f) != 30) return false;
  if (ReadU32LE(header) != 0x04034b50) return false;

  const uint16_t gp_flag = ReadU16LE(&header[6]);
  const uint16_t compression = ReadU16LE(&header[8]);
  const uint32_t crc32_expected = ReadU32LE(&header[14]);
  const uint32_t compressed_size = ReadU32LE(&header[18]);
  const uint32_t uncompressed_size = ReadU32LE(&header[22]);
  const uint16_t filename_len = ReadU16LE(&header[26]);
  const uint16_t extra_len = ReadU16LE(&header[28]);

  // Bit 3 ("data descriptor follows") means the sizes/CRC above are all
  // zero and the real ones are appended after the entry's data instead -
  // not supported here, see the file-level comment above.
  if (gp_flag & 0x0008) return false;

  if (fseek(f, filename_len + extra_len, SEEK_CUR) != 0) return false;

  std::vector<uint8_t> compressed(compressed_size);
  if (compressed_size > 0 && fread(compressed.data(), 1, compressed_size, f) != compressed_size) {
    return false;
  }

  if (compression == 0) {
    if (compressed_size != uncompressed_size) return false;  // "stored" implies equal sizes
    out_data = std::move(compressed);
  } else if (compression == 8) {
    if (!InflateRaw(compressed, uncompressed_size, out_data)) return false;
  } else {
    return false;  // unsupported compression method
  }

  return crc32(0, out_data.data(), (uInt)out_data.size()) == crc32_expected;
}

}  // namespace

bool switch_zip_is_valid(const std::string& zip_path) {
  FILE* f = fopen(zip_path.c_str(), "rb");
  if (!f) return false;

  uint8_t sig[4];
  bool valid = false;
  if (fread(sig, 1, 4, f) == 4) valid = (ReadU32LE(sig) == 0x04034b50);

  fclose(f);
  return valid;
}

bool switch_zip_extract_bin(const std::string& zip_path, std::vector<uint8_t>& out_data) {
  FILE* f = fopen(zip_path.c_str(), "rb");
  if (!f) return false;

  bool found = false;

  while (!found) {
    const long entry_offset = ftell(f);
    uint8_t header[30];
    if (fread(header, 1, 30, f) != 30) break;
    if (ReadU32LE(header) != 0x04034b50) break;  // end of local headers

    const uint32_t compressed_size = ReadU32LE(&header[18]);
    const uint16_t filename_len = ReadU16LE(&header[26]);
    const uint16_t extra_len = ReadU16LE(&header[28]);

    std::vector<char> filename(filename_len + 1, '\0');
    if (fread(filename.data(), 1, filename_len, f) != filename_len) break;

    std::string fname_lower(filename.data());
    for (char& c : fname_lower) c = (char)std::tolower((unsigned char)c);

    if (fname_lower.size() >= 4 && fname_lower.substr(fname_lower.size() - 4) == ".bin") {
      if (ExtractZipEntry(f, entry_offset, out_data)) {
        found = true;
        break;
      }
      // Corrupt/unsupported .bin entry: keep scanning in case a later
      // entry (unlikely, but cheap to allow) works instead, rather than
      // failing the whole archive on the first candidate.
    }

    const long next_offset = entry_offset + 30 + filename_len + extra_len + (long)compressed_size;
    if (fseek(f, next_offset, SEEK_SET) != 0) break;
  }

  fclose(f);
  return found;
}
