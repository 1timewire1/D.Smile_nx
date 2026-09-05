#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ZIP file handling for V.Smile game cartridges (.zip archives containing .bin files)
// BIOS selection remains .bin only (no zip BIOS support)

// Attempts to extract the first .bin file found in a .zip archive.
// Returns true on success, filling out_data with the decompressed .bin content.
// Returns false if the zip file is invalid, corrupt, or contains no .bin files.
bool switch_zip_extract_bin(const std::string& zip_path, std::vector<uint8_t>& out_data);

// Checks if a file is a valid .zip archive by reading the ZIP signature
bool switch_zip_is_valid(const std::string& zip_path);
