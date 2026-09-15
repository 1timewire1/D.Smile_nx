#include "switch_library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <glad/glad.h>

#include "switch_settings.h"
#include "switch_zip.h"
#include "third_party/stb_image.h"

namespace {

std::vector<LibraryGame> g_games;

bool HasExtension(const std::string& name, const char* ext_lower) {
  const size_t len = std::strlen(ext_lower);
  if (name.size() < len) return false;
  std::string ext = name.substr(name.size() - len);
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext == ext_lower;
}

bool FileExists(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

// Same-basename *.png next to the .bin/.zip, e.g. games/Aladdin.bin ->
// games/Aladdin.png (or games/Aladdin.zip -> games/Aladdin.png - cover art
// always lives next to the game file itself, never inside the zip). PNG
// only (matches third_party/stb_image.cpp's STBI_ONLY_PNG - same choice
// the Yokoi reference port made for its own art, keeping the decoder
// small).
uint32_t LoadCoverTexture(const std::string& game_path, int& out_w, int& out_h) {
  out_w = out_h = 0;
  std::string png_path = game_path.substr(0, game_path.size() - 4) + ".png";
  if (!FileExists(png_path)) return 0;

  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load(png_path.c_str(), &w, &h, &channels, 4);
  if (!pixels) return 0;

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  stbi_image_free(pixels);

  out_w = w;
  out_h = h;
  return tex;
}

void ScanFolder(const std::string& folder) {
  DIR* dir = opendir(folder.c_str());
  if (!dir) return;
  while (struct dirent* ent = readdir(dir)) {
    if (ent->d_name[0] == '.') continue;
    std::string name = ent->d_name;
    const std::string full_path = folder + "/" + name;

    const bool is_bin = HasExtension(name, ".bin");
    // A .zip is only listed if it actually starts with a valid ZIP local
    // file header - switch_zip_extract_bin() (called at launch time, see
    // main.cpp's LoadGame()) still has the final say on whether a .bin is
    // actually inside and extractable, but this keeps a garbage .zip from
    // cluttering the list as a game that can never launch.
    const bool is_zip = HasExtension(name, ".zip") && switch_zip_is_valid(full_path);
    if (!is_bin && !is_zip) continue;

    LibraryGame g;
    g.full_path = full_path;
    g.display_name = name.substr(0, name.size() - 4);

    struct stat st{};
    if (stat(g.full_path.c_str(), &st) == 0) g.mtime = (long long)st.st_mtime;

    g.cover_tex = LoadCoverTexture(g.full_path, g.cover_w, g.cover_h);
    g_games.push_back(std::move(g));
  }
  closedir(dir);
}

}  // namespace

void switch_library_rescan() {
  switch_library_shutdown();
  g_games.clear();

  for (const std::string& folder : switch_settings_game_folders()) {
    ScanFolder(folder);
  }
  switch_library_resort();
}

const std::vector<LibraryGame>& switch_library_games() { return g_games; }

void switch_library_resort() {
  switch (g_settings.sort_mode) {
    case SortMode::RecentlyAdded:
      std::sort(g_games.begin(), g_games.end(), [](const LibraryGame& a, const LibraryGame& b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return strcasecmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
      });
      break;
    case SortMode::RecentlyPlayed:
      std::sort(g_games.begin(), g_games.end(), [](const LibraryGame& a, const LibraryGame& b) {
        const long long pa = switch_settings_last_played(a.full_path);
        const long long pb = switch_settings_last_played(b.full_path);
        if (pa != pb) return pa > pb;
        return strcasecmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
      });
      break;
    case SortMode::Alpha:
    default:
      std::sort(g_games.begin(), g_games.end(), [](const LibraryGame& a, const LibraryGame& b) {
        return strcasecmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
      });
      break;
  }
}

void switch_library_shutdown() {
  for (auto& g : g_games) {
    if (g.cover_tex) {
      GLuint t = g.cover_tex;
      glDeleteTextures(1, &t);
    }
  }
}
