#pragma once
#include <string>

// A small modal folder picker: navigate the SD card's directory tree and
// pick a folder to use as a game source (Library & Storage > Game folders).
// Runs its own blocking input/render/present loop until the user confirms
// or cancels (mirrors DraStic's browseFolder(), minus USB/SMB roots - SD
// card only for now). Returns the selected folder's full sdmc: path, or
// empty if canceled.
std::string switch_dirbrowse_pick(const std::string& start_path, int viewport_w, int viewport_h);
