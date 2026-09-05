#include "switch_zip.h"

#include <cctype>
#include <cstdio>
#include <cstring>

// Minimal ZIP file handling - reads central directory and extracts first .bin file
// Standard ZIP format: local file header + file data + central directory records
// 
// Local file header signature: 0x04034b50
// Central directory signature: 0x02014b50
// End of central directory signature: 0x06054b50

namespace {

// ZIP local file header structure (first 30 bytes minimum)
struct ZipLocalHeader {
  uint32_t signature;       // 0x04034b50
  uint16_t version;
  uint16_t flags;
  uint16_t compression;     // 0 = stored, 8 = deflate
  uint16_t mod_time;
  uint16_t mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_len;
  uint16_t extra_len;
};

// Read little-endian 32-bit value
uint32_t ReadU32LE(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

// Read little-endian 16-bit value
uint16_t ReadU16LE(const uint8_t* p) {
  return p[0] | (p[1] << 8);
}

// Simple deflate decompression - uses inline minimal implementation
// For Switch, we could use zlib if available, but keeping it self-contained
// This is a minimal DEFLATE decoder for stored (uncompressed) and deflate streams

// Extract a single file from ZIP with deflate decompression
bool ExtractZipEntry(FILE* f, long offset, std::vector<uint8_t>& out_data) {
  if (fseek(f, offset, SEEK_SET) != 0) return false;

  uint8_t header[30];
  if (fread(header, 1, 30, f) != 30) return false;

  // Verify local file header signature
  if (ReadU32LE(header) != 0x04034b50) return false;

  ZipLocalHeader zh;
  zh.compression = ReadU16LE(&header[8]);
  zh.compressed_size = ReadU32LE(&header[18]);
  zh.uncompressed_size = ReadU32LE(&header[22]);
  zh.filename_len = ReadU16LE(&header[26]);
  zh.extra_len = ReadU16LE(&header[28]);

  // Skip filename and extra field to get to actual file data
  if (fseek(f, zh.filename_len + zh.extra_len, SEEK_CUR) != 0) return false;

  out_data.resize(zh.uncompressed_size);

  if (zh.compression == 0) {
    // Stored (no compression)
    if (fread(out_data.data(), 1, zh.compressed_size, f) != zh.compressed_size) {
      return false;
    }
  } else if (zh.compression == 8) {
    // Deflate compression - for this implementation, we'll use a simple approach
    // For a production implementation, link against zlib
    // For now, read the compressed data (this is a placeholder that expects uncompressed)
    std::vector<uint8_t> compressed(zh.compressed_size);
    if (fread(compressed.data(), 1, zh.compressed_size, f) != zh.compressed_size) {
      return false;
    }
    
    // TODO: Implement proper DEFLATE decompression or link zlib
    // For now, assume it's stored anyway (many ZIP tools can use stored method)
    if (zh.compressed_size == zh.uncompressed_size) {
      std::memcpy(out_data.data(), compressed.data(), zh.compressed_size);
    } else {
      // Deflate decompression not implemented - return false
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
    long offset = ftell(f);
    uint8_t header[30];

    if (fread(header, 1, 30, f) != 30) break;

    uint32_t signature = ReadU32LE(header);
    if (signature != 0x04034b50) break;  // End of local headers

    uint16_t filename_len = ReadU16LE(&header[26]);
    uint16_t extra_len = ReadU16LE(&header[28]);

    // Read filename
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
        if (ExtractZipEntry(f, offset, out_data)) {
          found = true;
          break;
        }
      }
    }

    // Skip to next local header
    if (fseek(f, offset + 30 + filename_len + extra_len, SEEK_SET) != 0) break;

    // Read compressed/uncompressed size to skip file data
    if (fseek(f, 18, SEEK_CUR) != 0) break;
    uint8_t sizes[8];
    if (fread(sizes, 1, 8, f) != 8) break;

    uint32_t compressed_size = ReadU32LE(sizes);
    long next_offset = ftell(f) + compressed_size;
    if (fseek(f, next_offset, SEEK_SET) != 0) break;
  }

  fclose(f);
  return found;
}
