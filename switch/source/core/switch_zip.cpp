#include "switch_zip.h"

#include <cctype>
#include <cstdio>
#include <cstring>

// Minimal ZIP file handling - reads local file headers and extracts first .bin file
// Standard ZIP format: local file header + file data, repeated for each file
// Local file header signature: 0x04034b50

namespace {

// Read little-endian 32-bit value
uint32_t ReadU32LE(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

// Read little-endian 16-bit value
uint16_t ReadU16LE(const uint8_t* p) {
  return p[0] | (p[1] << 8);
}

// Extract a single file from ZIP
bool ExtractZipEntry(FILE* f, long offset, std::vector<uint8_t>& out_data) {
  if (fseek(f, offset, SEEK_SET) != 0) return false;

  uint8_t header[30];
  if (fread(header, 1, 30, f) != 30) return false;

  // Verify local file header signature
  if (ReadU32LE(header) != 0x04034b50) return false;

  uint16_t compression = ReadU16LE(&header[8]);
  uint32_t compressed_size = ReadU32LE(&header[18]);
  uint32_t uncompressed_size = ReadU32LE(&header[22]);
  uint16_t filename_len = ReadU16LE(&header[26]);
  uint16_t extra_len = ReadU16LE(&header[28]);

  // Skip filename and extra field to get to actual file data
  if (fseek(f, filename_len + extra_len, SEEK_CUR) != 0) return false;

  out_data.resize(uncompressed_size);

  if (compression == 0) {
    // Stored (no compression)
    if (fread(out_data.data(), 1, compressed_size, f) != compressed_size) {
      return false;
    }
  } else if (compression == 8) {
    // Deflate compression - read compressed data and attempt decompression
    // For now, only support if sizes match (data was stored despite deflate flag)
    std::vector<uint8_t> compressed(compressed_size);
    if (fread(compressed.data(), 1, compressed_size, f) != compressed_size) {
      return false;
    }
    
    // If compressed and uncompressed sizes are the same, copy as-is
    if (compressed_size == uncompressed_size) {
      std::memcpy(out_data.data(), compressed.data(), compressed_size);
    } else {
      // TODO: Link zlib for proper DEFLATE decompression
      return false;
    }
  } else {
    // Unknown compression method
    return false;
  }

  return true;
}

}  // namespace

bool switch_zip_is_valid(const std::string& zip_path) {
  FILE* f = fopen(zip_path.c_str(), "rb");
  if (!f) return false;

  uint8_t sig[4];
  bool valid = false;
  if (fread(sig, 1, 4, f) == 4) {
    uint32_t signature = ReadU32LE(sig);
    // Check for local file header signature
    valid = (signature == 0x04034b50);
  }

  fclose(f);
  return valid;
}

bool switch_zip_extract_bin(const std::string& zip_path, std::vector<uint8_t>& out_data) {
  FILE* f = fopen(zip_path.c_str(), "rb");
  if (!f) return false;

  bool found = false;

  // Scan through all local file headers looking for the first .bin file
  while (!found) {
    long file_header_offset = ftell(f);
    uint8_t header[30];

    if (fread(header, 1, 30, f) != 30) break;

    uint32_t signature = ReadU32LE(header);
    if (signature != 0x04034b50) break;  // End of local headers

    uint16_t compression = ReadU16LE(&header[8]);
    uint32_t compressed_size = ReadU32LE(&header[18]);
    uint32_t uncompressed_size = ReadU32LE(&header[22]);
    uint16_t filename_len = ReadU16LE(&header[26]);
    uint16_t extra_len = ReadU16LE(&header[28]);

    // Read filename to check if it's a .bin file
    std::vector<char> filename(filename_len + 1, '\0');
    if (fread(filename.data(), 1, filename_len, f) != filename_len) break;

    // Check if this is a .bin file
    std::string fname(filename.data());
    
    // Convert to lowercase for comparison
    std::string fname_lower = fname;
    for (char& c : fname_lower) c = (char)std::tolower((unsigned char)c);

    if (fname_lower.size() >= 4) {
      std::string ext = fname_lower.substr(fname_lower.size() - 4);
      if (ext == ".bin") {
        // Found a .bin file - extract it
        if (ExtractZipEntry(f, file_header_offset, out_data)) {
          found = true;
          break;
        }
      }
    }

    // Skip to next local file header
    // The next header is at: current_header_offset + 30 + filename_len + extra_len + compressed_size
    long next_offset = file_header_offset + 30 + filename_len + extra_len + compressed_size;
    if (fseek(f, next_offset, SEEK_SET) != 0) break;
  }

  fclose(f);
  return found;
}
