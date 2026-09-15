#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Minimal ZIP reading for V.Smile game cartridges (.zip archives containing
// a .bin dump - and optionally cover art, though switch_library.cpp looks
// for that next to the .zip itself, not inside it). BIOS selection stays
// .bin only - no zip BIOS support, matches the ROM-only scope this covers.

// Attempts to extract the first .bin file found in a .zip archive.
// Returns true on success, filling out_data with the decompressed .bin
// content. Returns false if the zip file is invalid/corrupt, contains no
// .bin file, or uses a zip feature this minimal reader doesn't support
// (see switch_zip.cpp's top comment for exactly what that covers).
bool switch_zip_extract_bin(const std::string& zip_path, std::vector<uint8_t>& out_data);

// Checks if a file is a valid .zip archive by reading the ZIP signature.
bool switch_zip_is_valid(const std::string& zip_path);
